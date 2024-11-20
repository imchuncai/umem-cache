// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <string.h>
#include "kv_cache.h"

struct slab_obj {
	uint64_t read_only;
	struct slab_obj_offset next_free;
	unsigned char __not_used[];
};

#define slab_obj_for_each(cache, slab, curr, temp)				\
for(temp = (void *)((char *)(slab) + (cache)->slab_objects * (cache)->obj_size),\
	curr = (slab); curr < temp; curr = (void *)((char *)curr + (cache)->obj_size))

static void free_obj_init(struct slab_obj *obj, struct slab_obj_offset next)
{
	obj->read_only = 0;
	obj->next_free = next;
}

static bool is_free_obj(void *obj)
{
	struct slab_obj *__obj = obj;
	return __obj->read_only == 0;
}

/**
 * kv_cache_init - Initialize @cache
 * @obj_size: minimum size of object that @cache allocates
 * 
 * @return: true on success, false on failure
 */
bool kv_cache_init(struct kv_cache *cache, uint16_t obj_size)
{
	obj_size = ALIGN(obj_size, SLAB_OBJ_ALIGN);
	if (obj_size > SLAB_OBJ_SIZE_MAX)
		return false;

	cache->slab_page = 1 << slab_calculate_order(obj_size);
	/* maybe we can allocate bigger object, don't do that, bigger object
	may have a better order */
	cache->obj_size = obj_size;
	cache->slab_objects = (cache->slab_page << PAGE_SHIFT) / obj_size;
	cache->free_objects = 0;
	cache->next_free_soo.x = 0;
	return true;
}

/**
 * add_slab - Add one more slab to @cache
 * @m: where the memory allocated from
 * 
 * @return: true on success, false on failure
 * 
 * Note: caller should make sure @cache don't have free objects
 */
static bool add_slab(struct kv_cache *cache, struct memory *m)
{
	assert(cache->free_objects == 0);
	void *slab = memory_malloc(m, cache->slab_page);
	if (slab == NULL)
		return false;

	struct slab_obj *curr, *temp;
	slab_obj_for_each(cache, slab, curr, temp) {
		free_obj_init(curr, cache->next_free_soo);
		cache->next_free_soo = soo_make(slab, curr);
	}
	cache->free_objects = cache->slab_objects;
	return true;
}

static struct slab_obj_offset __pop_free_soo(struct kv_cache *cache)
{
	assert(cache->next_free_soo.x != 0);
	struct slab_obj_offset soo = cache->next_free_soo;
	struct slab_obj *obj = SOO_OBJ(soo);
	cache->next_free_soo = obj->next_free;
	return soo;
}

/**
 * mem_cache_malloc - Allocate an object from @cache
 * 
 * @return: the allocated object on success, or 0 on failure
 */
static struct slab_obj_offset kv_cache_malloc(
				struct kv_cache *cache, struct memory *m)
{
	if (cache->next_free_soo.x == 0 && !add_slab(cache, m))
		return (struct slab_obj_offset) { 0 };

	cache->free_objects--;
	return __pop_free_soo(cache);
}

static void migrate(void *obj_from, struct slab_obj_offset soo_to, uint16_t size)
{
	void *obj_to = SOO_OBJ(soo_to);
	memcpy(obj_to, obj_from, size);

	if ((void *)(*(unsigned long *)obj_from & ~7) != obj_from) {
		struct concat_val *val = obj_to;
		assert(SOO_OBJ(*(val->soo_ptr)) == obj_from);
		*(val->soo_ptr) = soo_to;
		return;
	}

	struct kv *from = obj_from;
	struct kv *to = obj_to;
	to->soo = soo_to;

	if (kv_enabled(from)) {
		list_fix(&to->lru);
		hlist_node_fix(&to->hash_node);
	} else {
		list_head_init(&to->lru);
	}

	if (!hlist_empty(&to->borrower_list)) {
		struct hlist_node *first = to->borrower_list.first;
		first->pprev = &to->borrower_list.first;

		struct hlist_node *curr;
		hlist_for_each(curr, &to->borrower_list) {
			struct kv_borrower *borrower;
			borrower = container_of(curr, struct kv_borrower, kv_ref_node);
			borrower->kv = to;
		}
	}
}

static void __clear_slab(struct kv_cache *cache, void *slab)
{
	struct slab_obj *curr, *temp;
	slab_obj_for_each(cache, slab, curr, temp) {
		if (!is_free_obj(curr)) {
			struct slab_obj_offset soo = __pop_free_soo(cache);
			while (soo_slab(soo) == slab)
				soo = __pop_free_soo(cache);
			migrate(curr, soo, cache->obj_size);
		}
	}
}

static struct slab_obj_offset free_soo_next(struct slab_obj_offset soo)
{
	assert(is_free_obj(SOO_OBJ(soo)));
	struct slab_obj *obj = SOO_OBJ(soo);
	return obj->next_free;
}

static void free_soo_set_next(struct slab_obj_offset soo,
			      struct slab_obj_offset next)
{
	struct slab_obj *obj = SOO_OBJ(soo);
	assert(is_free_obj(obj));
	obj->next_free = next;
}

static void __clean_free_list(struct kv_cache *cache, void *rm_slab)
{
	while (soo_slab(cache->next_free_soo) == rm_slab)
		__pop_free_soo(cache);

	struct slab_obj_offset soo = cache->next_free_soo;
	while (soo.x != 0) {
		struct slab_obj_offset next = free_soo_next(soo);
		while (next.x != 0 && soo_slab(next) == rm_slab)
			next = free_soo_next(next);

		free_soo_set_next(soo, next);
		soo = next;
	}
}

/**
 * reclaim_slab - Reclaim one slab from @cache
 */
static void reclaim_slab(struct kv_cache *cache, struct memory *m)
{
	void *rm_slab = soo_slab(__pop_free_soo(cache));
	__clear_slab(cache, rm_slab);
	__clean_free_list(cache, rm_slab);

	memory_free(m, rm_slab, cache->slab_page);
	assert(cache->free_objects == (cache->slab_objects << 1));
	cache->free_objects = cache->slab_objects;
}

/**
 * kv_cache_malloc_kv - Allocate a (struct kv) from @cache
 * 
 * @return: the allocated kv, or NULL on failure
 */
struct kv *kv_cache_malloc_kv(struct kv_cache *cache, struct memory *m)
{
	struct slab_obj_offset soo = kv_cache_malloc(cache, m);
	if (soo.x == 0)
		return NULL;

	struct kv *kv = SOO_OBJ(soo);
	kv->soo = soo;
	return kv;
}

/**
 * kv_cache_malloc_concat_val - Allocate a (struct concat_val) from @cache and
 * reference it to @soo_ptr
 * 
 * @return: true on success, false on failure
 */
bool kv_cache_malloc_concat_val(
struct kv_cache *cache, struct memory *m, struct slab_obj_offset *soo_ptr)
{
	struct slab_obj_offset soo = kv_cache_malloc(cache, m);
	if (soo.x == 0)
		return false;

	struct concat_val *concat_val = SOO_OBJ(soo);
	concat_val->soo_ptr = soo_ptr;
	*soo_ptr = soo;
	return true;
}

/**
 * kv_cache_free - Deallocates the space related to @soo
 */
void kv_cache_free(
	struct kv_cache *cache, struct slab_obj_offset soo, struct memory *m)
{
	free_obj_init(SOO_OBJ(soo), cache->next_free_soo);
	cache->next_free_soo = soo;
	cache->free_objects++;
	if (cache->free_objects >= (cache->slab_objects << 1))
		reclaim_slab(cache, m);
}
