// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.
// Most of the ideas are stolen from the Go programming language.

#include "kv_hash_table.h"
#include "murmur_hash3.h"

static_assert(sizeof(struct hlist_head) == 8);
#define page_to_mask(page)	(((page) << PAGE_SHIFT >> 3) - 1)
#define mask_to_page(mask)	(((mask) + 1) << 3 >> PAGE_SHIFT)
#define min_mask		page_to_mask(1)

/**
 * hash_table_init - Allocate memory for hash table @ht and initialize
 * @m: where memory taken from
 * 
 * @return: true on success, false on failure
 */
bool hash_table_init(struct kv_hash_table *ht, struct memory *m)
{
	ht->n = 0;
	ht->mask = min_mask;
	ht->buckets = memory_malloc(m, mask_to_page(min_mask));
	if (ht->buckets == NULL)
		return false;

	for (int i = 0; i <= min_mask; i++)
		hlist_head_init(&ht->buckets[i]);

	ht->old_buckets = NULL;
	return true;
}

/**
 * under_migrating - Check if @ht is under migrating
 */
static bool under_migrating(const struct kv_hash_table *ht)
{
	return ht->old_buckets != NULL;
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
	/* MurmurHash3_x64_128 has an implicit alignment requirement */
	MurmurHash3_x64_128(key, (int)key[0] + 1, 47, &out);
	return out[1];
}

/**
 * key_equal - Check if @a and @b are equal
 */
static bool key_equal(const unsigned char *key_a, const unsigned char *key_b)
{
	const unsigned char *last = key_a + key_a[0];
	while(key_a <= last && *(uint64_t *)key_a == *(uint64_t *)key_b) {
		key_a += 8;
		key_b += 8;
	}
	return key_a > last;
}

/**
 * hash_bucket - Get the hash bucket that @key resides
 */
static struct hlist_head *hash_bucket(
		const struct kv_hash_table *ht, const unsigned char *key)
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
 * hash_get - Get kv from @ht
 * 
 * @return: the kv or NULL if @key not exist
 */
struct kv *hash_get(const struct kv_hash_table *ht, const unsigned char *key)
{
	struct hlist_head *bucket = hash_bucket(ht, key);
	struct hlist_node *node;
	hlist_for_each(node, bucket) {
		struct kv *kv = container_of(node, struct kv, hlist_node);
		if (key_equal(kv->key, key))
			return kv;
	}
	return NULL;
}

/**
 * bucket_evacuated - Check if the @i'th old_bucket has been evacuated
 */
static bool bucket_evacuated(const struct kv_hash_table *ht, uint64_t i)
{
	return evacuated(&ht->old_buckets[i]);
}

/**
 * evacuate - Evacuate the @i'th old bucket
 */
static void evacuate(struct kv_hash_table *ht, uint64_t i, struct memory *m)
{
	struct hlist_head *bucket = &ht->old_buckets[i];
	if (!evacuated(bucket)) {
		struct hlist_node *curr, *temp;
		hlist_for_each_safe(curr, temp, bucket) {
			struct kv *kv = container_of(curr, struct kv, hlist_node);
			// hlist_del(&kv->hlist_node);
			uint64_t hkey = key_hash(kv->key);
			struct hlist_head *bucket = &ht->buckets[hkey & ht->mask];
			hlist_add(bucket, &kv->hlist_node);
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

		if (ht->migrated > ht->old_mask){
			memory_free(m, ht->old_buckets, mask_to_page(ht->old_mask));
			ht->old_buckets = NULL;
		}
	}
}

static void migrate(struct kv_hash_table *ht, uint64_t i, struct memory *m) {
	evacuate(ht, i, m);
	if (under_migrating(ht))
		evacuate(ht, ht->migrated, m);
}

/**
 * resize - Adjust the space usage of bucket to @page pages
 */
static void resize(struct kv_hash_table *ht, uint64_t page, struct memory *m)
{
	void *new = memory_malloc(m, page);
	if (new == NULL)
		return;

	ht->old_buckets = ht->buckets;
	ht->old_mask = ht->mask;
	ht->migrated = 0;
	ht->mask = page_to_mask(page);
	ht->buckets = new;
}

/**
 * should_grow - Check if the number of buckets should be increased
 */
static bool should_grow(struct kv_hash_table *ht)
{
	return !under_migrating(ht) && ht->n > (ht->mask << 3);
}

/**
 * grow - Increase the number of buckets
 */
static void grow(struct kv_hash_table *ht, struct memory *m)
{
	assert(should_grow(ht));
	uint64_t page = mask_to_page(ht->mask) << 1;
	resize(ht, page, m);
}

/**
 * hash_add - Add @kv to @ht
 * 
 * Note: we will not check if @kv has been added before
 */
void hash_add(struct kv_hash_table *ht, struct kv *kv, struct memory *m)
{
	ht->n++;

	uint64_t hkey = key_hash(kv->key);
	if (under_migrating(ht))
		migrate(ht, hkey & ht->old_mask, m);

	struct hlist_head *bucket = &ht->buckets[hkey & ht->mask];
	hlist_add(bucket, &kv->hlist_node);
	if (should_grow(ht))
		grow(ht, m);
}

/**
 * should_shrink - Check if the number of buckets should be reduced
 */
static bool should_shrink(struct kv_hash_table *ht)
{
	return !under_migrating(ht) && ht->mask > min_mask &&
	       ht->n < (ht->mask << 1);
}

/**
 * shrink - Reduce the number of buckets
 */
static void shrink(struct kv_hash_table *ht, struct memory *m)
{
	assert(should_shrink(ht));
	uint64_t page = mask_to_page(ht->mask) >> 1;
	resize(ht, page, m);
}

/**
 * hash_del - Del @kv from @ht
 * 
 * Note: caller should make sure @kv has been added to @ht
 */
void hash_del(struct kv_hash_table *ht, struct kv *kv, struct memory *m)
{
	ht->n--;
	hlist_del(&kv->hlist_node);

	if (under_migrating(ht)) {
		uint64_t hkey = key_hash(kv->key);
		migrate(ht, hkey & ht->old_mask, m);
	}

	if (should_shrink(ht))
		shrink(ht, m);
}

/**
 * hash_desire_memory - Check if @ht desires memory for resizing
 */
bool hash_desire_memory(struct kv_hash_table *ht)
{
	return should_grow(ht) || should_shrink(ht);
}
