// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_EPOLL_H
#define __UMEM_CACHE_EPOLL_H

#include <sys/epoll.h>
#include <assert.h>
#include <stddef.h>

static inline bool __epoll_add(int epfd, int fd, uint64_t u64, uint32_t events)
{
	struct epoll_event event;
	event.data.u64 = u64;
	event.events = events;

	return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == 0;
}

static inline bool epoll_add(int epfd, int fd, uint64_t u64)
{
	return __epoll_add(epfd, fd, u64, EPOLLIN | EPOLLOUT | EPOLLET);
}

static inline bool epoll_add_in(int epfd, int fd, uint64_t u64)
{
	return __epoll_add(epfd, fd, u64, EPOLLIN | EPOLLET);
}

static inline bool epoll_add_out(int epfd, int fd, uint64_t u64)
{
	return __epoll_add(epfd, fd, u64, EPOLLOUT | EPOLLET);
}

static inline void epoll_del(int epfd, int fd)
{
	int ret __attribute__((unused));
	ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
	assert(ret == 0);
}

#endif
