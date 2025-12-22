// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RAFT_MACHINE_H
#define __UMEM_CACHE_RAFT_MACHINE_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

/**
 * machine - Network byte order machine
 */
struct machine {
	struct in6_addr sin6_addr;
	in_port_t sin6_port;
	uint32_t id;
	uint64_t stability;
	uint64_t version;
} __attribute__((aligned(8)));

#define MACHINE_SIZE		sizeof(struct machine)
#define MACHINES_MIN		4
#define MACHINES_MAX		INT32_MAX
#define MACHINES_SIZE_MIN	(MACHINE_SIZE * (uint64_t)MACHINES_MIN)
#define MACHINES_SIZE_MAX	(MACHINE_SIZE * (uint64_t)MACHINES_MAX)

uint32_t machine_id(const struct machine *m);
uint64_t machine_stability(const struct machine *m);
bool machines_size_valid(uint64_t size);
bool machine_available(const struct machine *m);
void machine_set_stability(struct machine *m, bool available);
void machine_copy(struct machine *dest, const struct machine *src);
int machine_addr_cmp(const struct machine *a, const struct machine *b);
int machines_cmp(struct machine *a, struct machine *b, uint64_t n);
void machines_copy(struct machine *dest, const struct machine *src, uint64_t n);
void machines_sort_by_addr(struct machine *machines, uint32_t n);
struct machine *machines_search_addr(
	const struct machine *m, const struct machine *machines, uint32_t n);
const struct machine *machines_find(
		const struct machine *machines, uint32_t n, uint32_t id);

#endif
