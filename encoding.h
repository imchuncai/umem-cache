// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_ENCODING_H
#define __UMEM_CACHE_ENCODING_H

#include <stdint.h>

static inline uint64_t ntohll(uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return __builtin_bswap64(v);
#else
	return v;
#endif
}

static inline uint64_t htonll(uint64_t v) {
	return ntohll(v);
}

#endif
