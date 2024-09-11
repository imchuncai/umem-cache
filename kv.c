// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <string.h>
#include "kv.h"
#include "time.h"
#include "embed_pointer.h"

/**
 * kv_init - Initialize @kv
 */
void kv_init(struct kv *kv, const unsigned char *key, unsigned char slab_offset)
{
	hlist_head_init(&kv->ref_conn_list);
	kv->locked = false;
	kv->slab_offset = slab_offset;
	kv->hval_allocated = false;
	kv->sval_allocated = false;
	memcpy(kv->key, key, ALIGN((int)key[0] + 1, 8));
}

/**
 * kv_no_ref - Check if @kv is not referenced
 */
bool kv_no_ref(const struct kv *kv)
{
	return hlist_empty(&kv->ref_conn_list);
}

/**
 * kv_down_to_one_ref - Check if @kv is referenced only once
 * 
 * Note: caller should make sure @kv is referenced
 */
bool kv_down_to_one_ref(const struct kv *kv)
{
	assert(!kv_no_ref(kv));
	struct hlist_node *first = kv->ref_conn_list.first;
	return first->next == NULL;
}

/**
 * kv_lock - Lock @kv
 *
 * Note: if you want delete a kv, just lock it, the last conn return the kv will
 * delete it for you.
 */
void kv_lock(struct kv *kv)
{
	assert(!kv->locked);
	kv->locked = true;
}

/**
 * kv_unlock - Unlock @kv
 */
void kv_unlock(struct kv *kv)
{
	assert(kv->locked);
	kv->locked = false;
	kv->lock_expire_time = UINT64_MAX;
}

/**
 * value_migrate - Migrate a small value from @soo_from to @soo_to
 * @size: size of the data to be migrated (in bytes)
 */
void value_migrate(struct slab_obj_offset soo_from,
		   struct slab_obj_offset soo_to, uint32_t size)
{
	void *from = SOO_OBJ(soo_from);
	struct slab_obj_offset *ref = embed_pointer_get(from);
	assert(ref->x == soo_from.x);
	*ref = soo_to;

	void *to = SOO_OBJ(soo_to);
	memcpy(to, from, size);
}

/**
 * kv_lock_expired - Check whether @kv's lock is expired
 * 
 * Note: caller should make sure @kv is locked.
 */
bool kv_lock_expired(struct kv *kv)
{
	assert(kv->locked);

	if (kv->lock_expire_time == UINT64_MAX) {
		kv->lock_expire_time = timenow() + CONFIG_TCP_TIMEOUT / 1000;
		kv->lock_hit = 0;
		return false;
	}

	return ((++kv->lock_hit) & 63) == 0 && kv->lock_expire_time < timenow();
}
