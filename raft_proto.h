// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RAFT_PROTO_H
#define __UMEM_CACHE_RAFT_PROTO_H

#include "log.h"

enum raft_cmd {
	RAFT_CMD_REQUEST_VOTE,
	RAFT_CMD_APPEND_LOG,
	RAFT_CMD_HEARTBEAT,

	RAFT_CMD_INIT_CLUSTER,
	RAFT_CMD_CHANGE_CLUSTER,

	RAFT_CMD_ADMIN_DIVIDER,

	RAFT_CMD_LEADER,
	RAFT_CMD_CLUSTER,
	RAFT_CMD_CONNECT,
	RAFT_CMD_AUTHORITY,
} __attribute__((__packed__));

static_assert(sizeof(enum raft_cmd) == 1);

struct request_vote_req {
	enum raft_cmd cmd;
	uint32_t candidate_id;
	uint64_t term;
	uint64_t log_index;
	uint64_t log_term;
} __attribute__((aligned(8)));

struct request_vote_res {
	uint64_t term;
	bool granted;
} __attribute__((aligned(8)));

struct append_log_req {
	enum raft_cmd cmd;
	enum log_type type;
	uint64_t machines_size;
	uint64_t term;
	uint32_t leader_id;
	uint32_t follower_id;
	uint64_t log_index;
	uint64_t log_term;
	uint64_t version;
	uint64_t next_machine_version;
	uint32_t next_machine_id;
	uint32_t new_machine_nr;
	uint64_t distinct_machines_n;
} __attribute__((aligned(8)));

struct heartbeat_req {
	enum raft_cmd cmd;
	uint64_t term;
} __attribute__((aligned(8)));

struct append_entry_res {
	uint64_t term;
	bool applied;
} __attribute__((aligned(8)));

struct change_cluster_req {
	enum raft_cmd cmd;
	uint64_t machines_size;
} __attribute__((aligned(8)));

struct leader_res {
	struct in6_addr sin6_addr;
	in_port_t sin6_port;
	bool lost;
} __attribute__((aligned(4)));

struct cluster_res {
	enum log_type type;
	uint64_t machines_size;
	uint64_t version;
} __attribute__((aligned(8)));

struct connect_req {
	enum raft_cmd cmd;
	uint32_t thread_id;
} __attribute__((aligned(4)));

struct authority_approval {
	uint64_t version;
	uint64_t count;
} __attribute__((aligned(8)));

#define RAFT_CONN_BUFFER_SIZE	sizeof(struct append_log_req)

/* required by RAFT_CMD_INIT_CLUSTER and RAFT_CMD_CHANGE_CLUSTER */
static_assert(RAFT_CONN_BUFFER_SIZE < MACHINES_SIZE_MIN);

#endif
