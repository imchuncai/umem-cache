// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "thread.h"
#include "conn.h"
#include "encoding.h"

static uint32_t server_version = 0;
static struct thread threads[CONFIG_THREAD_NR];

/* event fd for version upgrade */
static int notify_event_fd[CONFIG_THREAD_NR];
static int collect_event_fd;

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
}

static void must_reserve_event_fd()
{
	collect_event_fd = eventfd(0, 0);
	must(collect_event_fd != -1);
	for (int i = 0; i < CONFIG_THREAD_NR; i++) {
		notify_event_fd[i] = eventfd(0, 0);
		must(notify_event_fd[i] != -1);

		struct epoll_event event;
		event.events = EPOLLIN | EPOLLET;
		event.data.u64 = ((uint64_t)collect_event_fd << 32) | 1;
		int ret = epoll_ctl(threads[i].epfd, EPOLL_CTL_ADD,
					notify_event_fd[i], &event);
		must(ret == 0);
	}
}

/**
 * must_init - Initialize or abort() on failure
 */
static void must_init()
{
	must_meet_requirements();
#ifdef DEBUG
	kv_cache_idx_generate_print();
#endif
	for (uint32_t i = 0; i < CONFIG_THREAD_NR; i++)
		must(thread_init(&threads[i], CONFIG_MEM_LIMIT / CONFIG_THREAD_NR));

	must_reserve_event_fd();

	for (uint32_t i = 0; i < CONFIG_THREAD_NR; i++)
		must(thread_run(&threads[i]));
}

/**
 * __listen - Create socket and listen for connections on it and add it to @epfd
 * 
 * @return: the sockfd or -1 on failure
 */
static int __listen(int epfd)
{
	int sockfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (sockfd == -1)
		return -1;

	int opt = 1;
	struct linger ling = {0, 0};
	unsigned int timeout = CONFIG_TCP_TIMEOUT;

	struct sockaddr_storage __server_addr;
	struct sockaddr_in6 *server_addr = (struct sockaddr_in6 *)&__server_addr;
	server_addr->sin6_family = AF_INET6;
	server_addr->sin6_port = htons(CONFIG_SERVER_PORT);
	server_addr->sin6_flowinfo = 0;
	server_addr->sin6_addr = in6addr_any;
	server_addr->sin6_scope_id = 0;

	struct epoll_event event;
	event.data.ptr = NULL;
	event.events = EPOLLIN | EPOLLET;

	if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) == -1 ||
	    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1 ||
	    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) == -1 ||
	    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) == -1 ||
	    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1 ||
	    setsockopt(sockfd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout)) == -1 ||
	    bind(sockfd, (struct sockaddr *)server_addr, sizeof(*server_addr)) == -1 ||
	    listen(sockfd, CONFIG_MAX_CONN) == -1 ||
	    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event) != 0) {
		close(sockfd);
		return -1;
	}
	return sockfd;
}

/**
 * accept_new_conn - Accept new connections from @sockfd
 * 
 * @return: true on success, false on failure
 */
static bool accept_new_conn(int sockfd)
{
	while (true) {
		int fd = accept4(sockfd, NULL, NULL, SOCK_NONBLOCK);
		if (fd == -1)
			return errno == EWOULDBLOCK;
		else
			conn_accept(fd);
	}
}

/**
 * conn_check_io - Update @conn after an io
 * @n: the return value from read() or write()
 * 
 * @return: true on something is read or written, false otherwise
 */
static bool conn_check_io(struct conn *conn, ssize_t n)
{
	if (n > 0) {
		assert(conn->unio >= (size_t)n);
		conn->unio -= n;
		return true;
	}

	if (n == -1 && errno == EWOULDBLOCK)
		return false;

	conn_free_before_dispatched(conn, E_CONNECT_KILL);
	return false;
}

/**
 * conn_read - Read from @conn to @buffer
 * 
 * @return: true on something is read, false on nothing is read
 */
static bool conn_read(struct conn *conn, unsigned char *buffer)
{
	assert(conn->unread > 0);
	ssize_t read_n = read(conn->sockfd, buffer, conn->unread);
	return conn_check_io(conn, read_n);
}

/**
 * conn_full_read - Read from @conn to @buffer
 * 
 * @return: true on full read, false on short read
 */
static bool conn_full_read(struct conn *conn, unsigned char *buffer)
{
	return conn_read(conn, buffer) && conn->unread == 0;
}

/**
 * version_upgrade - Close all connections and update version to @version
 */
static void version_upgrade(uint32_t version)
{
	uint64_t u = 1;
	for (int i = 0; i < CONFIG_THREAD_NR; i++) {
		ssize_t ret __attribute__((unused));
		ret = write(notify_event_fd[i], &u, sizeof(u));
		assert(ret == sizeof(u));
	}

	uint32_t count = 0;
	while (count < CONFIG_THREAD_NR) {
		ssize_t ret __attribute__((unused));
		ret = read(collect_event_fd, &u, sizeof(u));
		assert(ret == sizeof(u));
		count += u;
	}
	assert(count == CONFIG_THREAD_NR);

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

	static_assert(sizeof(((struct conn *)0)->buffer) >= CONN_THREAD_INFO_SIZE);
	uint64_t readed = CONN_THREAD_INFO_SIZE - conn->unread;
	if (!conn_full_read(conn, conn->buffer + readed))
		return;

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

int main()
{
	must_init();

	int epfd = epoll_create1(0);
	must(epfd != -1);
	conn_init(epfd);

	int sockfd = __listen(epfd);
	must(sockfd != -1);

	printf("memory: %luk\t", (uint64_t)CONFIG_MEM_LIMIT << PAGE_SHIFT >> 10);
	printf("value_size_limit: %luk\n", (uint64_t)CONFIG_VAL_SIZE_LIMIT >> 10);
	printf("started :)\n");

	while (true) {
		struct epoll_event events[CONFIG_MAX_CONN];
		int n;
		if (sockfd == -1) {
			sleep(3);
			sockfd = __listen(epfd);
			n = epoll_wait(epfd, events, CONFIG_MAX_CONN, 0);
		} else {
			n = epoll_wait(epfd, events, CONFIG_MAX_CONN, -1);
		}

		for (int i = 0; i < n; i++) {
			if (events[i].data.ptr != NULL) {
				read_thread_info(&events[i]);
			} else if (!accept_new_conn(sockfd)) {
				close(sockfd);
				sockfd = -1;
			}
		}
	}
}
