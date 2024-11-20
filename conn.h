// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONN_H
#define __UMEM_CACHE_CONN_H

#include "kv.h"
#include "errno.h"

enum conn_cmd {
	CONN_CMD_GET_OR_SET,
	CONN_CMD_DEL,
};

#define CONN_THREAD_INFO_SIZE	(4 + 4)
#define CMD_SIZE_MAX		(1 + (1 + CONFIG_KEY_SIZE_LIMIT))
#define CMD_SIZE_MIN		(1 + 1)
#define CONN_VAL_SIZE		(1 + 8)

/* see README.rst -> CLIENT PROTOCOL */
enum conn_state {
	// CONN_STATE_IN_THREAD_INFO,
	CONN_STATE_IN_CMD			= (0 << 3) + EPOLLIN,
	CONN_STATE_GET_BLOCKED			= (1 << 3) + 0,
	CONN_STATE_OUT_SUCCESS			= (2 << 3) + EPOLLOUT,
	CONN_STATE_GET_OUT_HIT			= (3 << 3) + EPOLLOUT,
	
	CONN_STATE_GET_OUT_MISS			= (4 << 3) + EPOLLOUT,
	CONN_STATE_SET_IN_VALUE_SIZE		= (5 << 3) + EPOLLIN,
	CONN_STATE_SET_IN_VALUE			= (6 << 3) + EPOLLIN,

	CONN_STATE_SET_DISCARD_VALUE_SIZE	= (7 << 3) + EPOLLIN,
	CONN_STATE_SET_DISCARD_VALUE		= (8 << 3) + EPOLLIN,
};

/**
 * conn - Structure describes connection
 * @sockfd: connection bound socket file descriptor
 * @call_clock: is clock called for timeout
 * @buffer: used by main thread and CONN_STATE_SET_IN_VALUE_SIZE
 * @thread_node: resides in (struct thread->conn_list) or
 * (struct thread->active_conn_list). Must hold (struct thread->mu) before
 * access if resides in (struct thread->conn_list)
 * @kv_borrower: borrows kv for operation
 * @val_size: value size received from client
 * @clock_node: resides in (struct thread->clock_list) when clock is called
 * @lock_expire_at: lock expire jiffies
 * @unread: number of bytes not read()
 * @unwrite: number of bytes not write()
 * @unio: number of bytes not read() or write()
 * @hlist_node: resides in (struct thread->hash_table) before malloc kv
 * @key: key received from client
 */
struct conn {
	int sockfd;
	bool call_clock;
	enum conn_state state;
	unsigned char buffer[1 + 8];
	struct hlist_node thread_node;
	struct kv_borrower kv_borrower;
	uint64_t val_size;
	struct hlist_node clock_node;
	uint64_t lock_expire_at;
	struct list_head interest_list;
	union {
		uint64_t unread;
		uint64_t unwrite;
		uint64_t unio;
	};
	struct hlist_node hash_node;
	unsigned char key[1 + CONFIG_KEY_SIZE_LIMIT];
};

/* this offset is required for hash table to locate the key, also kind of
required by CONN_STATE_IN_CMD */
static_assert(offsetof(struct conn, key) - offsetof(struct conn, hash_node) ==
		sizeof(struct hlist_node));

/* alignment is required by key comparison */
static_assert(offsetof(struct conn, key) % 8 == 0);

void conn_init(int __epfd);
void conn_accept(int sockfd);
void conn_free_before_dispatched(struct conn *conn, enum umem_cache_errno e);
void conn_dispatched(struct conn *conn);
void conn_free(struct conn *conn);
bool conn_range(void *ptr);

#endif
