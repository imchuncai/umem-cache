// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <stdlib.h>
#include <string.h>
#include "cluster.h"

static uint32_t majority(uint32_t n)
{
	assert(n > 0);

	return n / 2 + 1;
}

struct cluster *cluster_malloc(struct log *log, uint32_t leader)
{
	bool leader_in_old = log_machines_find_old(log, leader);
	bool leader_in_new = log_machines_find_new(log, leader);
	uint32_t n = log->distinct_machines_n;
	if (leader_in_old || leader_in_new)
		n--;

	uint64_t size = sizeof(struct cluster) + sizeof(struct member) * n;
	struct cluster *cl = malloc(size);
	if (cl) {
		cl->next_stale = NULL;

		cl->require_old_votes = majority(log->old_n);
		if (leader_in_old)
			cl->require_old_votes--;

		if (log->new_n == 0) {
			cl->require_new_votes = 0;
		} else {
			cl->require_new_votes = majority(log->new_n);
			if (leader_in_new)
				cl->require_new_votes--;
		}

		cl->members_n = n;
		uint32_t k __attribute__((unused));
		k = members_init(cl->members, log, leader);
		assert(k == n);
	}
	return cl;
}

void cluster_free(struct cluster *cl)
{
	for (uint32_t i = 0; i < cl->members_n; i++) {
		struct member *m = cl->members + i;
		struct raft_conn *conn = &m->conn;
		if (conn->state != RAFT_CONN_STATE_NOT_CONNECTED)
			raft_conn_clear(conn);
	}
	free(cl);
}

bool cluster_has_conn(const struct cluster *cl, const struct raft_conn *conn)
{
	return cl && (void *)conn >= (void *)cl &&
		(void *)conn < (void *)(cl->members + cl->members_n);
}

static struct member *cluster_search(const struct cluster *cl,
				     const struct machine *m)
{
	return members_search_id(cl->members, cl->members_n, machine_id(m));
}

struct log *log_malloc_change_available(
		const struct cluster *cl, const struct log *old, uint64_t term)
{
	uint32_t n = old->old_n;
	struct log *log = log_malloc_unstable(n, n);
	if (log) {
		struct machine *new_machines = log->machines + n;
		machines_copy(new_machines, old->machines, n);
		for (uint32_t i = 0; i < n; i++) {
			struct machine *m = new_machines + i;
			struct member *member = cluster_search(cl, m);
			if (member)
				machine_set_stability(m, member->available);
			else
				machine_set_stability(m, true);
		}

		log_complete_change_available(log, old, term);
	}
	return log;
}
