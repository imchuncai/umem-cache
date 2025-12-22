// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONTAINER_OF_H
#define __UMEM_CACHE_CONTAINER_OF_H

#include <stddef.h>
#include <assert.h>

#define __same_type(a, b) _Generic((a), typeof(b): true, default: false)

/**
 * container_of - get the address of the container which has a member @member
 * with address @ptr
 * @type: the struct type of the container
 * @member: the name of the member within the struct
 */
#define container_of(ptr, type, member) ({				       \
	static_assert(__same_type(*(ptr), ((type *)0)->member),		       \
			"container_of(): type mismatch");		       \
	((type *)((char *)(ptr) - offsetof(type, member))); })

#endif
