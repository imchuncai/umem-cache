// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_HASH_TABLE_H
#define __UMEM_CACHE_HASH_TABLE_H

#include "memory.h"
#include "list.h"

/**
 * hash_table - A hash table for index keys
 * @n: number of keys in hash table
 * @mask: determined the size of @buckets
 * @buckets: bucket array, which size is power of 2
 * @old_buckets: if it is not NULL, the hash table is under migrating
 * @old_mask: determined the size of @old_buckets
 * @migrated: number of buckets have migrated
 * 
 * Note: we try to keep (@mask * 2 <= @n <= @mask * 8)
 */
struct hash_table {
	uint64_t n;
	uint64_t mask;
	struct hlist_head *buckets;

	struct hlist_head *old_buckets;
	uint64_t old_mask;
	uint64_t migrated;
};

bool hash_table_init(struct hash_table *ht, struct memory *m);
struct hlist_node *hash_get(const struct hash_table *ht, const unsigned char *key);
uint64_t hash_add(struct hash_table *ht, const unsigned char *key, struct memory *m);
uint64_t hash_del(struct hash_table *ht, const unsigned char *key, struct memory *m);
bool hash_grow(struct hash_table *ht, struct memory *m);
bool hash_shrink(struct hash_table *ht, struct memory *m);

#endif
