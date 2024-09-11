// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include "page_cache.h"

/**
 * page_cache_init - Reserve memory for @cache
 * @page: number of pages @cache requires
 * @m: where memory steal from
 * 
 * @return: true on success, false on failure
 */
bool page_cache_init(struct page_cache *cache, uint64_t page, struct memory *m)
{
	if (m->free_pages < page)
		return false;

	memory_remove(m, page);
	cache->reserved_page = page;
	cache->used_page = 0;
	list_head_init(&cache->lru);
	return true;
}

/**
 * page_cache_is_malloc_from_reserved - Check if is allocate from reserved page
 */
bool page_cache_is_malloc_from_reserved(struct page_cache *cache, uint64_t page)
{
	return page + cache->used_page <= cache->reserved_page;
}

/**
 * page_cache_malloc - Allocate an object from @cache
 * @page: size of the object (in pages)
 * 
 * @return: pointer to the allocated space, or NULL on failure
 */
void *page_cache_malloc(struct page_cache *cache, uint64_t page, struct memory *m)
{
	if (cache->reserved_page > cache->used_page)
		memory_add(m, cache->reserved_page - cache->used_page);

	void *ptr = memory_malloc(m, page);
	if (ptr)
		cache->used_page += page;

	if (cache->reserved_page > cache->used_page)
		memory_remove(m, cache->reserved_page - cache->used_page);

	return ptr;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

/**
 * page_cache_free - Deallocates the space related to @obj
 * @page: size of @obj (in pages)
 */
void page_cache_free(struct page_cache *cache, void *obj, uint64_t page, struct memory *m)
{
	assert(cache->used_page >= page);
	cache->used_page -= page;
	memory_free(m, obj, page);
	if (cache->reserved_page > cache->used_page)
		memory_remove(m, min(page, cache->reserved_page - cache->used_page));
}
