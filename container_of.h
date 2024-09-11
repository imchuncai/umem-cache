// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __EMBED_CONTAINER_OF_H
#define __EMBED_CONTAINER_OF_H

#include <stddef.h>

#define __same_type(a, b) _Generic((a), typeof(b): true, default: false)

/**
 * container_of - get the address of the container which has a member @member
 * with address @ptr
 * @type: the type of the container
 * @member: the name of the member
 */
#define container_of(ptr, type, member) ({				       \
	static_assert(__same_type(*(ptr), ((type *)0)->member),		       \
			"container_of(): type mismatch");		       \
	((type *)((char *)(ptr) - offsetof(type, member))); })

#endif
