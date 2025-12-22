// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_THREAD_H
#define __UMEM_CACHE_THREAD_H

#include "conn.h"
#include "hash_table.h"
#include "kv_cache.h"
#include "fixed_mem_cache.h"

#define KV_CACHE_LEN	75

#define THREAD_MAX_CONN	(CONFIG_MAX_CONN / CONFIG_THREAD_NR)
#define THREAD_MAX_MEM	((uint64_t)CONFIG_MEM_LIMIT / CONFIG_THREAD_NR)

static_assert(THREAD_MAX_CONN <= INT32_MAX);

/**
 * thread -
 * @epfd: the epoll file descriptor that manages IO events for this thread
 * @__warmed_up: used for cluster growth. we call thread is warmed up once we
 * reclaim memory from it, be aware of main thread will read it.
 * @memory: memory manager
 * @lru_head: lru of all enabled kv
 * @hash_table: hash table used to index kv or conn
 * @kv_cache_list: the list of kv_cache manages memory for kv and concat_val
 */
struct thread {
	int epfd;
#ifdef CONFIG_RAFT
	bool __warmed_up;
#endif

	struct memory memory;
	struct list_head lru_head;
	struct hash_table hash_table;
	struct hlist_head clock_list;
	struct kv_cache kv_cache_list[KV_CACHE_LEN];

	struct fixed_mem_cache conn_cache;
	struct conn __conns[THREAD_MAX_CONN];

	struct epoll_event events[THREAD_MAX_CONN];
};

bool threads_run();
void thread_dispatch(uint32_t id, int sockfd);

#ifdef CONFIG_RAFT
bool threads_warmed_up();
#endif

#endif
