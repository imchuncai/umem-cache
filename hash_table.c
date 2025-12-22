// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.
// Most of the ideas are stolen from the Go programming language.

#include "hash_table.h"
#include "murmur_hash3.h"
#include "config.h"

static_assert(sizeof(struct hlist_head) == 8);
#define PAGE_TO_MASK(page)	(((page) << (PAGE_SHIFT - 3)) - 1)
#define MASK_TO_PAGE(mask)	(((mask) + 1) >> (PAGE_SHIFT - 3))
#define MIN_MASK		PAGE_TO_MASK(1)

static const unsigned char *node_to_key(const struct hlist_node *node)
{
	return (const unsigned char *)(node + 1);
}

static struct hlist_node *key_to_node(const unsigned char *key)
{
	return (struct hlist_node *)key - 1;
}

/**
 * hash_table_init - Allocate memory for hash table @ht and initialize
 * @m: where memory allocated from
 * 
 * @return: true on success, false on failure
 */
bool hash_table_init(struct hash_table *ht, struct memory *m)
{
	struct hlist_head *buckets = memory_malloc(m, MASK_TO_PAGE(MIN_MASK));
	if (buckets) {
		ht->n = 0;
		ht->mask = MIN_MASK;
		ht->buckets = buckets;
		ht->old_buckets = NULL;
		for (int i = 0; i <= MIN_MASK; i++)
			hlist_head_init(&ht->buckets[i]);
	}
	return buckets;
}

/**
 * under_migrating - Check if @ht is under migrating
 */
static bool under_migrating(const struct hash_table *ht)
{
	return ht->old_buckets;
}

/**
 * evacuated - Check if @bucket is evacuated
 */
static bool evacuated(const struct hlist_head *bucket)
{
	return hlist_empty(bucket);
}

/**
 * key_hash - Compute hash of @key using MurmurHash3 algorithm
 */
static uint64_t key_hash(const unsigned char *key)
{
	uint64_t out[2];
	MurmurHash3_x64_128(key, (int)key[0] + 1, 47, &out);
	return out[1];
}

/**
 * key_equal - Check if @a and @b are equal
 */
static bool key_equal(const unsigned char *key_a, const unsigned char *key_b)
{
	const uint64_t *last = (const uint64_t *)(key_a + key_a[0]);
	const uint64_t *a = (const uint64_t *)key_a;
	const uint64_t *b = (const uint64_t *)key_b;
	while(a <= last && *a == *b) {
		a++;
		b++;
	}
	return a > last;
}

/**
 * hash_bucket - Get the hash bucket that @key resides
 */
static struct hlist_head *hash_bucket(
		const struct hash_table *ht, const unsigned char *key)
{
	uint64_t hkey = key_hash(key);
	if (under_migrating(ht)) {
		struct hlist_head *old_bucket;
		old_bucket = &ht->old_buckets[hkey & ht->old_mask];
		if (!evacuated(old_bucket))
			return old_bucket;
	}
	return &ht->buckets[hkey & ht->mask];
}

/**
 * hash_get - Get the hash node of @key from @ht
 * 
 * @return: the hash node or NULL if @key not exist
 */
struct hlist_node *hash_get(const struct hash_table *ht, const unsigned char *key)
{
	struct hlist_head *bucket = hash_bucket(ht, key);
	struct hlist_node *node;
	hlist_for_each(node, bucket) {
		if (key_equal(node_to_key(node), key))
			return node;
	}
	return NULL;
}

/**
 * bucket_evacuated - Check if the @i'th old_bucket has been evacuated
 */
static bool bucket_evacuated(const struct hash_table *ht, uint64_t i)
{
	return evacuated(&ht->old_buckets[i]);
}

/**
 * evacuate - Evacuate the @i'th old bucket
 */
