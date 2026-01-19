// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include "kv.h"
#include <string.h>

/* Note: these two fake is designed to reduce branches in migrate() */
struct list_head fake_lru;
struct hlist_head fake_hash;

void kv_init(struct kv *kv, const unsigned char *key, uint64_t val_size)
{
	kv_set_fake(kv);

	hlist_head_init(&kv->borrower_list);
	kv->val_size = val_size;
	memcpy(KV_KEY(kv), key, KEY_SIZE(key));
}

void kv_set_fake(struct kv *kv)
{
	kv->lru.prev = &fake_lru;
	kv->lru.next = &fake_lru;

	kv->hash_node.prev_next = &fake_hash.first;
	kv->hash_node.next = NULL;
}

/**
 * kv_enabled - Check if @kv is enabled for command GET
 */
bool kv_enabled(struct kv *kv)
{
	return kv->lru.prev != &fake_lru;
}

void kv_borrow(struct kv *kv, struct kv_borrower *borrower)
{
	hlist_add(&kv->borrower_list, &borrower->kv_ref_node);
	borrower->kv = kv;
}

void kv_return(struct kv_borrower *borrower)
{
	hlist_del(&borrower->kv_ref_node);
	borrower->kv = NULL;
}

/**
 * kv_is_concat - Check if @kv is concat
 * 
 * Note: this is a trick, should be replaced if there is a better solution
 */
bool kv_is_concat(struct kv *kv)
{
	return SOO_OBJ(kv->soo) != kv;
}

/**
 * kv_no_borrower - Check if @kv don't have any borrower
 */
bool kv_no_borrower(struct kv *kv)
{
	return hlist_empty(&kv->borrower_list);
}

void kv_borrower_init(struct kv_borrower *borrower)
{
	borrower->kv = NULL;
}

/**
 * kv_val_to_iovec - Map (@kv->val) to @iov for IO
 * @i: offset to the beginning of the value
 */
int kv_val_to_iovec(struct kv *kv, uint64_t i, struct iovec *iov)
{
	if (!kv_is_concat(kv)) {
		iov->iov_base = KV_VAL(kv) + i;
		iov->iov_len = kv->val_size - i;
		return 1;
	}

	struct concat_val *concat_val = SOO_OBJ(kv->soo);
	uint64_t concat_val_size = KV_SIZE(kv) & PAGE_MASK;
	uint64_t iov0_len = kv->val_size - concat_val_size;
	if (i < iov0_len) {
		iov->iov_base = KV_VAL(kv) + i;
		iov->iov_len = iov0_len - i;
		(iov+1)->iov_base = concat_val->data;
		(iov+1)->iov_len = concat_val_size;
		return 2;
	}

	iov->iov_base = concat_val->data + i - iov0_len;
	iov->iov_len = kv->val_size - i;
	return 1;
}
