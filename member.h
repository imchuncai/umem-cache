// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RAFT_MEMBER_H
#define __UMEM_CACHE_RAFT_MEMBER_H

#include "raft_conn.h"

#define MEMBER_TYPE_OLD	(1 << 0)
#define MEMBER_TYPE_NEW	(1 << 1)
#define MEMBER_TYPE_ALL	(MEMBER_TYPE_OLD + MEMBER_TYPE_NEW)

struct member {
	uint32_t id;
	char unstable_round;
	unsigned char type;
	in_port_t sin6_port;
	struct in6_addr sin6_addr;
	bool available;
	bool available_since_last_timer_event;

	struct raft_conn conn;
	uint64_t append_entry_round;

	uint64_t next_index;
	uint64_t match_index;
};

const char *member_string_address(const struct member *m, char str[INET6_ADDRSTRLEN]);
struct member *members_search_id(const struct member *members, uint32_t n, uint32_t id);
uint32_t members_init(struct member *members, struct log *log, uint32_t leader);

#endif
