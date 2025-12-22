// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "member.h"

const char *member_string_address(const struct member *m, char str[INET6_ADDRSTRLEN])
{
	return inet_ntop(AF_INET6, &m->sin6_addr, str, INET6_ADDRSTRLEN);
}

static int member_id_cmp(const void *_a, const void *_b)
{
	const struct member *a = _a;
	const struct member *b = _b;
	return (a->id > b->id) - (a->id < b->id);
}

static void members_sort_by_id(struct member *members, uint32_t n)
{
	qsort(members, n, sizeof(struct member), member_id_cmp);
}

struct member *members_search_id(const struct member *members, uint32_t n, uint32_t id)
{
	struct member key;
	key.id = id;
	return bsearch(&key, members, n, sizeof(struct member), member_id_cmp);
}

static void init_member(struct member *m, const struct machine *machine, unsigned char type)
{
	m->id = machine_id(machine);
	m->unstable_round = 0;
	m->type = type;
	m->sin6_port = machine->sin6_port;
	m->sin6_addr = machine->sin6_addr;
	m->available = machine_available(machine);
	m->available_since_last_timer_event = false;
	raft_out_conn_init(&m->conn);
	m->next_index = 0;
	m->match_index = 0;
}

static uint32_t init_members(struct member *members, unsigned char type,
		struct machine *machines, uint32_t n, uint32_t leader)
{
	uint32_t k = 0;
	for (uint32_t i = 0; i < n; i++) {
		struct machine *m = machines + i;
		if (machine_id(m) != leader) {
			init_member(members + k, m, type);
			k++;
		}
	}
	return k;
}

static uint32_t init_adjust(struct member *members, struct log *log, uint32_t leader)
{
	uint32_t n = log->old_n;
	uint32_t k = init_members(members, MEMBER_TYPE_OLD, log->machines, n, leader);

	members_sort_by_id(members, k);
	uint32_t sorted = k;

	struct machine *new_machines = log->machines + n;
	for (uint32_t i = 0; i < n; i++) {
		struct machine *m = new_machines + i;
		uint32_t id = machine_id(m);
		if (id != leader) {
			struct member *member = members_search_id(members, sorted, id);
			if (member) {
				member->type = MEMBER_TYPE_ALL;
			} else {
				init_member(members + k, m, MEMBER_TYPE_NEW);
				k++;
			}
		}
	}
	return k;
}

static uint32_t init(struct member *members, uint32_t leader, unsigned char extra_type,
			uint32_t k, uint32_t n, struct machine *machines)
{
	uint32_t m = init_members(members, MEMBER_TYPE_ALL, machines, k, leader);
	return m + init_members(members + m, extra_type, machines + k, n - k, leader);
}

uint32_t members_init(struct member *members, struct log *log, uint32_t leader)
{
	uint32_t k;
	if (log->type == LOG_TYPE_ADJUST)
		k = init_adjust(members, log, leader);
	else if (log->new_n >= log->old_n)
		k = init(members, leader, MEMBER_TYPE_NEW, log->old_n, log->new_n, log->machines + log->old_n);
	else
		k = init(members, leader, MEMBER_TYPE_OLD, log->new_n, log->old_n, log->machines);

	members_sort_by_id(members, k);
	return k;
}
