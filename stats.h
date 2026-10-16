// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <stdint.h>

/**
 * stats -
 * @connects: number of accepted connections
 * @disconnects: number of disconnect connections
 * @hits: number of hits
 * @misses: number of misses
 * @dels: number of delete requests
 */
struct stats {
	uint64_t connects;
	uint64_t disconnects;
	uint64_t hits;
	uint64_t misses;
	uint64_t dels;
};

void stats_init(struct stats *s);
void stats_connected(struct stats *s);
void stats_disconnected(struct stats *s);
void stats_hit(struct stats *s);
void stats_miss(struct stats *s);
void stats_del(struct stats *s);

struct stats_res {
	uint32_t size;

	uint32_t config_thread_nr;
	uint32_t config_tcp_timeout;
	uint32_t config_max_conn;
	uint64_t config_mem_limit;
	uint64_t version;

	uint64_t nanoseconds;
	struct stats stats;
};
