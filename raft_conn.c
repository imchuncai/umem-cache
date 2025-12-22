// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "raft_conn.h"
#include "debug.h"

#ifdef CONFIG_KERNEL_TLS
struct raft_conn *raft_in_conn_malloc(int sockfd, bool admin, struct in6_addr peer)
{
	struct raft_conn *conn = malloc(sizeof(struct raft_conn));
	if (conn) {
		if (!tls_init_server(&conn->session, sockfd, peer)) {
			free(conn);
			return NULL;
		}

		conn->sockfd = sockfd;
		conn->admin = admin;
		conn->state = RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_IN;
	}
	return conn;
}

ssize_t raft_conn_discard(struct raft_conn *conn)
{
	ssize_t discarded = 0;
	unsigned char trash[1024];
	while (true) {
		ssize_t n = read(conn->sockfd, trash, 1024);
		if (n == -1) {
			if (errno == EWOULDBLOCK) {
				return discarded;
			} else {
				raft_conn_free(conn);
				return -1;
			}
		} else {
			discarded += n;
			if (n < 1024)
				return discarded;
		}
	}
}
#else
struct raft_conn *raft_in_conn_malloc(int sockfd, bool admin, struct in6_addr)
{
	struct raft_conn *conn = malloc(sizeof(struct raft_conn));
	if (conn) {
		conn->sockfd = sockfd;
		conn->admin = admin;
		raft_conn_set_io(conn, RAFT_CONN_STATE_IN_CMD, RAFT_CONN_BUFFER_SIZE);
	}
	return conn;
}

ssize_t raft_conn_discard(struct raft_conn *conn)
{
	ssize_t n = recv(conn->sockfd, NULL, SIZE_MAX, MSG_TRUNC);
	if (n > 0)
		return n;

	if (n == -1 && errno == EWOULDBLOCK)
		return 0;

	raft_conn_free(conn);
	return -1;
}
#endif

static bool conn_outgoing(struct raft_conn *conn)
{
	return conn->state < RAFT_CONN_OUTGOING_INCOMING_DIVIDER;
}

static bool conn_borrowed_log(struct raft_conn *conn)
{
	return conn->state & EPOLLLOG;
}

void raft_out_conn_init(struct raft_conn *conn)
{
	conn->state = RAFT_CONN_STATE_NOT_CONNECTED;
}

void raft_conn_set_io(
	struct raft_conn *conn, enum raft_conn_state state, uint64_t size)
{
	conn->state = state;
	conn->unio = size;
}

/**
 * raft_conn_borrow_log - @conn borrow @log and change state to @state with
 * @size bytes unio data
 */
void raft_conn_borrow_log(struct raft_conn *conn, struct log *log,
				enum raft_conn_state state, uint64_t size)
{
	assert(!conn_borrowed_log(conn));
	raft_conn_set_io(conn, state, size);
	conn->log = log;
	log_borrow(log);
	assert(conn_borrowed_log(conn));
}

/**
 * raft_conn_return_log - Return borrowed log
 * 
 * Note: caller should change conn state as soon as possible
 */
void raft_conn_return_log(struct raft_conn *conn)
{
	assert(conn_borrowed_log(conn));

	log_return(conn->log);
}

void raft_conn_change_to_ready_for_use(struct raft_conn *conn)
{
	conn->state = RAFT_CONN_STATE_READY_FOR_USE;
}

void raft_conn_free(struct raft_conn *conn)
{
	debug_printf("raft_conn_free:\n");
	assert(!conn_outgoing(conn));

	if (conn_borrowed_log(conn))
		raft_conn_return_log(conn);
	else if (conn->state > RAFT_CONN_STATE_AUTHORITY_DIVIDER)
		list_del(&conn->authority_node);
#ifdef CONFIG_KERNEL_TLS
	else if (conn->state < RAFT_CONN_STATE_TLS_SERVER_DIVIDER)
		tls_deinit(&conn->session);
#endif

	close(conn->sockfd);
	free(conn);
}

