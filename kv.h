// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_KV_H
#define __UMEM_CACHE_KV_H

#include <sys/uio.h>
#include "slab.h"
#include "list.h"

struct concat_val {
	struct slab_obj_offset *soo_ptr;
	unsigned char data[];
};

/* memory migration requires this member shows first */
static_assert(offsetof(struct concat_val, soo_ptr) == 0);

struct kv_borrower {
	struct hlist_node kv_ref_node;
	struct kv *kv;
};

/**
 * kv -
 * @soo: it has a trick involved, see kv_malloc() and kv_is_concat()
 * @lru: kv is on lru and is ready to serve command GET if it is not empty
 * @borrower_list: the list of kv_borrower
 * @val_size: value size
 * @hash_node: resides in a hash_table, only valid on kv_enabled()
 * @data: data of key and value
 */
struct kv {
	struct slab_obj_offset soo;
	struct list_head lru;
	struct hlist_head borrower_list;
	uint64_t val_size;

	struct hlist_node hash_node;
	unsigned char data[] __attribute__((aligned(8)));
	/* alignment is required by key comparison */
};

/* this offset is required for hashtable to locate the key */
static_assert(offsetof(struct kv, data) - offsetof(struct kv, hash_node) ==
		sizeof(struct hlist_node));
/* memory migration requires this member shows first */
static_assert(offsetof(struct kv, soo) == 0);

#define KV_KEY(kv)		((kv)->data)
#define KEY_SIZE(key)		ALIGN(1 + (int)((key)[0]), 8)
#define KV_KEY_SIZE(kv)		KEY_SIZE(KV_KEY(kv))
#define KV_VAL(kv)		((kv)->data + KV_KEY_SIZE(kv))
#define KV_SIZE(kv)	(sizeof(struct kv) + KV_KEY_SIZE(kv) + (kv)->val_size)

void kv_init(struct kv *kv, const unsigned char *key, uint64_t val_size);
bool kv_enabled(struct kv *kv);
void kv_borrow(struct kv *kv, struct kv_borrower *borrower);
void kv_return(struct kv_borrower *borrower);
bool kv_is_concat(struct kv *kv);
void kv_borrower_init(struct kv_borrower *borrower);
bool kv_no_borrower(struct kv *kv);
int kv_val_to_iovec(struct kv *kv, uint64_t i, struct iovec *iov);

#endif
