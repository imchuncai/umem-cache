// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_KV_CACHE_H
#define __UMEM_CACHE_KV_CACHE_H

#include "memory.h"
#include "kv.h"

#define KV_CACHE_OBJ_SIZE_MIN	(8 + 8)
#define KV_CACHE_OBJ_SIZE_MAX	SLAB_OBJ_SIZE_MAX

/**
 * kv_cache - Manage memory for (struct kv) and (struct concat_val)
 * @slab_page: the number of pages the underlay slab requires
 * @obj_size: the size of the allocated object (in bytes)
 * @slab_objects: the number of objects the underlay slab can allocate
 * @free_objects: the number of free objects
 * @next_free_soo: the information of next free object
 * 
 * Note: objects allocated from (struct kv_cache) always 8 bytes aligned
 */
struct kv_cache {
	uint16_t slab_page;
	uint16_t obj_size;
	uint16_t slab_objects;
	uint16_t free_objects;
	struct slab_obj_offset next_free_soo;
};

/* make sure uint16_t will not overflow */
static_assert(UINT16_MAX >= (1 << SLAB_ORDER_MAX));
static_assert(UINT16_MAX >= SLAB_OBJ_SIZE_MAX);
static_assert(UINT16_MAX >= 2 * SLAB_OBJ_MAX);

bool kv_cache_init(struct kv_cache *cache, uint16_t obj_size);
struct kv *kv_cache_malloc_kv(struct kv_cache *cache, struct memory *m);
bool kv_cache_malloc_concat_val(
struct kv_cache *cache, struct memory *m, struct slab_obj_offset *soo_ptr);
void kv_cache_free(
	struct kv_cache *cache, struct slab_obj_offset soo, struct memory *m);

#endif
