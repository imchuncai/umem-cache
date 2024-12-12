// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONFIG_H
#define __UMEM_CACHE_CONFIG_H

#include <stdint.h>
#include <assert.h>

/***************************** CONFIGURABLE BEGIN *****************************/

/* note: server will listen on address in6addr_any */
#ifndef CONFIG_SERVER_PORT
#define CONFIG_SERVER_PORT 47474
#endif

#ifndef CONFIG_THREAD_NR
#define CONFIG_THREAD_NR 4
#endif

#ifndef CONFIG_MAX_CONN
#define CONFIG_MAX_CONN 512
#endif

/* maximum space allocated for kv (in pages) */
#ifndef CONFIG_MEM_LIMIT
#define CONFIG_MEM_LIMIT (100 << 20 >> PAGE_SHIFT)
#endif

/* tcp read or write timeout in milliseconds (inaccurate) */
/* note: we will close the connection if tcp read or write timeout */
#ifndef CONFIG_TCP_TIMEOUT
#define CONFIG_TCP_TIMEOUT 3000
#endif

/***************************** CONFIGURABLE END *******************************/

/* (in bytes) */
#define CONFIG_KEY_SIZE_LIMIT	UINT8_MAX
#define CONFIG_MAX_CONN_PER_THREAD	(CONFIG_MAX_CONN / CONFIG_THREAD_NR)
/* value too large is meaningless */
#define CONFIG_VAL_SIZE_LIMIT						       \
		((CONFIG_MEM_LIMIT << PAGE_SHIFT) / CONFIG_THREAD_NR / 16)

#define PAGE_SHIFT	12
#define PAGE_MASK	((1 << PAGE_SHIFT) - 1)

static_assert(CONFIG_THREAD_NR > 0 && CONFIG_THREAD_NR <= UINT16_MAX);
static_assert(CONFIG_THREAD_NR > 0 && CONFIG_THREAD_NR <= UINT32_MAX);
static_assert(CONFIG_MAX_CONN > 0 && CONFIG_MAX_CONN <= UINT32_MAX);
static_assert(CONFIG_MEM_LIMIT > 0 && CONFIG_MEM_LIMIT <= UINT64_MAX);
static_assert(CONFIG_TCP_TIMEOUT > 0 && CONFIG_TCP_TIMEOUT <= UINT32_MAX);

#endif
