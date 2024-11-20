// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#include "memory.h"
#include "config.h"

/**
 * memory_init - Initialize @m with @page pages
 */
void memory_init(struct memory *m, uint64_t page)
{
	m->free_pages = page;
}

/**
 * sys_malloc - Allocate page aligned space from system
 * @page: number of pages required
 * 
 * @return: pointer to the allocated space, or NULL on failure
 */
static void *sys_malloc(uint64_t page)
{
	size_t len = page << PAGE_SHIFT;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	/* manual says ptr will not be NULL, if you not insist. */
	void *ptr = mmap(NULL, len, prot, flags, -1, 0);
	if (ptr == (void *)-1)
		return NULL;
	return ptr;
}

/**
 * sys_free - Deallocates the space related to @ptr back to the system
 * @page: size of @ptr (in pages)
 */
static void sys_free(void *ptr, uint64_t page)
{
	int ret __attribute__((unused)) = munmap(ptr, page << PAGE_SHIFT);
	assert(ret == 0);
}

/**
 * memory_malloc - Allocate space from @m
 * @page: size of space required (in pages)
 * 
 * @return: pointer to the allocated space, or NULL on failure
 */
void *memory_malloc(struct memory *m, uint64_t page)
{
	if (page > m->free_pages)
		return NULL;

	void *ptr = sys_malloc(page);
	if (ptr)
		m->free_pages -= page;
	return ptr;
}

/**
 * memory_free - Deallocates the space related to @ptr
 * @m: where the deallocated space goes
 * @page: size of @ptr (in pages)
 */
void memory_free(struct memory *m, void *ptr, uint64_t page)
{
	sys_free(ptr, page);
	m->free_pages += page;
}