void raft_conn_clear(struct raft_conn *conn)
{
	debug_printf("raft_conn_clear:\n");
	assert(conn_outgoing(conn) && conn->state != RAFT_CONN_STATE_NOT_CONNECTED);

	if (conn_borrowed_log(conn))
		raft_conn_return_log(conn);
#ifdef CONFIG_KERNEL_TLS
	else if (conn->state < RAFT_CONN_STATE_TLS_CLIENT_DIVIDER)
		tls_deinit(&conn->session);
#endif

	conn->state = RAFT_CONN_STATE_NOT_CONNECTED;
	close(conn->sockfd);
}

void raft_conn_free_or_clear(struct raft_conn *conn)
{
	if (conn_outgoing(conn))
		raft_conn_clear(conn);
	else
		raft_conn_free(conn);
}

static bool conn_check_io(struct raft_conn *conn, ssize_t n)
{
	if (n > 0) {
		assert(conn->unio >= (size_t)n);
		conn->unio -= n;
		return true;
	}

	if (!(n == -1 && errno == EWOULDBLOCK))
		raft_conn_free_or_clear(conn);

	return false;
}

/**
 * conn_write - Write from @buffer to @conn
 * 
 * @return: true on something is written, false on nothing is written
 */
static bool conn_write(struct raft_conn *conn, const unsigned char *buffer)
{
	assert(conn->unio > 0);
	ssize_t n = send(conn->sockfd, buffer, conn->unio, MSG_NOSIGNAL);
	return conn_check_io(conn, n);
}

/**
 * raft_conn_read - Read from @conn to @buffer
 * 
 * @return: true on something is read, false on nothing is read
 */
bool raft_conn_read(struct raft_conn *conn, unsigned char *buffer)
{
	assert(conn->unio > 0);
	ssize_t n = read(conn->sockfd, buffer, conn->unio);
	return conn_check_io(conn, n);
}

/**
 * raft_conn_full_read - Read from @conn to @buffer
 * 
 * @return: true on full read, false on short read
 */
bool raft_conn_full_read(struct raft_conn *conn, unsigned char *buffer)
{
	return raft_conn_read(conn, buffer) && conn->unio == 0;
}

/**
 * raft_conn_full_read_to_buffer - Read from @conn to @conn->buffer
 * 
 * @return: true on full read, false on short read
 */
bool raft_conn_full_read_to_buffer(struct raft_conn *conn, uint64_t size)
{
	uint64_t readed = size - conn->unio;
	return raft_conn_full_read(conn, conn->buffer + readed);
}

/**
 * conn_full_write - Write from @buffer to @conn
 * 
 * @return: true on full write, false on short write
 */
static bool conn_full_write(struct raft_conn *conn, const unsigned char *buffer)
{
	return conn_write(conn, buffer) && conn->unio == 0;
}

/**
 * raft_conn_full_write_buffer - Write from @conn->buffer to @conn
 * 
 * @return: true on full write, false on short write
 */
bool raft_conn_full_write_buffer(struct raft_conn *conn, uint64_t size)
{
	uint64_t written = size - conn->unio;
	return conn_full_write(conn, conn->buffer + written);
}

/**
 * raft_conn_write_byte - Write @b to @conn
 * 
 * @return: true on @b is written, false on nothing is written
 */
bool raft_conn_write_byte(struct raft_conn *conn, char b)
{
	ssize_t n = send(conn->sockfd, &b, 1, MSG_NOSIGNAL);
	if (n > 0)
		return true;

	if (!(n == -1 && errno == EWOULDBLOCK))
		raft_conn_free_or_clear(conn);

	return false;
}

/**
 * conn_write_msg - Write message from @iov to @conn
 * @iovlen: length of @iov
 * 
 * @return: true on something is written, false on nothing is written
 */
static bool conn_write_msg(
		struct raft_conn *conn, struct iovec *iov, size_t iovlen)
{
	struct msghdr msg = {};
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	assert(conn->unio > 0);
	ssize_t n = sendmsg(conn->sockfd, &msg, MSG_NOSIGNAL);
	return conn_check_io(conn, n);
}

/**
 * raft_conn_full_write_msg - Write message from @iov to @conn
 * @iovlen: length of @iov
 * 
 * @return: true on full write, false on short write
 */
bool raft_conn_full_write_msg(
		struct raft_conn *conn, struct iovec *iov, size_t iovlen)
{
	return conn_write_msg(conn, iov, iovlen) && conn->unio == 0;
}
