// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_CONN_H
#define __UMEM_CACHE_CONN_H

#include <sys/epoll.h>
#include "kv.h"

enum cache_cmd {
	CACHE_CMD_GET_OR_SET,
	CACHE_CMD_DEL,
} __attribute__((__packed__));

static_assert(sizeof(enum cache_cmd) == 1);

#define CMD_SIZE_MAX	(1 + (1 + CONFIG_KEY_SIZE_MAX))
#define CMD_SIZE_MIN	(1 + 1)
#define GET_RES_SIZE	(8 + 1)
#define SET_REQ_SIZE	8

/* see README.rst -> CACHE PROTOCOL */
enum conn_state {
	CONN_STATE_IN_CMD		= (0 << 3) + EPOLLIN,
	CONN_STATE_GET_BLOCKED		= (1 << 3) + 0,
	CONN_STATE_OUT_SUCCESS		= (2 << 3) + EPOLLOUT,
	CONN_STATE_GET_OUT_HIT		= (3 << 3) + EPOLLOUT,
	
	CONN_STATE_SET_DIVIDER		= (4 << 3) + 0,
	CONN_STATE_GET_OUT_MISS		= (5 << 3) + EPOLLOUT,
	CONN_STATE_SET_IN_VALUE_SIZE	= (6 << 3) + EPOLLIN,
	CONN_STATE_SET_IN_VALUE		= (7 << 3) + EPOLLIN,
} __attribute__((__packed__));

/**
 * conn - Structure describes connection
 * @clock_time_left: time ticks remain before timeout, 0 for clock not called
 * @sockfd: connection bound socket file descriptor
 * @kv_borrower: borrows kv for operation
 * @val_size: value size received from client
 * @clock_node: resides in (struct thread->clock_list) when clock is called
 * @unio: number of bytes not read() or write()
 * @hash_node: resides in (struct thread->hash_table) before malloc kv
 * @key: key received from client
 */
struct conn {
	union {
		unsigned char buffer[GET_RES_SIZE];
		struct {
			uint64_t size;
			bool miss;

			enum conn_state state;
			unsigned char clock_time_left;
			int sockfd;
		};
	};
	struct kv_borrower kv_borrower;
	uint64_t val_size;
	struct hlist_node clock_node;
	struct list_head interest_list;
	uint64_t unio;
	struct hlist_node hash_node;
	unsigned char key[1 + CONFIG_KEY_SIZE_MAX];
} __attribute__((aligned(8)));
/* alignment is required by loop_forever */

/* this offset is required for hash table to locate the key, also kind of
required by CONN_STATE_IN_CMD */
static_assert(offsetof(struct conn, key) - offsetof(struct conn, hash_node) ==
		sizeof(struct hlist_node));

/* alignment is required by key comparison */
static_assert(offsetof(struct conn, key) % 8 == 0);

#endif
