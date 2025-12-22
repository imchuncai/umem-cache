// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <unistd.h>
#include <netinet/tcp.h>
#include "socket.h"
#include "config.h"
#include "epoll.h"

int listen_port(int port, int epfd, uint64_t event_u64)
{
	int sockfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (sockfd == -1)
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

	if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt))  ||
	    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))   ||
	    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt))   ||
	    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling))    ||
	    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt))   ||
	    setsockopt(sockfd, IPPROTO_TCP, TCP_USER_TIMEOUT, &t, sizeof(t))  ||
	    bind(sockfd, (struct sockaddr *)addr, sizeof(*addr))	      ||
	    listen(sockfd, CONFIG_MAX_CONN)				      ||
	    !epoll_add_in(epfd, sockfd, event_u64)) {
		close(sockfd);
		return -1;
	}
	return sockfd;
}

int accept2(int fd, struct in6_addr *peer)
{
	struct sockaddr_storage __addr;
	struct sockaddr *sockaddr = (struct sockaddr *)&__addr;
	socklen_t len = sizeof(__addr);
	int sockfd = accept4(fd, sockaddr, &len, SOCK_NONBLOCK);
	if (sockfd != -1) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&__addr;
		*peer = addr->sin6_addr;
	}
	return sockfd;
}
