// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_DEBUG_H
#define __UMEM_CACHE_DEBUG_H

#ifdef DEBUG
#include <stdio.h>
#define debug_printf(...)	({ printf(__VA_ARGS__); })
#else
#define debug_printf(...)	({})
#endif

#endif
