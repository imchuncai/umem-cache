// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include "mem_cache.h"

/**
 * add_partial - Add an new slab to @cache
 * @m: where the memory from
 * 
 * @return: true on success, false on failure
 */
static bool add_partial(struct mem_cache *cache, struct memory *m)
{
	struct slab *slab = slab_new(m, cache->oo, cache->obj_size);
	if (slab == NULL)
		return false;

	cache->objects += OO_OBJECTS(cache->oo);
	hlist_add(&cache->partial_list, &slab->partial_node);
	return true;
}

/**
 * mem_cache_init - Allocate space for @cache and initialize
 * @obj_size: minimum size of object that @cache allocates
 * @m: where the memory from
 * 
 * @return: true on success, false on failure
 */
bool mem_cache_init(struct mem_cache *cache, uint32_t obj_size, struct memory *m)
{
	obj_size = ALIGN(obj_size, SLAB_OBJ_ALIGN);
	if (obj_size > SLAB_OBJ_SIZE_MAX)
		return false;

	cache->oo = slab_calculate_oo(obj_size);
	cache->obj_size = obj_size;
	cache->objects = 0;
	hlist_head_init(&cache->partial_list);
	list_head_init(&cache->lru);

	while (cache->objects < CONFIG_CACHE_MIN_OBJ_NR) {
		if (!add_partial(cache, m))
			return false;
	}
	return true;
}

/**
 * mem_cache_malloc - Allocate an object from @cache
 * 
 * @return: the allocated object on success, or 0 on failure
 */
struct slab_obj_offset mem_cache_malloc(struct mem_cache *cache, struct memory *m)
{
	if (hlist_empty(&cache->partial_list) && !add_partial(cache, m)) {
		struct slab_obj_offset soo = {0};
		return soo;
	}

	struct slab *slab;
	slab = hlist_first_node(&cache->partial_list, struct slab, partial_node);
	return slab_malloc(slab, cache->obj_size);
}

/**
 * mem_cache_free - Deallocates the space related to @soo
 */
void mem_cache_free(
	struct mem_cache *cache, struct slab_obj_offset soo, struct memory *m,
	void (migrate)(struct slab_obj_offset from,
		       struct slab_obj_offset to, uint32_t size))
{
	struct slab *slab = soo_slab(soo);

	if (hlist_empty(&cache->partial_list)) {
		hlist_add(&cache->partial_list, &slab->partial_node);
		slab_free(soo, cache->obj_size, migrate);
		return;
	}

	struct slab *first;
	first = hlist_first_node(&cache->partial_list, struct slab, partial_node);
	if (slab == first) {
		slab_free(soo, cache->obj_size, migrate);
	} else if (first->free_offset == 0) {
		slab_free(soo, cache->obj_size, migrate);
		hlist_add(&cache->partial_list, &slab->partial_node);
		return;
	} else {
		slab_migrate_tail(first, soo, cache->obj_size, migrate);
	}

	if (first->free_offset == 0 &&
	    cache->partial_list.first->next != NULL &&
	    cache->objects - OO_OBJECTS(cache->oo) >= CONFIG_CACHE_MIN_OBJ_NR) {
		cache->objects -= OO_OBJECTS(cache->oo);
		free_slab(first, cache->oo, m);
	}
}
