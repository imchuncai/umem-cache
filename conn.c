// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include "conn.h"
#include "fixed_mem_cache.h"

/* main thread epoll file descriptor */
static int main_epfd;

/* manage memory for (struct conn) */
static struct conn __conn[CONFIG_MAX_CONN];
static pthread_mutex_t mu;
static struct fixed_mem_cache conn_cache;
static_assert(alignof(struct conn) % 8 == 0);

void conn_init(int __main_epfd)
{
	main_epfd = __main_epfd;
	pthread_mutex_init(&mu, NULL);
	fixed_mem_cache_init(
		&conn_cache, __conn, sizeof(struct conn), CONFIG_MAX_CONN);
}

/**
 * conn_malloc - Allocate space for conn
 * 
 * @return: the allocated conn on success, or NULL on failure
 */
static struct conn *conn_malloc()
{
	pthread_mutex_lock(&mu);
	struct conn *conn = fixed_mem_cache_malloc(&conn_cache);
	pthread_mutex_unlock(&mu);
	return conn;
}

/**
 * main_epoll_add - Add @conn to main thread epoll
 */
static bool main_epoll_add(struct conn *conn)
{
	struct epoll_event event;
	event.data.ptr = conn;
	event.events = EPOLLIN | EPOLLET;

	int ret = epoll_ctl(main_epfd, EPOLL_CTL_ADD, conn->sockfd, &event);
	return ret == 0;
}

/**
 * main_epoll_del - Delete @conn from main thread epoll
 */
static void main_epoll_del(struct conn *conn)
{
	/* can't assert the result, because conn->sockfd could be closed by the
	dispatched thread already */
	epoll_ctl(main_epfd, EPOLL_CTL_DEL, conn->sockfd, NULL);
}

/**
 * send_errno - Write @e to @sockfd
 * 
 * Note: caller should make sure it is the first write to @sockfd, so the write
 * won't block.
 */
static void send_errno(int sockfd, enum umem_cache_errno e)
{
	char _e = (char)e;
	int ret __attribute__((unused)) = send(sockfd, &_e, 1, MSG_NOSIGNAL);
	assert(ret == 1);
}

/**
 * free_before_accept - Write @e and close @sockfd
 * 
 * Note: we never write to @sockfd before
 */
static void free_before_accept(int sockfd, enum umem_cache_errno e)
{
	send_errno(sockfd, e);
	close(sockfd);
}

/**
 * conn_free_before_dispatched - Write @e and free @conn
 * 
 * Note: we never write to @conn before
 */
void conn_free_before_dispatched(struct conn *conn, enum umem_cache_errno e)
{
	send_errno(conn->sockfd, e);
	conn_free(conn);
}

/**
 * conn_accept - Accept connection and add it to main thread epoll
 * @sockfd: connection bound socket file descriptor
 */
void conn_accept(int sockfd)
{
	struct conn *conn = conn_malloc();
	if (conn == NULL) {
		free_before_accept(sockfd, E_CONNECT_TOO_MANY);
		return;
	}

	conn->sockfd = sockfd;
	// conn->state = CONN_STATE_IN_THREAD_INFO;
	conn->unread = CONN_THREAD_INFO_SIZE;
	if (!main_epoll_add(conn))
		conn_free_before_dispatched(conn, E_CONNECT_KILL);
}

/**
 * conn_dispatched - @conn is dispatched, remove it from main thread epoll
 */
void conn_dispatched(struct conn *conn)
{
	main_epoll_del(conn);
}

/**
 * conn_free - Deallocates the space related to @conn
 */
void conn_free(struct conn *conn)
{
	close(conn->sockfd);

	pthread_mutex_lock(&mu);
	fixed_mem_cache_free(&conn_cache, conn);
	pthread_mutex_unlock(&mu);
}

/**
 * conn_range - Check if @ptr is inside a conn
 */
bool conn_range(void *ptr)
{
	return (unsigned long)ptr >= (unsigned long)__conn &&
	       (unsigned long)ptr <  (unsigned long)__conn + sizeof(__conn);
}
