// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_TIME_H
#define __UMEM_CACHE_TIME_H

#include <stdint.h>
#include <time.h>
#include <assert.h>

/**
 * timenow - Get current unix time
 * 
 * @return: seconds since the Epoch(1970-01-01 00:00 UTC)
 */
static inline uint64_t timenow()
{
	struct timespec tp;
	int ret __attribute__((unused));
	ret = clock_gettime(CLOCK_REALTIME_COARSE, &tp);
	assert(ret == 0);
	return tp.tv_sec;
}

/**
 * check_timeNow - Check if clock_gettime() is available
 */
static inline bool check_timeNow()
{
	struct timespec tp;
	int ret = clock_gettime(CLOCK_REALTIME_COARSE, &tp);
	return ret == 0;
}

#endif
