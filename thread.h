// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_THREAD_H
#define __UMEM_CACHE_THREAD_H

#include <pthread.h>
#include "conn.h"
#include "kv_hash_table.h"
#include "page_cache.h"

#define KEY_LEN ((CONFIG_KEY_SIZE_LIMIT >> 3) + 1)

/**
 * thread -
 * @memory: memory manager
 * @reclaim_bytes: number of bytes of memory should be reclaimed
 * @hash_table: hash table used to index kv
 * @global_lru: lru of all kv, regular memory reclaim will use it
 * @mu: protect @conn_list and @conn_nr which also accessed by main thread
 * @conn_list: the list of conns that dispatched to this thread
 * @conn_nr: the number of conns dispatched to this thread
 * @epfd: the epoll file descriptor that manages IO events for this thread
 * @unblocked_conn_nr: the number of active conns that not blocked by IO
 * @active_conn_list: the list of conns that have been activated
 * @kv_cache_list: the list of memory cache manages memory for kv
 * @hval_cache_list: the list of memory cache manages memory for huge value
 * @sval_cache_list: the list of memory cache manages memory for small value
 */
struct thread {
	struct memory memory;
	int64_t reclaim_bytes;
	struct kv_hash_table hash_table;
	struct list_head global_lru;

	pthread_mutex_t mu;
	struct hlist_head conn_list;
	uint32_t conn_nr;

	int epfd;
	uint32_t unblocked_conn_nr;
	struct hlist_head active_conn_list;

	struct mem_cache    kv_cache_list[KEY_LEN];
	struct page_cache hval_cache_list[HVAL_LEN];
	struct mem_cache  sval_cache_list[SVAL_LEN];
} __attribute__((aligned(64)));

bool thread_init(struct thread *t, struct memory *m);
bool thread_run(struct thread *t, uint64_t page);
bool thread_add_conn(struct thread *t, struct conn *conn);

#endif
