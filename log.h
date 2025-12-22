// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RAFT_LOG_H
#define __UMEM_CACHE_RAFT_LOG_H

#include <assert.h>
#include "machine.h"

#define B00_0_0	0
#define B00_0_1 1
#define B00_1_0 2
#define B00_1_1 3
#define B01_0_0 4
#define B01_0_1 5
#define B01_1_0 6
#define B10_0_0 8
#define B11_1_0 14

#define LOG_TYPE_UNSTABLE_MASK	B11_1_0
#define LOG_TYPE_JOINT_MASK	B00_1_0

/**
 * LOG_TYPE_GROW_TRANSFORM - Designed to against shrink after grow
 */
enum log_type {
	LOG_TYPE_OLD			= B00_0_0,
	LOG_TYPE_ADJUST			= B00_1_0,
	LOG_TYPE_SHRINK			= B01_1_0,
	LOG_TYPE_CHANGE_AVAILABLE	= B01_0_0,
	LOG_TYPE_GROW_COMPLETE		= B10_0_0,

	LOG_TYPE_GROW_TRANSFORM		= B00_0_1,
	LOG_TYPE_GROW			= B00_1_1,
	LOG_TYPE_GROW_CHANGE_AVAILABLE	= B01_0_1,
} __attribute__((__packed__));

static_assert(sizeof(enum log_type) == 1);

/**
 * log - Raft log that contains cluster informations
 */
struct log {
	int64_t refcount;

	uint64_t index;
	uint64_t term;
	uint64_t version;
	uint64_t next_machine_version;
	uint32_t next_machine_id;
	enum log_type type;
	uint32_t old_n;
	uint32_t new_n;
	uint64_t distinct_machines_n;
	struct machine machines[];
};

struct log *log_malloc(uint64_t machines_size);
struct log *log_malloc_init(uint64_t machines_size);
struct log *log_malloc_stable(const struct log *unstable);
struct log *log_malloc_unstable(uint32_t old_n, uint32_t new_n);
struct log *log_malloc_grow_complete(const struct log *transform, uint64_t term);

void log_borrow(struct log *log);
void log_return(struct log *log);
bool log_at_least_up_to_date(const struct log *log, uint64_t index, uint64_t term);
bool log_complete_init(struct log *log);
bool log_complete_change(struct log *log, struct log *old, uint64_t term);
void log_complete_change_available(
		struct log *log, const struct log *old_log, uint64_t term);

const struct machine *log_machines_find(const struct log *log, uint32_t id);
const struct machine *log_machines_find_old(const struct log *log, uint32_t id);
const struct machine *log_machines_find_new(const struct log *log, uint32_t id);

#endif
