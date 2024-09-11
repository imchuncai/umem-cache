// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_KV_HASH_TABLE_H
#define __UMEM_CACHE_KV_HASH_TABLE_H

#include "kv.h"

/**
 * hash_table - A hash table for (struct kv)
 * @n: number of kvs in hash table
 * @mask: determined the size of @buckets
 * @buckets: bucket array, which size is power of 2
 * @old_buckets: if it is not NULL, the hash table is under migrating
 * @old_mask: determined the size of @old_buckets
 * @migrated: number of buckets have migrated
 * 
 * Note: we are try to keep (@mask * 2 <= @n <= @mask * 8)
 */
struct kv_hash_table {
	uint64_t n;
	uint64_t mask;
	struct hlist_head *buckets;

	struct hlist_head *old_buckets;
	uint64_t old_mask;
	uint64_t migrated;
};

bool hash_table_init(struct kv_hash_table *ht, struct memory *m);
struct kv *hash_get(const struct kv_hash_table *ht, const unsigned char *key);
void hash_add(struct kv_hash_table *ht, struct kv *kv, struct memory *m);
void hash_del(struct kv_hash_table *ht, struct kv *kv, struct memory *m);
bool hash_desire_memory(struct kv_hash_table *ht);

#endif
