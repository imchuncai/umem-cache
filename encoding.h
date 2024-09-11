// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_ENCODING_H
#define __UMEM_CACHE_ENCODING_H

#include <stdint.h>

static inline uint32_t big_endian_uint32(unsigned char *b)
{
	return (uint32_t)b[3] | (uint32_t)b[2] << 8 |
		(uint32_t)b[1] << 16 | (uint32_t)b[0] << 24;
}

static inline void big_endian_put_uint32(unsigned char *b, uint32_t v)
{
	b[0] = v >> 24;
	b[1] = v >> 16;
	b[2] = v >> 8;
	b[3] = v;
}

static inline uint64_t big_endian_uint64(unsigned char *b)
{
	return (uint64_t)b[7] | (uint64_t)b[6] << 8 |
		(uint64_t)b[5] << 16 | (uint64_t)b[4] << 24 |
		(uint64_t)b[3] << 32 | (uint64_t)b[2] << 40 |
		(uint64_t)b[1] << 48 | (uint64_t)b[0] << 56;
}

static inline void big_endian_put_uint64(unsigned char *b, uint64_t v)
{
	b[0] = v >> 56;
	b[1] = v >> 48;
	b[2] = v >> 40;
	b[3] = v >> 32;
	b[4] = v >> 24;
	b[5] = v >> 16;
	b[6] = v >> 8;
	b[7] = v;
}

#endif
