// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <string.h>
#include <stdlib.h>
#include "log.h"
#include "encoding.h"

struct log *log_malloc(uint64_t machines_size)
{
	struct log *log = malloc(sizeof(struct log) + machines_size);
	if (log) {
		log->refcount = 0;
	}
	return log;
}

struct log *log_malloc_init(uint64_t machines_size)
{
	assert(machines_size_valid(machines_size));

	struct log *log = log_malloc(machines_size);
	if (log) {
		log->index = 1;
		log->term = 1;
		log->version = 1;
		log->next_machine_version = 1;
		log->next_machine_id = 1;
		log->type = LOG_TYPE_OLD;
		log->old_n = machines_size / MACHINE_SIZE;
		log->new_n = 0;
		log->distinct_machines_n = log->old_n;
	}
	return log;
}

struct log *log_malloc_stable(const struct log *unstable)
{
	uint64_t size = MACHINE_SIZE * (uint64_t)unstable->new_n;
	struct log *log = log_malloc(size);
	if (log) {
		log->index = unstable->index + 1;
		log->term = unstable->term;
		log->version = unstable->version + 1;
		log->next_machine_version = unstable->next_machine_version;
		log->next_machine_id = unstable->next_machine_id;
		log->type = unstable->type & ~LOG_TYPE_UNSTABLE_MASK;
		log->old_n = unstable->new_n;
		log->new_n = 0;
		log->distinct_machines_n = unstable->new_n;

		memcpy(log->machines, unstable->machines + unstable->old_n, size);
	}
	return log;
}

struct log *log_malloc_unstable(uint32_t old_n, uint32_t new_n)
{
	struct log *log = log_malloc(MACHINE_SIZE * (uint64_t)(old_n + new_n));
	if (log) {
		log->old_n = old_n;
		log->new_n = new_n;
	}
	return log;
}

static void __log_complete_unstable(
			struct log *log, const struct log *old, uint64_t term)
{
	log->index = old->index + 1;
	log->term = term;
	log->version = old->version;
	log->next_machine_version = old->next_machine_version;
	log->next_machine_id = old->next_machine_id;
	machines_copy(log->machines, old->machines, old->old_n);
}

static void log_machine_reset_version(struct log *log, struct machine *m)
{
	m->version = htonll(log->next_machine_version);
	log->next_machine_version++;
}

struct log *log_malloc_grow_complete(const struct log *transform, uint64_t term)
{
	uint32_t n = transform->old_n;
	struct log *log = log_malloc_unstable(n, n);
	if (log) {
		__log_complete_unstable(log, transform, term);
		log->type = LOG_TYPE_GROW_COMPLETE;

		struct machine *new_machines = log->machines + n;
		machines_copy(new_machines, log->machines, n);
		uint32_t k = n >> 1;
		for (uint32_t i = 0; i < k; i++)
			log_machine_reset_version(log, new_machines + i);

		log->distinct_machines_n = n;
	}
	return log;
}

void log_borrow(struct log *log)
{
	log->refcount++;
}

void log_return(struct log *log)
{
	log->refcount--;
	assert(log->refcount >= 0);
	if (log->refcount == 0)
		free(log);
}

/**
 * log_at_least_up_to_date - Check if a log with @index @term is at least
 * up to date than @log
 *
 * RAFT: 5.4.1
 * Raft determines which of two logs is more up-to-date
 * by comparing the index and term of the last entries in the
 * logs. If the logs have last entries with different terms, then
 * the log with the later term is more up-to-date. If the logs
 * end with the same term, then whichever log is longer is
 * more up-to-date.
 */
bool log_at_least_up_to_date(const struct log *log, uint64_t index, uint64_t term)
{
	return term > log->term || (term == log->term && index >= log->index);
}

static void log_machine_set_id(struct log *log, struct machine *m)
{
	m->id =htonl(log->next_machine_id);
	log->next_machine_id++;
}

static void log_machine_init(struct log *log, struct machine *m)
{
	log_machine_set_id(log, m);
	machine_set_stability(m, 1);
	log_machine_reset_version(log, m);
}

static bool sorted_by_addr_duplate(struct machine *machines, uint32_t n)
{
	for (uint32_t i = 1; i < n; i++) {
		if (machine_addr_cmp(machines + i - 1, machines + i) == 0)
			return true;
	}
	return false;
}

bool log_complete_init(struct log *log)
{
	for (uint32_t i = 0; i < log->old_n; i++)
		log_machine_init(log, log->machines + i);

	machines_sort_by_addr(log->machines, log->old_n);
	return !sorted_by_addr_duplate(log->machines, log->old_n);
}

