// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_FIXED_MEM_CACHE_H
#define __UMEM_CACHE_FIXED_MEM_CACHE_H

struct fixed_mem_cache {
	void *next_free;
};

void fixed_mem_cache_init(struct fixed_mem_cache *cache, void *ptr, int size, int n);
void *fixed_mem_cache_malloc(struct fixed_mem_cache *cache);
void fixed_mem_cache_free(struct fixed_mem_cache *cache, void *ptr);

#endif
