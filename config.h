// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONFIG_H
#define __UMEM_CACHE_CONFIG_H

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

/***************************** CONFIGURABLE BEGIN *****************************/

#ifndef CONFIG_THREAD_NR
#define CONFIG_THREAD_NR 4
#endif

/* only limit connections for threads */
/* Note: user may use system's open fd limitation for further protection */
#ifndef CONFIG_MAX_CONN
#define CONFIG_MAX_CONN 512
#endif

/* maximum available memory space for kv (in bytes) */
#ifndef CONFIG_MEM_LIMIT
#define CONFIG_MEM_LIMIT (100 << 20)
#endif

/* tcp read or write timeout in milliseconds (inaccurate, at least) */
/* Note: we will close the connection if tcp read or write timeout */
#ifndef CONFIG_TCP_TIMEOUT
#define CONFIG_TCP_TIMEOUT 3000
#endif

/***************************** CONFIGURABLE END *******************************/

/* (in bytes) */
#define CONFIG_KEY_SIZE_MAX	UINT8_MAX

#define PAGE_SHIFT	12
#define PAGE_MASK	((1 << PAGE_SHIFT) - 1)

static_assert(CONFIG_THREAD_NR > 0 && CONFIG_THREAD_NR <= INT32_MAX);
static_assert(CONFIG_MAX_CONN > 0 && CONFIG_MAX_CONN <= INT32_MAX);
static_assert(CONFIG_MEM_LIMIT > 0 && CONFIG_MEM_LIMIT <= INT64_MAX);
static_assert(CONFIG_TCP_TIMEOUT > 0 && CONFIG_TCP_TIMEOUT <= UINT32_MAX);

/**
 * must - Check x and abort() on false
 */
static inline void must(bool x)
{
	if (!x)
		abort();
}

#endif
