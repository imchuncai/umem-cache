// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_SLAB_H
#define __UMEM_CACHE_SLAB_H

#include "config.h"
#include "align.h"

/**
 * slab_obj_offset - Information of objects allocated from slab, contains
 * object address and page offset to slab header.
 */
struct slab_obj_offset {
	unsigned long x;
};

#define __SOO_OFFSET_SHIFT	3
#define __SOO_OFFSET_MASK	((1 << __SOO_OFFSET_SHIFT) - 1)
#define SOO_OBJ(soo)		((void *)((soo).x & ~__SOO_OFFSET_MASK))
#define SOO_OFFSET(soo)		((soo).x & __SOO_OFFSET_MASK)
#define SOO_MAKE(obj, offset)						       \
		((struct slab_obj_offset) { (unsigned long)obj | offset })

#define SLAB_OBJ_ALIGN		(1 << __SOO_OFFSET_SHIFT)
#define SLAB_ORDER_MAX		__SOO_OFFSET_SHIFT
#define SLAB_SIZE(order)	(1 << (order) << PAGE_SHIFT)

#define SLAB_OBJ_MAX		(SLAB_SIZE(SLAB_ORDER_MAX) / SLAB_OBJ_ALIGN)
#define SLAB_OBJ_SIZE_MAX	ALIGN_DOWN(				       \
(SLAB_SIZE(SLAB_ORDER_MAX) / ((1 << SLAB_ORDER_MAX) + 1)), SLAB_OBJ_ALIGN)

uint16_t slab_calculate_order(uint16_t obj_size);
void *soo_slab(struct slab_obj_offset soo);
struct slab_obj_offset soo_make(const void *slab, const void *obj);

#endif
