// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_THREAD_H
#define __UMEM_CACHE_THREAD_H

#include <pthread.h>
#include "conn.h"
#include "hash_table.h"
#include "kv_cache.h"

#define KV_CACHE_LEN	75

/**
 * thread -
 * @memory: memory manager
 * @lru_head: lru of all enabled kv
 * @hash_table: hash table used to index kv or conn
 * @kv_cache_list: the list of kv_cache manages memory for kv and concat_val
 * @mu: protect @conn_list and @conn_nr which also accessed by main thread
 * @conn_list: the list of conns that dispatched to this thread
 * @conn_nr: the number of conns that dispatched to this thread, it is
 * required, we should treat every thread independently
 * @epfd: the epoll file descriptor that manages IO events for this thread
 */
struct thread {
	struct memory memory;
	struct list_head lru_head;
	struct hash_table hash_table;
	struct kv_cache kv_cache_list[KV_CACHE_LEN];

	uint64_t jiffies;
	struct hlist_head clock_list;

	pthread_mutex_t mu;
	struct hlist_head conn_list;
	uint32_t conn_nr;

	int epfd;
};

void kv_cache_idx_generate_print();
bool thread_init(struct thread *t, uint64_t page);
bool thread_run(struct thread *t);
bool thread_add_conn(struct thread *t, struct conn *conn);

#endif
