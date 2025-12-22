// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_SOCKET_H
#define __UMEM_CACHE_SOCKET_H

#include <netinet/in.h>

int listen_port(int port, int epfd, uint64_t event_u64);
int accept2(int fd, struct in6_addr *peer);

#endif
