// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONN_H
#define __UMEM_CACHE_CONN_H

#include "kv.h"
#include "errno.h"

enum conn_cmd {
	CONN_CMD_GET,
	CONN_CMD_SET,
	CONN_CMD_DEL,
};

enum conn_get_flag {
	CONN_GET_FLAG_NON_BLOCK		= 1 << 0,
	CONN_GET_FLAG_SET_ON_MISS	= 1 << 1,
};

enum conn_set_flag {
	CONN_SET_FLAG_NON_BLOCK		= 1 << 0,
};

enum conn_del_flag {
	CONN_DEL_FLAG_NON_BLOCK		= 1 << 0,
	CONN_DEL_FLAG_SET		= 1 << 1,
};

#define CONN_THREAD_INFO_SIZE		(4 + 4)
#define CONN_CMD_SIZE			(1 + 1 + 1 + 8)
#define CONN_VAL_SIZE			(1 + 8)

/* see README.rst -> CLIENT PROTOCOL */
enum conn_state {
	CONN_STATE_IN_THREAD_INFO,

	CONN_STATE_IN_CMD,

	CONN_STATE_OUT_ERRNO,

	CONN_STATE_GET_IN_KEY,
	CONN_STATE_GET_WAIT_LOCK,
	CONN_STATE_GET_OUT_ERRNO,
	CONN_STATE_GET_OUT_HIT,

	CONN_STATE_GET_SET_OUT_MISS,

	CONN_STATE_SET_IN_KEY,
	CONN_STATE_SET_LOCK_KV,
	CONN_STATE_SET_LOCK_WAIT,

	CONN_STATE_DEL_IN_KEY,
	CONN_STATE_DEL_LOCK_KV,

	CONN_STATE_DEL_SET_LOCK_WAIT,
	CONN_STATE_DEL_SET_OUT_SYN,

	CONN_STATE_DISCARD_VALUE,

	/* the following states will read from socket with a kv lock held, the
	   read should have a timeout */
	CONN_TIME_LIMIT_STATE_IN_VALUE_SIZE,
	CONN_TIME_LIMIT_STATE_IN_VALUE,
};

/**
 * conn - Structure describes connection
 * @sockfd: connection bound socket file descriptor
 * @active: if conn is active, conn is on (struct thread->active_conn_list),
 * otherwise, conn is on (struct thread->conn_list)
 * @blocked: is conn blocked by IO
 * @cmd_flag: command flag received from client
 * @kv: the kv currently being operated
 * @kv_ref_node: resides in (struct kv->ref_conn_list)
 * @val_size: value size received from client
 * @unread: number of bytes not read()
 * @unwrite: number of bytes not write()
 * @key_n: key size received from client
 * @buffer: buffer for socket io
 * @thread_node: resides in (struct thread->conn_list) or
 * (struct thread->active_conn_list). Must hold (struct thread->mu) before
 * access if resides in (struct thread->conn_list)
 */
struct conn {
	int sockfd;
	bool active;
	bool blocked;
	char cmd_flag;
	enum conn_state state;

	struct kv *kv;
	struct hlist_node kv_ref_node;

	uint64_t val_size;
	union {
		uint64_t unread;
		uint64_t unwrite;
	};
	unsigned char key_n;
	unsigned char buffer[CONFIG_KEY_SIZE_LIMIT];

	struct hlist_node thread_node;
} __attribute__((aligned(64)));

/* alignment is required by key comparison */
static_assert(offsetof(struct conn, key_n) % 8 == 0);
static_assert((offsetof(struct conn, thread_node) + 16) % 64 == 0);

void conn_init(int __epfd);
void conn_accept(int sockfd);
void conn_free_before_dispatched(struct conn *conn, enum umem_cache_errno e);
void conn_dispatched(struct conn *conn);
void conn_free(struct conn *conn);
void conn_ref_kv(struct conn *conn, struct kv *kv);

#endif