static bool __log_complete_adjust(struct log *log, struct log *old_log)
{
	const uint32_t n = log->old_n;
	machines_sort_by_addr(log->machines, n);
	struct machine *old_machines = old_log->machines;
	struct machine *new_machines = log->machines + n;
	uint32_t keeps = 0;
	uint32_t new_n = 0;
	for (uint32_t i = 0; i < n; i++) {
		struct machine *old = old_machines + i;
		struct machine *new = new_machines + i;
		if (machine_addr_cmp(new, old) == 0) {
			machine_copy(new, old);
			keeps++;
		} else {
			struct machine *m;
			m = machines_search_addr(new, log->machines, n);
			if (m) {
				machine_copy(new, m);
			} else {
				log_machine_init(log, new);
				new_n++;
			}
		}
	}
	if (keeps == n || keeps < n / 2)
		return false;

	machines_copy(log->machines, new_machines, n);
	machines_sort_by_addr(log->machines, n);
	if (sorted_by_addr_duplate(log->machines, n))
		return false;

	machines_copy(log->machines, old_machines, n);

	struct machine *old = old_machines;
	struct machine *new = new_machines;
	while (machine_id(old) == machine_id(new) && !machine_available(old)) {
		old++;
		new++;
	}

	bool upgrade = machine_id(old) != machine_id(new);
	for (int32_t i = n - 1; i >= 0; i--) {
		struct machine *old = old_machines + i;
		struct machine *new = new_machines + i;
		if (machine_id(old) != machine_id(new))
			upgrade = true;
		else if (machine_available(new))
			upgrade = false;

		if (upgrade)
			log_machine_reset_version(log, new);
	}

	log->distinct_machines_n = (uint64_t)n + new_n;
	return true;
}

static bool __log_complete_shrink(struct log *log)
{
	if (machines_cmp(log->machines, log->machines + log->old_n, log->new_n))
		return false;

	log->distinct_machines_n = log->old_n;
	return true;
}

static bool __log_complete_grow(struct log *log)
{
	uint32_t n = log->old_n;
	struct machine *old_machines = log->machines;
	struct machine *new_machines = log->machines + n;
	if (machines_cmp(new_machines, old_machines, n))
		return false;

	struct machine *machines = new_machines + n;
	machines_sort_by_addr(machines, n);
	if (sorted_by_addr_duplate(machines, n))
		return false;

	machines_sort_by_addr(old_machines, n);
	uint32_t i = 0;
	uint32_t j = 0;
	while (i < n && j < n) {
		int cmp = machine_addr_cmp(old_machines + i, machines + j);
		if (cmp < 0)
			i++;
		else if (cmp > 0)
			j++;
		else
			return false;
	}
	machines_copy(old_machines, new_machines, n);

	for (uint32_t i = 0; i < n; i++)
		log_machine_init(log, machines + i);

	log->distinct_machines_n = log->new_n;
	return true;
}

bool log_complete_change(struct log *log, struct log *old, uint64_t term)
{
	__log_complete_unstable(log, old, term);

	if (log->new_n == log->old_n) {
		log->type = LOG_TYPE_ADJUST;
		return __log_complete_adjust(log, old);
	} else if (log->new_n == (log->old_n >> 1)) {
		log->type = LOG_TYPE_SHRINK;
		return __log_complete_shrink(log);
	} else if (log->new_n == (log->old_n << 1)) {
		log->type = LOG_TYPE_GROW;
		return __log_complete_grow(log);
	} else {
		return false;
	}
}

void log_complete_change_available(
		struct log *log, const struct log *old_log, uint64_t term)
{
	__log_complete_unstable(log, old_log, term);
	if (old_log->type == LOG_TYPE_OLD) {
		log->type = LOG_TYPE_CHANGE_AVAILABLE;
	} else {
		assert(old_log->type == LOG_TYPE_GROW_TRANSFORM);
		log->type = LOG_TYPE_GROW_CHANGE_AVAILABLE;
	}

	uint32_t n = log->old_n;
	struct machine *old_machines = log->machines;
	struct machine *new_machines = log->machines + n;
	assert(machines_cmp(old_machines, new_machines, n));

	struct machine *old = old_machines;
	struct machine *new = new_machines;
	while (!machine_available(old) && !machine_available(new)) {
		old++;
		new++;
	}

	bool upgrade = machine_available(old) != machine_available(new);
	for (int32_t i = n - 1; i >= 0; i--) {
		struct machine *old = old_machines + i;
		struct machine *new = new_machines + i;
		bool available = machine_available(old);
		if (machine_available(new) != available)
			upgrade = true;
		else if (available)
			upgrade = false;

		if (upgrade)
			log_machine_reset_version(log, new);
	}
	log->distinct_machines_n = n;
}

const struct machine *log_machines_find(const struct log *log, uint32_t id)
{
	return machines_find(log->machines, log->old_n + log->new_n, id);
}

const struct machine *log_machines_find_old(const struct log *log, uint32_t id)
{
	return machines_find(log->machines, log->old_n, id);
}

const struct machine *log_machines_find_new(const struct log *log, uint32_t id)
{
	return machines_find(log->machines + log->old_n, log->new_n, id);
}
