// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONFIG_H
#define __UMEM_CACHE_CONFIG_H

#include <stdint.h>
#include <assert.h>

/***************************** CONFIGURABLE BEGIN *****************************/

/* note: server will listen on address in6addr_any */
#define CONFIG_SERVER_PORT 		47474
#define CONFIG_THREAD_NR		4
#define CONFIG_MAX_CONN			512
/* maximum space allocated for kv (in pages) */
#define CONFIG_MEM_LIMIT		((uint64_t)100 << 20 >> PAGE_SHIFT)
/* tcp read or write timeout in milliseconds (inaccurate) */
/* note: we will close the connection if tcp read or write timeout */
#define CONFIG_TCP_TIMEOUT 		((unsigned int)3000)

static_assert(CONFIG_THREAD_NR > 0 && CONFIG_THREAD_NR <= UINT32_MAX);
static_assert(CONFIG_MAX_CONN > 0 && CONFIG_MAX_CONN <= UINT32_MAX);
static_assert(CONFIG_TCP_TIMEOUT > 0);

/***************************** CONFIGURABLE END *******************************/

/* (in bytes) */
#define CONFIG_KEY_SIZE_LIMIT	UINT8_MAX
#define CONFIG_MAX_CONN_PER_THREAD	(CONFIG_MAX_CONN / CONFIG_THREAD_NR)
/* value too large is meaningless */
#define CONFIG_VAL_SIZE_LIMIT						       \
		((CONFIG_MEM_LIMIT << PAGE_SHIFT) / CONFIG_THREAD_NR / 16)

#define PAGE_SHIFT	12
#define PAGE_MASK	((1 << PAGE_SHIFT) - 1)

#endif
