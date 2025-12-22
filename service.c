// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <unistd.h>
#include <errno.h>
#include "thread.h"
#include "encoding.h"
#include "socket.h"
#include "epoll.h"
#include "tls.h"

struct service_conn {
	int sockfd;
	unsigned char buffer[4];

#ifdef CONFIG_KERNEL_TLS
	struct tls_session session;
#endif
};

static struct service_conn *service_conn_malloc(
		int sockfd, struct in6_addr peer __attribute__((unused)))
{
	struct service_conn *conn = malloc(sizeof(struct service_conn));
	if (conn) {
		conn->sockfd = sockfd;
		conn->buffer[3] = 8;

	#ifdef CONFIG_KERNEL_TLS
		if (!tls_init_server(&conn->session, sockfd, peer)) {
			free(conn);
			return NULL;
		}
	#endif
	}
	return conn;
}

static void service_conn_free(struct service_conn *conn)
{
	close(conn->sockfd);
#ifdef CONFIG_KERNEL_TLS
	if (conn->session.session)
		tls_deinit(&conn->session);
#endif
	free(conn);
}

static bool service_conn_full_read_connect_in(struct service_conn *conn)
{
	int sockfd = conn->sockfd;
	unsigned char unread = conn->buffer[3];
	assert(unread > 0);
	ssize_t n = read(conn->sockfd, conn->buffer - 4 + (8 - unread), unread);
	conn->sockfd = sockfd;
	if (n > 0) {
		assert(unread >= n);
		if (unread == n) {
			return true;
		} else {
			conn->buffer[3] -= n;
			return false;
		}
	}

	if (!(n == -1 && errno == EWOULDBLOCK))
		service_conn_free(conn);

	return false;
}

#ifdef CONFIG_KERNEL_TLS
static bool handshake(struct service_conn *conn)
{
	if (conn->session.session == NULL)
		return true;

	int ret = tls_handshake(&conn->session);
	if (ret == GNUTLS_E_SUCCESS) {
		tls_deinit(&conn->session);
		conn->session.session = NULL;
		return true;
	}
	
	if (ret != GNUTLS_E_AGAIN)
		service_conn_free(conn);

	return false;
}
#endif

/**
 * read_thread_info - Read thread info and dispatch conn
 */
static void read_thread_info(struct service_conn *conn, int epfd)
{
#ifdef CONFIG_KERNEL_TLS
	if (!handshake(conn))
		return;
#endif

	if (!service_conn_full_read_connect_in(conn))
		return;

	uint32_t thread_id = ntohl(*(uint32_t *)conn->buffer);
	if (thread_id >= CONFIG_THREAD_NR) {
		service_conn_free(conn);
		return;
	}

	epoll_del(epfd, conn->sockfd);
	thread_dispatch(thread_id, conn->sockfd);
	free(conn);
}

/**
 * accept_new_conn - Accept new connections from @sockfd and add to @epfd
 * 
 * @return: true on success, false on failure
 */
static bool accept_new_conn(int sockfd, int epfd)
{
	while (true) {
		struct in6_addr sin6_addr;
		int fd = accept2(sockfd, &sin6_addr);
		if (fd == -1)
			return errno == EWOULDBLOCK;

		struct service_conn *conn = service_conn_malloc(fd, sin6_addr);
		if (conn == NULL)
			close(fd);
		else if (!epoll_add_in(epfd, fd, (uint64_t)conn))
			service_conn_free(conn);
	}
}

#define SERVER_MAX_EPOLL_EVENTS	64
struct epoll_event events[SERVER_MAX_EPOLL_EVENTS];

void must_service_run(int port)
{
	int epfd = epoll_create1(0);
	must(epfd != -1);

	int sockfd = listen_port(port, epfd, 0);
	must(sockfd != -1);

	must(threads_run());

	while (true) {
		int n;
		if (sockfd == -1) {
			sleep(3);
			sockfd = listen_port(port, epfd, 0);
			/* in case listen failed, don't wait epoll */
			n = epoll_wait(epfd, events, SERVER_MAX_EPOLL_EVENTS, 0);
		} else {
			n = epoll_wait(epfd, events, SERVER_MAX_EPOLL_EVENTS, -1);
		}

		for (int i = 0; i < n; i++) {
			if (events[i].data.ptr) {
				struct service_conn *conn = events[i].data.ptr;
				if (events[i].events ^ EPOLLIN)
					service_conn_free(conn);
				else
					read_thread_info(conn, epfd);
			} else if (!accept_new_conn(sockfd, epfd)) {
				close(sockfd);
				sockfd = -1;
			}
		}
	}
}
