// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RAFT_CLUSTER_H
#define __UMEM_CACHE_RAFT_CLUSTER_H

#include "member.h"

/**
 * cluster -
 * @members: members are sorted by id
 */
struct cluster {
	struct cluster *next_stale;
	uint32_t require_old_votes;
	uint32_t require_new_votes;

	uint32_t members_n;
	struct member members[];
};

struct cluster *cluster_malloc(struct log *log, uint32_t leader);
void cluster_free(struct cluster *cl);
bool cluster_has_conn(const struct cluster *cl, const struct raft_conn *conn);
struct log *log_malloc_change_available(
	const struct cluster *cluster, const struct log *old, uint64_t term);

#endif
