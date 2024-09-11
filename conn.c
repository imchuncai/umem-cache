// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>
#include <fcntl.h>
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

/**
 * conn_init - Initialize function
 */
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
	int ret __attribute__((unused));
	ret = epoll_ctl(main_epfd, EPOLL_CTL_DEL, conn->sockfd, NULL);
	assert(ret == 0);
}

/**
 * nonblock - Make @sockfd non-blocking
 * 
 * @return: true on success, false on failure
 */
static bool nonblock(int sockfd)
{
	int ret = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
	return ret != -1;
}

/**
 * send_errno - Write @e to @sockfd
 * 
 * Note: if it is the first write to @sockfd, the write won't block
 */
static void send_errno(int sockfd, enum umem_cache_errno e)
{
	char _e = (char)e;
	send(sockfd, &_e, 1, MSG_NOSIGNAL);
}

/**
 * free_before_accept - Write @e and close @sockfd
 * 
 * Note: we never write to @conn before
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
	if (!nonblock(sockfd)) {
		free_before_accept(sockfd, E_CONNECT_KILL);
		return;
	}

	struct conn *conn = conn_malloc();
	if (conn == NULL) {
		free_before_accept(sockfd, E_CONNECT_TOO_MANY);
		return;
	}

	conn->sockfd = sockfd;
	conn->state = CONN_STATE_IN_THREAD_INFO;
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
 * conn_ref_kv: Reference @kv to @conn
 */
void conn_ref_kv(struct conn *conn, struct kv *kv)
{
	hlist_add(&kv->ref_conn_list, &conn->kv_ref_node);
	conn->kv = kv;
}
