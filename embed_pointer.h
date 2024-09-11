// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_EMBED_POINTER_H
#define __UMEM_CACHE_EMBED_POINTER_H

#include <assert.h>

static inline void embed_pointer(void *object, void *fp)
{
	assert(((unsigned long)object) % 8 == 0);
	*(unsigned long *)object = (unsigned long)fp;
}

static inline void *embed_pointer_get(void *object)
{
	return (void *)(*(unsigned long *)object);
}

#endif
