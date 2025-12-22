// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_DEBUG_H
#define __UMEM_CACHE_DEBUG_H

#ifdef DEBUG
#include <stdio.h>
#include <time.h>
#define debug_printf(...) ({						       \
	time_t timer = time(NULL);					       \
									       \
	char buffer[26];						       \
	strftime(buffer, 26, "%F %T", localtime(&timer));		       \
	printf("%s ", buffer);						       \
	printf(__VA_ARGS__);						       \
})
#else
#define debug_printf(...) ({})
#endif

#endif
