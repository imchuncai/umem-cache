// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_MEMORY_H
#define __UMEM_CACHE_MEMORY_H

#include <stdint.h>

/**
 * memory - Memory manager
 * @free_pages: the number of free pages
 */
struct memory {
	uint64_t free_pages;
};

void memory_init(struct memory *m, uint64_t page);
void *memory_malloc(struct memory *m, uint64_t page);
void memory_free(struct memory *m, void *ptr, uint64_t page);

#endif
