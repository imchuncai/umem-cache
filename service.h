// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_SERVICE_H
#define __UMEM_CACHE_SERVICE_H

#include <stdlib.h>

void must_service_run(int port);

/**
 * must - Check x and abort() on false
 */
static inline void must(bool x)
{
	if (!x)
		abort();
}

#endif
