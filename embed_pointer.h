// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_EMBED_POINTER_H
#define __UMEM_CACHE_EMBED_POINTER_H

#include <assert.h>

static inline void embed_ulong(void *obj, unsigned long v)
{
	assert(((unsigned long)obj) % 8 == 0);

	*(unsigned long *)obj = v;
}

static inline void embed_pointer(void *obj, const void *fp)
{
	assert(((unsigned long)obj) % 8 == 0);

	embed_ulong(obj, (unsigned long)fp);
}

static inline unsigned long embed_ulong_get(const void *obj)
{
	return *(unsigned long *)obj;
}

static inline void *embed_pointer_get(const void *obj)
{
	return (void *)embed_ulong_get(obj);
}

#endif
