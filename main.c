// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "thread.h"
#include "conn.h"
#include "time.h"
#include "encoding.h"

static uint32_t server_version = 0;
static struct thread threads[CONFIG_THREAD_NR];

/**
 * must - Check x and abort() on false
 */
static void must(bool x)
{
	if (!x)
		abort();
}

static void must_meet_requirements()
{
	static_assert(sizeof(void *) == 8);
	must(sysconf(_SC_PAGESIZE) == (1 << PAGE_SHIFT));
	must(check_timeNow());
}

/**
 * must_init - Initialize or abort() on failure
 */
static void must_init()
{
	must_meet_requirements();

	struct memory m;
	memory_init(&m, CONFIG_MEM_LIMIT);

	for (uint32_t i = 0; i < CONFIG_THREAD_NR; i++)
		must(thread_init(&threads[i], &m));

	printf("memory: %luk\treserved: %luk\n",
		CONFIG_MEM_LIMIT << PAGE_SHIFT >> 10,
		(CONFIG_MEM_LIMIT - m.free_pages) << PAGE_SHIFT >> 10);

	/* maybe some pages are wasted */
	for (uint32_t i = 0; i < CONFIG_THREAD_NR; i++)
		must(thread_run(&threads[i], m.free_pages / CONFIG_THREAD_NR));
}

/**
 * must_setsockopt - Set options on sockets or abort() on failure
 */
static void must_setsockopt(
int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
	int ret = setsockopt(sockfd, level, optname, optval, optlen);
	must(ret != -1);
}

/**
 * must_bind - Bind a name to @sockfd or abort() on failure
 */
static void must_bind(int sockfd, const struct sockaddr * addr, socklen_t len)
{
	int ret = bind(sockfd, addr, len);
	must(ret != -1);
}

/**
 * must_listen - Create socket and listen for connections on it or abort()
 * on failure
 */
static int must_listen()
{
	int sockfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	must(sockfd != -1);

	int opt = 1;
	must_setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
	must_setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	must_setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
	must_setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

	struct linger ling = {0, 0};
	must_setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

	unsigned int timeout = CONFIG_TCP_TIMEOUT;
	must_setsockopt(sockfd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout));

	struct sockaddr_storage _server_addr;
	struct sockaddr_in6 *server_addr = (struct sockaddr_in6 *)&_server_addr;
	server_addr->sin6_family = AF_INET6;
	server_addr->sin6_port = htons(CONFIG_SERVER_PORT);
	server_addr->sin6_flowinfo = 0;
	server_addr->sin6_addr = in6addr_any;
	server_addr->sin6_scope_id = 0;
	must_bind(sockfd, (struct sockaddr *)server_addr, sizeof(*server_addr));

	int ret = listen(sockfd, CONFIG_MAX_CONN);
	must(ret != -1);
	return sockfd;
}

/**
 * accept_new_conn - Accept new connections from @sockfd
 */
static void accept_new_conn(int sockfd)
{
	while (true) {
		int fd = accept4(sockfd, NULL, NULL, SOCK_NONBLOCK);
		if (fd != -1)
			conn_accept(fd);
		else if (errno == EWOULDBLOCK)
			break;
	}
}

/**
 * __blocked_or_freed - Check if @conn is blocked or **freed**
 * @n: the return value of the read() or write() system call
 */
static bool __blocked_or_freed(struct conn *conn, ssize_t n)
{
	if (n > 0)
		return false;

	if (n == 0 || errno != EWOULDBLOCK)
		conn_free_before_dispatched(conn, E_CONNECT_KILL);

	return true;
}

/**
 * version_upgrade - Close all connections and update version to @version
 */
static void version_upgrade(uint32_t version)
{
	int fd[CONFIG_THREAD_NR];
	for (int i = 0; i < CONFIG_THREAD_NR; i++) {
		fd[i] = eventfd(0, 0);
		struct epoll_event event;
		event.events = EPOLLOUT | EPOLLET;
		event.data.u64 = ((uint64_t)fd[i] << 32) | 1;
		int ret = epoll_ctl(threads[i].epfd, EPOLL_CTL_ADD, fd[i], &event);
		must(ret == 0);
	}

	uint64_t u;
	for (int i = 0; i < CONFIG_THREAD_NR; i++) {
		ssize_t ret __attribute__((unused)) = read(fd[i], &u, sizeof(u));
		assert(ret == sizeof(u));

		int ret2 __attribute__((unused)) = close(fd[i]);
		assert(ret2 == 0);
	}

	server_version = version;
}

/**
 * read_thread_info - Read thread info and dispatch conn
 */
static void read_thread_info(struct epoll_event *event)
{
	struct conn *conn = event->data.ptr;

	if (event->events ^ EPOLLIN) {
		conn_free_before_dispatched(conn, E_CONNECT_KILL);
		return;
	}

	while (conn->unread > 0) {
		ssize_t readed = CONN_THREAD_INFO_SIZE - conn->unread;
		ssize_t n = read(conn->sockfd, conn->buffer + readed, conn->unread);
		if (__blocked_or_freed(conn, n))
			return;
		conn->unread -= n;
	}

	uint32_t thread_id = big_endian_uint32(conn->buffer);
	if (thread_id >= CONFIG_THREAD_NR) {
		conn_free_before_dispatched(conn, E_CONNECT_KILL);
		return;
	}

	uint32_t version = big_endian_uint32(&conn->buffer[4]);
	if (version < server_version) {
		conn_free_before_dispatched(conn, E_CONNECT_OUTDATED);
		return;
	}

	if (version > server_version)
		version_upgrade(version);

	struct thread *t = &threads[thread_id];
	if (thread_add_conn(t, conn))
		conn_dispatched(conn);
	else
		conn_free_before_dispatched(conn, E_CONNECT_TOO_MANY);
}

#define MAX_EVENTS 64

int main()
{
	must_init();
	int sockfd = must_listen();
	int epfd = epoll_create1(0);
	must(epfd != -1);
	conn_init(epfd);

	struct epoll_event event;
	event.data.ptr = NULL;
	event.events = EPOLLIN | EPOLLET;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event);
	must(ret == 0);

	printf("started :)\n");
	struct epoll_event events[MAX_EVENTS];
	while (true) {
		int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
		assert(n > 0);
		for (int i = 0; i < n; i++) {
			if (events[i].data.ptr == NULL)
				accept_new_conn(sockfd);
			else
				read_thread_info(&events[i]);
		}
	}
}
