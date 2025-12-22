// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "machine.h"
#include "encoding.h"

uint32_t machine_id(const struct machine *m)
{
	return ntohl(m->id);
}

uint64_t machine_stability(const struct machine *m)
{
	return ntohll(m->stability);
}

static bool is_power_of_2(uint32_t i)
{
	return (i & (i - 1)) == 0;
}

bool machines_size_valid(uint64_t size)
{
	return size >= MACHINES_SIZE_MIN && size <= MACHINES_SIZE_MAX &&
		size % MACHINE_SIZE == 0 && is_power_of_2(size / MACHINE_SIZE);
}

static bool stability_to_available(uint64_t stability)
{
	return stability & 1;
}

bool machine_available(const struct machine *m)
{
	return stability_to_available(machine_stability(m));
}

void machine_set_stability(struct machine *m, bool available)
{
	uint64_t stability = machine_stability(m);
	if (stability_to_available(stability) != available)
		m->stability = htonll(stability + 1);
}

void machine_copy(struct machine *dest, const struct machine *src)
{
	memcpy(dest, src, MACHINE_SIZE);
}

int machine_addr_cmp(const struct machine *a, const struct machine *b)
{
	static_assert(offsetof(struct machine, sin6_port) ==
		offsetof(struct machine, sin6_addr) + sizeof(struct in6_addr));

	return memcmp(&a->sin6_addr, &b->sin6_addr,
			sizeof(struct in6_addr) + sizeof(in_port_t));
}

static int _machine_addr_cmp(const void *a, const void *b)
{
	return machine_addr_cmp(a, b);
}

void machines_sort_by_addr(struct machine *machines, uint32_t n)
{
	qsort(machines, n, MACHINE_SIZE, _machine_addr_cmp);
}

struct machine *machines_search_addr(
	const struct machine *key, const struct machine *base, uint32_t n)
{
	return bsearch(key, base, n, MACHINE_SIZE, _machine_addr_cmp);
}

int machines_cmp(struct machine *a, struct machine *b, uint64_t n)
{
	return memcmp(a, b, n * MACHINE_SIZE);
}

void machines_copy(struct machine *dest, const struct machine *src, uint64_t n)
{
	memcpy(dest, src, n * MACHINE_SIZE);
}

const struct machine *machines_find(
		const struct machine *machines, uint32_t n, uint32_t id)
{
	for (uint64_t i = 0; i < n; i++) {
		if (machine_id(machines + i) == id)
			return machines + i;
	}
	return NULL;
}
