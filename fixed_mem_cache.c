// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <stddef.h>
#include "fixed_mem_cache.h"
#include "embed_pointer.h"

/**
 * fixed_mem_cache_init - Init fixed memory cache for allocating objects
 * @ptr: address of the object list (where the memory from)
 * @size: size of the object
 * @n: number of objects
 * 
 * Note: caller should make sure object is at least 8 bytes aligned
 */
void fixed_mem_cache_init(struct fixed_mem_cache *cache, void *ptr, int size, int n)
{
	assert(size % 8 == 0);
	cache->next_free = NULL;
	for (int i = 0; i < n; i++) {
		embed_pointer(ptr, cache->next_free);
		cache->next_free = ptr;
		ptr = (char *)ptr + size;
	}
}

/**
 * fixed_mem_cache_malloc - Allocate object from @cache
 * 
 * @return: the allocated object on success, or NULL on failure
 */
void *fixed_mem_cache_malloc(struct fixed_mem_cache *cache)
{
	if (cache->next_free == NULL)
		return NULL;

	void *obj = cache->next_free;
	cache->next_free = embed_pointer_get(obj);
	return obj;
}

/**
 * fixed_mem_cache_free - Return @obj back to @cache
 */
void fixed_mem_cache_free(struct fixed_mem_cache *cache, void *obj)
{
	embed_pointer(obj, cache->next_free);
	cache->next_free = obj;
}