static void evacuate(struct hash_table *ht, uint64_t i, struct memory *m)
{
	struct hlist_head *bucket = &ht->old_buckets[i];
	if (!evacuated(bucket)) {
		struct hlist_node *curr, *temp;
		hlist_for_each_safe(curr, temp, bucket) {
			// hlist_del(curr);
			const unsigned char *key = node_to_key(curr);
			uint64_t hkey = key_hash(key);
			struct hlist_head *bucket = &ht->buckets[hkey & ht->mask];
			hlist_add(bucket, curr);
		}
		hlist_head_init(bucket);
	}

	if (i == ht->migrated) {
		ht->migrated++;
		uint64_t max = ht->migrated + 1024;
		if (max > ht->old_mask)
			max = ht->old_mask + 1;

		while (ht->migrated < max && bucket_evacuated(ht, ht->migrated))
			ht->migrated++;

		if (ht->migrated > ht->old_mask) {
			memory_free(m, ht->old_buckets, MASK_TO_PAGE(ht->old_mask));
			ht->old_buckets = NULL;
		}
	}
}

/**
 * migrate_advance - Advancing the migration process
 */
static void migrate_advance(struct hash_table *ht, struct memory *m)
{
	if (under_migrating(ht))
		evacuate(ht, ht->migrated, m);
}

/**
 * migrate - Make sure @i'th old bucket is evacuated
 */
static void migrate(struct hash_table *ht, uint64_t i, struct memory *m)
{
	evacuate(ht, i, m);
	migrate_advance(ht, m);
}

/**
 * should_grow - Check if the number of buckets should be increased
 */
static bool should_grow(const struct hash_table *ht)
{
	return !under_migrating(ht) && ht->n > (ht->mask << 3);
}

static uint64_t grow_required_page(const struct hash_table *ht)
{
	return MASK_TO_PAGE(ht->mask) << 1;
}

/**
 * hash_add - Add @key to @ht
 * 
 * Note: we will not check if @key has been added before
 */
uint64_t hash_add(struct hash_table *ht, const unsigned char *key, struct memory *m)
{
	ht->n++;

	uint64_t hkey = key_hash(key);
	if (under_migrating(ht))
		migrate(ht, hkey & ht->old_mask, m);

	struct hlist_head *bucket = &ht->buckets[hkey & ht->mask];
	hlist_add(bucket, key_to_node(key));
	return should_grow(ht) ? grow_required_page(ht) : 0;
}

/**
 * should_shrink - Check if the number of buckets should be reduced
 */
static bool should_shrink(const struct hash_table *ht)
{
	return !under_migrating(ht) && ht->mask > MIN_MASK &&
		ht->n < (ht->mask << 1);
}

static uint64_t shrink_required_page(const struct hash_table *ht)
{
	return MASK_TO_PAGE(ht->mask) >> 1;
}

/**
 * hash_del - Del @key from @ht
 * 
 * Note: caller should make sure @key has been added to @ht
 */
uint64_t hash_del(struct hash_table *ht, const unsigned char *key, struct memory *m)
{
	ht->n--;
	hlist_del(key_to_node(key));
	migrate_advance(ht, m);
	return should_shrink(ht) ? shrink_required_page(ht) : 0;
}

/**
 * resize - Adjust the space usage of buckets to @page pages
 */
static bool hash_resize(struct hash_table *ht, uint64_t page, struct memory *m)
{
	void *new = memory_malloc(m, page);
	if (new) {
		ht->old_buckets = ht->buckets;
		ht->old_mask = ht->mask;
		ht->migrated = 0;
		ht->mask = PAGE_TO_MASK(page);
		ht->buckets = new;
		for (uint64_t i = 0; i <= ht->mask; i++)
			hlist_head_init(&ht->buckets[i]);
	}
	return new;
}

bool hash_grow(struct hash_table *ht, struct memory *m)
{
	return !should_grow(ht) || hash_resize(ht, grow_required_page(ht), m);
}

bool hash_shrink(struct hash_table *ht, struct memory *m)
{
	return !should_shrink(ht) || hash_resize(ht, shrink_required_page(ht), m);
}
