// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include "slab.h"

uint16_t slab_calculate_order(uint16_t obj_size)
{
	assert(obj_size % SLAB_OBJ_ALIGN == 0);
	assert(obj_size <= SLAB_OBJ_SIZE_MAX);

	for (unsigned int fraction = 16; ; fraction /= 2) {
		for (uint16_t order = 0; order <= SLAB_ORDER_MAX; order++) {
			unsigned int data_size = SLAB_SIZE(order);
			unsigned int rem = data_size % obj_size;
			if (rem <= data_size / fraction)
				return order;
		}
	}
}

/**
 * soo_slab - Get the slab that @soo allocated from
 */
void *soo_slab(struct slab_obj_offset soo)
{
	unsigned long page = soo.x & ~PAGE_MASK;
	unsigned long slab = page - (SOO_OFFSET(soo) << PAGE_SHIFT);
	return (void *)slab;
}

/**
 * soo_make - Make (struct slab_obj_offset)
 * 
 * Note: @slab is page aligned
 * Note: caller should make sure @obj is allocated from @slab
 */
struct slab_obj_offset soo_make(const void *slab, const void *obj)
{
	assert(((unsigned long)obj & __SOO_OFFSET_MASK) == 0);
	assert(((unsigned long)slab & PAGE_MASK) == 0);

	unsigned long page = (unsigned long)obj & ~PAGE_MASK;
	unsigned long i = (page - (unsigned long)slab) >> PAGE_SHIFT;
	struct slab_obj_offset soo = SOO_MAKE(obj, i);
	assert(soo_slab(soo) == slab);
	return soo;
}
