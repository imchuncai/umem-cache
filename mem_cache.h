// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_MEM_CACHE_H
#define __UMEM_CACHE_MEM_CACHE_H

#include "slab.h"

/**
 * mem_cache - Manage memory for (struct kv) and small value
 * @oo: information of the underlay slab
 * @obj_size: the size of the allocated object (in bytes)
 * @partial_list: the list of non-full slabs
 * @lru: lru of allocated objects
 * 
 * Note: memory allocated from (struct mem_cache) always 8 bytes aligned
 * Note: at least @CONFIG_CACHE_MIN_OBJ_NR objects can be allocated
 */
struct mem_cache {
	struct objects_order oo;
	uint32_t obj_size;
	uint64_t objects;
	struct hlist_head partial_list;
	struct list_head lru;
};

static_assert(sizeof(struct mem_cache) == 5 * 8);

bool mem_cache_init(struct mem_cache *cache, uint32_t obj_size, struct memory *m);
struct slab_obj_offset mem_cache_malloc(struct mem_cache *cache, struct memory *m);
void mem_cache_free(
	struct mem_cache *cache, struct slab_obj_offset soo, struct memory *m,
	void (migrate)(struct slab_obj_offset from,
		       struct slab_obj_offset to, uint32_t size));

#endif
