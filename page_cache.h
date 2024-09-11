// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_PAGE_CACHE_H
#define __UMEM_CACHE_PAGE_CACHE_H

#include "memory.h"
#include "list.h"

/**
 * page_cache - Manage memory for page aligned objects
 */
struct page_cache {
	uint64_t reserved_page;
	uint64_t used_page;
	struct list_head lru;
};

bool page_cache_init(struct page_cache *cache, uint64_t page, struct memory *m);
bool page_cache_is_malloc_from_reserved(struct page_cache *cache, uint64_t page);
void *page_cache_malloc(struct page_cache *cache, uint64_t page, struct memory *m);
void page_cache_free(struct page_cache *cache, void *obj, uint64_t page, struct memory *m);

#endif
