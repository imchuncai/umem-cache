// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025-2026, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include "socket.h"
#include "config.h"
#include "epoll.h"

/**
 * listen_port - Listen on port @port and add socket fd to epfd
 * @event_u64: epoll event
 * 
 * @return: socket fd on success, or -1 on failure
 */
int listen_port(int port, int epfd, uint64_t event_u64)
{
	int fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (fd == -1)
		return -1;

	struct sockaddr_storage __addr;
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&__addr;
	addr->sin6_family = AF_INET6;
	addr->sin6_port = htons(port);
	addr->sin6_flowinfo = 0;
	addr->sin6_addr = in6addr_any;
	addr->sin6_scope_id = 0;

	int opt = 1;
	struct linger ling = {0, 0};
	unsigned int t = CONFIG_TCP_TIMEOUT;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) ||
	    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))	 ||
	    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt))	 ||
	    setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling))	 ||
	    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt))	 ||
	    setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &t, sizeof(t)) ||
	    bind(fd, (struct sockaddr *)addr, sizeof(*addr))		 ||
	    listen(fd, CONFIG_MAX_CONN)					 ||
	    !epoll_add_in(epfd, fd, event_u64)) {
		close(fd);
		return -1;
	}
	return fd;
}

int accept2(int sockfd, struct in6_addr *peer)
{
	struct sockaddr_storage __addr;
	struct sockaddr *sockaddr = (struct sockaddr *)&__addr;
	socklen_t len = sizeof(__addr);
	int fd = accept4(sockfd, sockaddr, &len, SOCK_NONBLOCK);
	if (fd != -1) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&__addr;
		*peer = addr->sin6_addr;
	}
	return fd;
}
