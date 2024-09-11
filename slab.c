// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include "slab.h"

/**
 * soo_slab - Get the slab that @soo allocated from
 */
struct slab *soo_slab(struct slab_obj_offset soo)
{
	unsigned long page = soo.x & ~PAGE_MASK;
	unsigned long slab = page - (SOO_OFFSET(soo) << PAGE_SHIFT);
	return (struct slab *)slab;
}

/**
 * slab_calculate_oo - Calculate best (struct objects_order) for slab
 * @obj_size: minimum size of object the slab allocates
 * 
 * Note: caller should make sure obj_size is SLAB_OBJ_ALIGN aligned
 * Note: caller should make sure (obj_size <= SLAB_OBJ_SIZE_MAX)
 */
struct objects_order slab_calculate_oo(uint32_t obj_size)
{
	assert(obj_size % SLAB_OBJ_ALIGN == 0);
	assert(obj_size <= SLAB_OBJ_SIZE_MAX);

	for (unsigned int fraction = 16; ; fraction /= 2) {
		for (unsigned int order = 0; order <= __OO_ORDER_MAX; order++) {
			unsigned int data_size = __SLAB_DATA_SIZE(order);
			unsigned int rem = data_size % obj_size;
			if (rem <= data_size / fraction)
				return __OO_MAKE(data_size / obj_size, order);
		}
	}
}

/**
 * soo_make - Make (struct slab_obj_offset)
 * 
 * Note: @slab is page aligned
 * Note: caller should make sure @obj is allocated from @slab
 */
static struct slab_obj_offset soo_make(void *obj, struct slab *slab)
{
	assert(((unsigned long)obj & __SOO_OFFSET_MASK) == 0);
	assert(((unsigned long)slab & PAGE_MASK) == 0);

	unsigned long page = (unsigned long)obj & ~PAGE_MASK;
	unsigned long i = (page - (unsigned long)slab) >> PAGE_SHIFT;
	struct slab_obj_offset soo = { (unsigned long)obj | i };
	assert(soo_slab(soo) == slab);
	return soo;
}

/**
 * slab_new - Allocate space for (struct slab) and initialize
 * @m: where the memory for (struct slab) from
 *
 * @return: address of (struct slab) on success, NULL on fail
 */
struct slab *slab_new(struct memory *m, struct objects_order oo, uint32_t obj_size)
{
	struct slab *slab = memory_malloc(m, 1 << __OO_ORDER(oo));
	if (slab == NULL)
		return NULL;

	slab->free_offset = 0;
	slab->max_offset = obj_size * OO_OBJECTS(oo);
	return slab;
}

/**
 * slab_malloc - Allocate object from @slab
 * 
 * Note: caller should make sure @slab has free object
 */
struct slab_obj_offset slab_malloc(struct slab *slab, uint32_t obj_size)
{
	void *obj = slab->data + slab->free_offset;
	slab->free_offset += obj_size;
	assert(slab->free_offset <= slab->max_offset);

	if (slab->free_offset == slab->max_offset)
		hlist_del(&slab->partial_node);
	return soo_make(obj, slab);
}

/**
 * slab_free - Deallocates the space related to @soo
 */
void slab_free(struct slab_obj_offset soo, uint32_t obj_size,
	void (migrate)(struct slab_obj_offset from,
		       struct slab_obj_offset to, uint32_t size))
{
	struct slab *slab = soo_slab(soo);
	void *obj = SOO_OBJ(soo);

	slab->free_offset -= obj_size;
	void *from = slab->data + slab->free_offset;
	if (from != obj)
		migrate(soo_make(from, slab), soo, obj_size);
}

/**
 * free_slab - Deallocates the space related to @slab
 * @m: where the freed memory goes
 */
void free_slab(struct slab *slab, struct objects_order oo, struct memory *m)
{
	assert(slab->free_offset == 0);

	hlist_del(&slab->partial_node);
	memory_free(m, slab, 1 << __OO_ORDER(oo));
}

/**
 * slab_migrate_tail - Migrate @slab's tail to @soo
 * 
 * Note: caller should make sure @soo is not allocated from @slab
 */
void slab_migrate_tail(
	struct slab *slab, struct slab_obj_offset soo, uint32_t obj_size,
	void (migrate)(struct slab_obj_offset from,
		       struct slab_obj_offset to, uint32_t size))
{
	assert(slab != soo_slab(soo));

	slab->free_offset -= obj_size;
	void *from = slab->data + slab->free_offset;
	migrate(soo_make(from, slab), soo, obj_size);
}
