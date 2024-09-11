// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_SLAB_H
#define __UMEM_CACHE_SLAB_H

#include "memory.h"
#include "list.h"
#include "align.h"
#include "config.h"

/**
 * objects_order - Information of a slab, contains the number of objects
 * it can allocates and the order of pages it requires.
 */
struct objects_order {
	uint32_t x;
};

#define __OO_ORDER_SHIFT	2
#define __OO_ORDER_MASK		((1 << __OO_ORDER_SHIFT) - 1)
#define __OO_ORDER_MAX		__OO_ORDER_MASK
#define __OO_ORDER(oo)		((oo).x & __OO_ORDER_MASK)
#define OO_OBJECTS(oo)		((oo).x >> __OO_ORDER_SHIFT)
#define __OO_MAKE(objects, order)					       \
	((struct objects_order){ ((objects) << __OO_ORDER_SHIFT) | (order) })

/* make sure objects will not overflow */
static_assert(32 - __OO_ORDER_SHIFT >= __OO_ORDER_SHIFT + PAGE_SHIFT);

/**
 * slab_obj_offset - Information of objects allocated from slab, contains
 * object address and page offset to slab header.
 */
struct slab_obj_offset {
	unsigned long x;
};

#define __SOO_OFFSET_SHIFT	3
#define __SOO_OFFSET_MASK	((1 << __SOO_OFFSET_SHIFT) - 1)
#define SOO_OBJ(soo)		((void *)((soo).x & ~__SOO_OFFSET_MASK))
#define SOO_OFFSET(soo)		((soo).x & __SOO_OFFSET_MASK)
#define SLAB_OBJ_ALIGN		(1 << __SOO_OFFSET_SHIFT)

static_assert(__SOO_OFFSET_MASK >= (1 << __OO_ORDER_MAX) - 1);

/**
 * slab - Manage memory for small objects
 * @free_offset: offset from @data to next free object
 * @max_offset: the maximum offset @free_offset can reach
 * @partial_node: use to resides in (struct mem_cache->partial_list)
 * 
 * Note: slab is page aligned
 */
struct slab {
	uint32_t free_offset;
	uint32_t max_offset;
	struct hlist_node partial_node;
	char data[];
};

#define __SLAB_DATA_SIZE(order)						       \
		((1 << (order) << PAGE_SHIFT) - offsetof(struct slab, data))

static_assert(offsetof(struct slab, data) % SLAB_OBJ_ALIGN == 0);
/* make sure slab->free_offset, slab->max_offset will not overflow */
static_assert(UINT32_MAX >= __SLAB_DATA_SIZE(__OO_ORDER_MAX) - 1);

#define SLAB_OBJ_SIZE_MAX	ALIGN_DOWN(				       \
	(__SLAB_DATA_SIZE(__OO_ORDER_MAX) / ((1 << __OO_ORDER_MAX) + 1)),      \
	SLAB_OBJ_ALIGN)

struct slab *soo_slab(struct slab_obj_offset soo);

struct objects_order slab_calculate_oo(uint32_t obj_size);
struct slab *slab_new(struct memory *m, struct objects_order oo, uint32_t obj_size);
struct slab_obj_offset slab_malloc(struct slab *slab, uint32_t obj_size);
void slab_free(struct slab_obj_offset soo, uint32_t obj_size,
	void (migrate)(struct slab_obj_offset from,
		       struct slab_obj_offset to, uint32_t size));
void free_slab(struct slab *slab, struct objects_order oo, struct memory *m);
void slab_migrate_tail(
	struct slab *slab, struct slab_obj_offset soo, uint32_t obj_size,
	void (migrate)(struct slab_obj_offset from,
		       struct slab_obj_offset to, uint32_t size));

#endif
