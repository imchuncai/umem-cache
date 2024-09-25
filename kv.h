// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_KV_H
#define __UMEM_CACHE_KV_H

#include "mem_cache.h"
#include "bit_ops.h"

/* delta = base_size / 16 */
/* smallest delta is 16 = (1 << (4 + 4)) / 16 */
#define __SVAL_HALF2_BASE_SHIFT_MIN	(4 + 4)
#define __SVAL_HALF2_BASE_SHIFT_MAX	(PAGE_SHIFT - 1)
#define __SVAL_HALF2_DELTA_MAX		(1 << __SVAL_HALF2_BASE_SHIFT_MAX >> 4)
#define __SVAL_MAX  (ALIGN_DOWN(SLAB_OBJ_SIZE_MAX, __SVAL_HALF2_DELTA_MAX) - 8)

/* the first 8 bytes of the slab object are reserved to store the address of
(union value->small) for memory migration. */
#define KV_SMALL(kv)		((char *)SOO_OBJ((kv)->val.small) + 8)
#define __SVAL_SLAB_MIN		(24 + 8 + 8)
#define __SVAL_HALF1_SLAB_MAX	(1 << __SVAL_HALF2_BASE_SHIFT_MIN)
#define __SVAL_HALF1_NR	((__SVAL_HALF1_SLAB_MAX - __SVAL_SLAB_MIN) / 8 + 1)

/* must check in the following order */
#define IS_TINY(size)		((size) <= 48)
#define IS_SMALL_TINY(size)	((size) <= __SVAL_MAX + 24)
#define IS_HUGE_TINY(size)	(((size) & PAGE_MASK) <= 24)
#define IS_HUGE_SMALL(size)	(((size) & PAGE_MASK) <= __SVAL_MAX)
#define IS_HUGE(size)		(true)

#define HUGE_PAGE(size)		(((size) + PAGE_MASK) >> PAGE_SHIFT)
#define HUGE_TINY_PAGE(size)	((size) >> PAGE_SHIFT)
#define HUGE_SMALL_S(size)	((size) & PAGE_MASK)
#define HUGE_SMALL_H(size)	((size) & ~PAGE_MASK)
#define HUGE_SMALL_H_PAGE(size)	((size) >> PAGE_SHIFT)
#define SMALL_TINY_S(size)	((size) - 24)

#define HVAL_REQUIRED(size)	((size) > __SVAL_MAX + 24)
/* must make sure (HVAL_REQUIRED(size) == true) */
#define HVAL_PAGE(size)		(((size) - __SVAL_MAX + PAGE_MASK) >> PAGE_SHIFT)
#define HVAL_INDEX(page)	(fls64(page))
#define __HVAL_LEN(max) (HVAL_REQUIRED(max) ? HVAL_INDEX(HVAL_PAGE(max)) + 1 : 0)
#define HVAL_LEN		(__HVAL_LEN(CONFIG_VAL_SIZE_LIMIT))

/**
 * SVAL_INDEX - Get the smallest index of (struct thread->sval_cache_list) that
 * can store @size bytes of small value
 * 
 * slab object:	[40, 48, ... , 240, 248, 256][256+16, ... , __SVAL_MAX+8]
 * value:	[32, 40, ... , 232, 240, 248][248+16, ... , __SVAL_MAX]
 */
#define __SVAL_INDEX(slab_size, shift)	(				       \
	(slab_size) <= __SVAL_HALF1_SLAB_MAX ? (((slab_size)-33) >> 3) :       \
	__SVAL_HALF1_NR + (((shift) - __SVAL_HALF2_BASE_SHIFT_MIN) << 4) +     \
	((((slab_size) - 1) >> ((shift)-4)) - 16))
#define SVAL_INDEX(size)	(__SVAL_INDEX(size + 8, fls(size + 8 - 1)))
#define __SVAL_LEN(max) (						       \
	IS_TINY(max) ? 0 : (1 +	SVAL_INDEX(				       \
	IS_SMALL_TINY(max) ? SMALL_TINY_S(max) : __SVAL_MAX)))
#define SVAL_LEN		((int)__SVAL_LEN(CONFIG_VAL_SIZE_LIMIT))

/**
 * value - Union value is separated from struct kv and should not be accessed
 * independently (beware of memory migration).
 * @huge_lru: resides in huge value page_cache lru for emergency memory reclaim
 * @small_lru: resides in small value mem_cache lru for emergency memory reclaim
 * 
 * Note: see kv_val_to_iovec() for how it is organized.
 * Note: allocate memory for @huge may fail, but allocate memory for @small
 * will always success.
 */
union value {
	struct {
		union {
			struct {
				struct list_head huge_lru;
				char *huge;
			};
			char small_tiny[24];
		};
		union {
			struct {
				struct list_head small_lru;
				struct slab_obj_offset small;
			};
			char huge_tiny[24];
		};
	};
	char tiny[48];
};

static_assert(sizeof(union value) == 48);

/**
 * kv - Key value structure
 * @global_lru: resides in (struct thread->global_lru) for regular memory reclaim
 * @cache_lru: resides in kv mem_cache lru for emergency memory reclaim
 * @hlist_node: resides in hash table bucket
 * @ref_conn_list: the list of conns that reference this kv
 * @val_size: size of stored value (in bytes)
 * @lock_expire_time: it is used to avoid kv locked forever on tcp read, the
 * borrower will check this time. We can set TCP_USER_TIMEOUT for write, so tcp
 * write will not lock a kv forever. UINT64_MAX means expire time is not setted.
 * @lock_hit: count lock hit times, designed to reduce timenow() call
 * @delete: if kv is marked for deletion
 * @locked: if kv is locked for modifying. Any access to this kv will hang until
 * the lock is unlocked. Note that if a kv has no references and is still
 * locked, it will be deleted.
 * @slab_offset: used to locate the slab
 * @key: key[0] is the size of the key (in bytes), follows key[0] bytes of
 * key data. And key is 8 bytes aligned, the unused bytes is setted to 0.
 * 
 * Note: kv is owned by hash table, any command want to use it should borrow.
 * Note: (struct kv) is only referenced by (struct conn).
 */
struct kv {
	struct list_head global_lru;
	struct list_head cache_lru;
	struct hlist_node hlist_node;
	struct hlist_head ref_conn_list;
	uint64_t val_size;
	union value val;
	uint64_t lock_expire_time;
	unsigned char lock_hit;
	bool locked;
	bool delete;
	unsigned char slab_offset;
	bool hval_allocated;
	unsigned char hval_cache_i;
	bool sval_allocated;
	unsigned char sval_cache_i;
	unsigned char key[] __attribute__((aligned(8)));
	/* alignment is required by key comparison */
};

static_assert(sizeof(bool) == 1);
static_assert(HVAL_LEN < UINT8_MAX);
static_assert(SVAL_LEN < UINT8_MAX);

void kv_init(struct kv *kv, const unsigned char *key, unsigned char slab_offset);
bool kv_no_ref(const struct kv *kv);
bool kv_down_to_one_ref(const struct kv *kv);
void kv_lock(struct kv *kv);
void kv_unlock(struct kv *kv);
void kv_delete(struct kv *kv);
bool kv_lock_expired(struct kv *kv);
void value_migrate(struct slab_obj_offset soo_from,
		   struct slab_obj_offset soo_to, uint32_t size);

#endif
