// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_BIT_OPS_H
#define __UMEM_CACHE_BIT_OPS_H

#include <stdint.h>

/**
 * fls64 - Find last set bit of @x
 * @x: uint64_t
 * 
 * Note: caller should make sure (x > 0)
 */
#define fls64(x)	((int)sizeof(uint64_t) * 8 - 1 - __builtin_clzl(x))

/**
 * fls - Find last set bit of @x
 * @x: uint32_t
 * 
 * Note: caller should make sure (x > 0)
 */
#define fls(x)		((int)sizeof(uint32_t) * 8 - 1 - __builtin_clz(x))

#endif
