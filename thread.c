// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "thread.h"
#include "embed_pointer.h"
#include "encoding.h"
#include "debug.h"

static void conn_blocked(struct thread *t, struct conn *conn)
{
	t->unblocked_conn_nr--;
	conn->blocked = true;
}

static void conn_unblocked(struct thread *t, struct conn *conn)
{
	if (!conn->active) {
		pthread_mutex_lock(&t->mu);
		hlist_del(&conn->thread_node);
		pthread_mutex_unlock(&t->mu);

		hlist_add(&t->active_conn_list, &conn->thread_node);
		conn->active = true;

		t->unblocked_conn_nr++;
		conn->blocked = false;
	} else if (conn->blocked){
		t->unblocked_conn_nr++;
		conn->blocked = false;
	}
}

/**
 * kv_sval_free - Free @kv's small value (if allocated)
 */
static void kv_sval_free(struct thread *t, struct kv *kv)
{
	if (!kv->sval_allocated)
		return;

	kv->sval_allocated = false;
	list_lru_del(&kv->val.small_lru);
	struct mem_cache *cache = &t->sval_cache_list[kv->sval_cache_i];
	mem_cache_free(cache, kv->val.small, &t->memory, value_migrate);
}

/**
 * kv_hval_free - Free @kv's huge value (if allocated)
 */
static void kv_hval_free(struct thread *t, struct kv *kv)
{
	if (!kv->hval_allocated)
		return;

	kv->hval_allocated = false;
	list_lru_del(&kv->val.huge_lru);
	struct page_cache *cache = &t->hval_cache_list[kv->hval_cache_i];
	page_cache_free(cache, kv->val.huge, HVAL_PAGE(kv->val_size), &t->memory);
}

/**
 * kv_val_free - Free @kv's value
 */
static void kv_val_free(struct thread *t, struct kv *kv)
{
	kv_sval_free(t, kv);
	kv_hval_free(t, kv);
}

/**
 * kv_cache - Get the (struct kv) cache for @key
 */
static struct mem_cache *kv_cache(struct thread *t, const unsigned char *key)
{
	return &t->kv_cache_list[key[0] >> 3];
}

/**
 * kv_clear - Clear everything related to @kv, make @kv ready to reuse or free
 * 
 * Note: caller should make sure @kv is not referenced
 */
static void kv_clear(struct thread *t, struct kv *kv)
{
	assert(kv_no_ref(kv));

	list_del(&kv->global_lru);
	list_del(&kv->cache_lru);
	hash_del(&t->hash_table, kv, &t->memory);
	kv_val_free(t, kv);
}

static void __list_fix(struct list_head *head)
{
	head->prev->next = head;
	head->next->prev = head;
}

static void __hlist_node_fix(struct hlist_node *node)
{
	*(node->pprev) = node;
	if (node->next)
		node->next->pprev = &node->next;
}

/**
 * kv_migrate - Migrate kv from @soo_from to @soo_to
 * @size: size of the data to be migrated (in bytes)
 */
static void kv_migrate(struct slab_obj_offset soo_from,
		       struct slab_obj_offset soo_to, uint32_t size)
{
	struct kv *from = SOO_OBJ(soo_from);
	struct kv *to = SOO_OBJ(soo_to);
	memcpy(to, from, size);

	__list_fix(&to->global_lru);
	__list_fix(&to->cache_lru);
	__hlist_node_fix(&to->hlist_node);

	if (!hlist_empty(&to->ref_conn_list)) {
		struct hlist_node *first = to->ref_conn_list.first;
		first->pprev = &to->ref_conn_list.first;

		struct hlist_node *curr;
		hlist_for_each(curr, &to->ref_conn_list) {
			struct conn *conn;
			conn = container_of(curr, struct conn, kv_ref_node);
			conn->kv = to;
		}
	}

	to->slab_offset = SOO_OFFSET(soo_to);

	if (to->hval_allocated)
		__list_fix(&to->val.huge_lru);

	if (to->sval_allocated) {
		embed_pointer(SOO_OBJ(to->val.small), &to->val.small);
		__list_fix(&to->val.small_lru);
	}
}

/**
 * kv_free - Deallocates the space related to @kv
 */
static void kv_free(struct thread *t, struct kv *kv)
{
	kv_clear(t, kv);
	struct slab_obj_offset soo = { (unsigned long)kv | kv->slab_offset };
	struct mem_cache *cache = kv_cache(t, kv->key);
	mem_cache_free(cache, soo, &t->memory, kv_migrate);
}

/**
 * kv_touch - Touch @kv by update lru
 */
static void kv_touch(struct thread *t, struct kv *kv)
{
	list_lru_touch(&t->global_lru, &kv->global_lru);
	struct mem_cache *cache = kv_cache(t, kv->key);
	list_lru_touch(&cache->lru, &kv->cache_lru);

	if (kv->hval_allocated) {
		struct page_cache *cache = &t->hval_cache_list[kv->hval_cache_i];
		list_lru_touch(&cache->lru, &kv->val.huge_lru);
	}

	if (kv->sval_allocated) {
		struct mem_cache *cache = &t->sval_cache_list[kv->sval_cache_i];
		list_lru_touch(&cache->lru, &kv->val.small_lru);
	}
}

/**
 * kv_return - Return the kv that @conn holds
 */
static void kv_return(struct thread *t, struct conn *conn)
{
	assert(conn->kv);
	hlist_del(&conn->kv_ref_node);

	/**
	 * A conn locked the kv and returned without unlocking it, either due
	 * to a socket error or intentionally. The kv may not be touched, free
	 * it anyway.
	 */
	if (conn->kv->locked && kv_no_ref(conn->kv))
		kv_free(t, conn->kv);

	conn->kv = NULL;
}

/**
 * free_conn - Close @conn and free
 */
static void free_conn(struct thread *t, struct conn *conn)
{
	debug_printf("free conn:\n");
	if (!conn->blocked)
		t->unblocked_conn_nr--;

	pthread_mutex_lock(&t->mu);
	t->conn_nr--;
	hlist_del(&conn->thread_node);
	pthread_mutex_unlock(&t->mu);

	if (conn->kv)
		kv_return(t, conn);
	conn_free(conn);
}

/**
 * thread_add_conn - Add @conn to @t
 * 
 * @return: true on success, false on too many connections
 */
bool thread_add_conn(struct thread *t, struct conn *conn)
{
	pthread_mutex_lock(&t->mu);
	if (t->conn_nr >= CONFIG_MAX_CONN_PER_THREAD) {
		pthread_mutex_unlock(&t->mu);
		return false;
	}

	t->conn_nr++;
	hlist_add(&t->conn_list, &conn->thread_node);
	pthread_mutex_unlock(&t->mu);

	conn->active = false;
	conn->state = CONN_STATE_OUT_ERRNO;
	conn->kv = NULL;
	conn->buffer[0] = E_NONE;

	struct epoll_event event;
	event.events = EPOLLIN | EPOLLOUT | EPOLLET;
	event.data.ptr = conn;
	int ret = epoll_ctl(t->epfd, EPOLL_CTL_ADD, conn->sockfd, &event);
	if (ret == 0)
		return true;

	pthread_mutex_lock(&t->mu);
	t->conn_nr--;
	hlist_del(&conn->thread_node);
	pthread_mutex_unlock(&t->mu);
	return false;
}

/**
 * kv_force_free - Free @kv and free conns that has reference to it
 */
static void kv_force_free(struct thread *t, struct kv *kv)
{
	if (kv_no_ref(kv)) {
		kv_free(t, kv);
		return;
	}

	if (!kv->locked)
		kv_lock(kv);

	struct hlist_node *curr, *temp;
	hlist_for_each_safe(curr, temp, &kv->ref_conn_list) {
		struct conn *conn;
		conn = container_of(curr, struct conn, kv_ref_node);
		free_conn(t, conn);
	}
}

/**
 * kv_borrow - Borrow kv for @conn
 * 
 * @return: true on success, false on borrow will be blocked
 *
 * Note: caller should make sure key is readed from client
 * Note: must call kv_return() when done using the kv
 * Note: NULL is borrowed if key not exist
 */
static bool kv_borrow(struct thread *t, struct conn *conn)
{
	assert(conn->kv == NULL);
	debug_printf("kv_borrow key: %u\n", conn->key_n);

	struct kv *kv = hash_get(&t->hash_table, &conn->key_n);
	if (kv == NULL){
	} else if (kv->locked) {
		if (!kv_lock_expired(kv))
			return false;

		kv_force_free(t, kv);
	} else {
		conn_ref_kv(conn, kv);
		kv_touch(t, kv);
	}
	return true;
}

static void grow_reclaim_bytes(struct thread *t)
{
	t->reclaim_bytes += CONFIG_RECLAIM_GROWTH;
}

static void decay_reclaim_bytes(struct thread *t)
{
	t->reclaim_bytes -= CONFIG_RECLAIM_DECAY;
}

/**
 * __kv_evict - Make room for storing new kv
 * 
 * @return: the space evicted to store new kv
 * 
 * Note: caller should make sure have tried allocate from @cache and failed.
 * Note: this can cause small value memory migrate, don't use old reference.
 */
static struct slab_obj_offset __kv_evict(struct thread *t, struct mem_cache *cache)
{
	grow_reclaim_bytes(t);

	/* make sure there always at least one kv is not referenced */
	static_assert(CONFIG_CACHE_MIN_OBJ_NR >= CONFIG_MAX_CONN_PER_THREAD);

	struct list_head *curr;
	list_lru_for_each(curr, &cache->lru) {
		struct kv *kv = container_of(curr, struct kv, cache_lru);
		if (kv_no_ref(kv)) {
			struct slab_obj_offset soo;
			soo.x = (unsigned long)kv | kv->slab_offset;
			kv_clear(t, kv);
			return soo;
		}
	}
	__builtin_unreachable();
}

/**
 * kv_new_and_borrow - Create new kv and borrow it for @conn
 * 
 * Note: caller should make sure key is readed from client
 * Note: caller should make sure kv is not exist
 * Note: must call kv_return() when done using the kv
 */
static void kv_new_and_borrow(struct thread *t, struct conn *conn)
{
	debug_printf("kv_new_and_borrow key: %u\n", conn->key_n);
	struct mem_cache *cache = kv_cache(t, &conn->key_n);
	struct slab_obj_offset soo = mem_cache_malloc(cache, &t->memory);
	if (soo.x == 0)
		soo = __kv_evict(t, cache);

	struct kv *kv = SOO_OBJ(soo);
	kv_init(kv, &conn->key_n, SOO_OFFSET(soo));
	list_lru_add(&t->global_lru, &kv->global_lru);
	list_lru_add(&cache->lru, &kv->cache_lru);
	hash_add(&t->hash_table, kv, &t->memory);
	conn_ref_kv(conn, kv);
}

/**
 * __sval_evict - Make room for storing new small value
 * 
 * Note: caller should make sure have tried allocate from @cache and failed.
 * Note: this can cause struct kv and small value memory migrate, don't use
 * old reference.
 */
static void __sval_evict(struct thread *t, struct mem_cache *cache)
{
	grow_reclaim_bytes(t);

	struct list_head *curr;
	list_lru_for_each(curr, &cache->lru) {
		union value *val = container_of(curr, union value, small_lru);
		struct kv *kv = container_of(val, struct kv, val);
		if (kv_no_ref(kv)) {
			kv_free(t, kv);
			return;
		}
	}
	__builtin_unreachable();
}

/**
 * sval_malloc - Allocate space for small value of @conn->kv
 * @size: size of small value (in bytes)
 */
static void sval_malloc(struct thread *t, struct conn *conn, unsigned int size)
{
	conn->kv->sval_cache_i = SVAL_INDEX(size);
	struct mem_cache *cache = &t->sval_cache_list[conn->kv->sval_cache_i];
	struct slab_obj_offset soo = mem_cache_malloc(cache, &t->memory);
	if (soo.x == 0) {
		__sval_evict(t, cache);
		soo = mem_cache_malloc(cache, &t->memory);
	}
	assert(soo.x != 0);

	conn->kv->sval_allocated = true;
	conn->kv->val.small = soo;
	embed_pointer(SOO_OBJ(soo), &conn->kv->val.small);
	list_lru_add(&cache->lru, &conn->kv->val.small_lru);
}

/**
 * __hval_evict - Make room for storing new huge value
 * 
 * Note: caller should make sure have tried allocate from @cache and failed.
 * Note: this can cause struct kv and small value memory migrate, don't use
 * old reference.
 */
static void __hval_evict(struct thread *t, struct page_cache *cache)
{
	grow_reclaim_bytes(t);

	struct list_head *curr;
	list_lru_for_each(curr, &cache->lru) {
		union value *val = container_of(curr, union value, huge_lru);
		struct kv *kv = container_of(val, struct kv, val);
		if (kv_no_ref(kv)) {
			kv_free(t, kv);
			return;
		}
	}
	__builtin_unreachable();
}

/**
 * hval_malloc - Allocate space for huge value of @conn->kv
 * @page: size of huge value (in pages)
 */
static bool hval_malloc(struct thread *t, struct conn *conn, uint64_t page)
{
	conn->kv->hval_cache_i = HVAL_INDEX(page);
	struct page_cache *cache = &t->hval_cache_list[conn->kv->hval_cache_i];
	void *ptr = page_cache_malloc(cache, page, &t->memory);
	if (ptr == NULL) {
		if (page_cache_is_malloc_from_reserved(cache, page))
			return false;

		/* 2 evictions are enough to make room for the next allocation */
		__hval_evict(t, cache);
		if (!page_cache_is_malloc_from_reserved(cache, page))
			__hval_evict(t, cache);

		ptr = page_cache_malloc(cache, page, &t->memory);
		if (ptr == NULL)
			return false;
	}

	conn->kv->hval_allocated = true;
	conn->kv->val.huge = ptr;
	list_lru_add(&cache->lru, &conn->kv->val.huge_lru);
	return true;
}

/**
 * value_malloc - Allocate space for @conn->kv
 *
 * Note: caller should make sure value size is readed from client
 * Note: caller should make sure old value is freed.
 * Note: @conn->kv may changed due to memory migration.
 */
static bool value_malloc(struct thread *t, struct conn *conn)
{
	assert(conn->kv->hval_allocated == false);
	assert(conn->kv->sval_allocated == false);

	uint64_t size = conn->val_size;
	conn->kv->val_size = size;

	if (HVAL_REQUIRED(size)) {
		uint64_t page = HVAL_PAGE(size);
		if (!hval_malloc(t, conn, page))
			return false;

		uint64_t huge_size = page << PAGE_SHIFT;
		if (size > huge_size + 24)
			sval_malloc(t, conn, size - huge_size);
	} else if (size > 48) {
		sval_malloc(t, conn, size - 24);
	}
	return true;
}

static void set_iov(struct iovec *msg_iov, void *iov_base, size_t iov_len)
{
	msg_iov->iov_base = iov_base;
	msg_iov->iov_len = iov_len;
}

static int set_iov1(struct iovec *msg_iov, uint64_t size, uint64_t i, char *base)
{
	set_iov(msg_iov, &base[i], size - i);
	return 1;
}

static int set_iov2(struct iovec *msg_iov, uint64_t size, uint64_t i,
			char *base0, uint64_t size0, char *base1)
{
	if (i < size0) {
		set_iov(msg_iov, &base0[i], size0 - i);
		set_iov(msg_iov + 1, base1, size - size0);
		return 2;
	}

	set_iov(msg_iov, base1 + i - size0, size - i);
	return 1;
}

/**
 * kv_val_to_iovec - Map (@kv->val) to @iov for IO
 */
static int kv_val_to_iovec(struct kv *kv, uint64_t i, struct iovec *iov)
{
	uint64_t size = kv->val_size;
	if (IS_TINY(size))
		return set_iov1(iov, size, i, kv->val.tiny);
	else if (IS_SMALL_TINY(size))
		return set_iov2(iov, size, i, kv->val.small_tiny, 24, KV_SMALL(kv));
	else if (IS_HUGE_TINY(size))
		return set_iov2(iov, size, i, kv->val.huge_tiny, 24, kv->val.huge);
	else if (IS_HUGE_SMALL(size))
		return set_iov2(iov, size, i, kv->val.huge, HUGE_SMALL_H(size), KV_SMALL(kv));
	else
		return set_iov1(iov, size, i, kv->val.huge);
}

/**
 * __blocked_or_freed - Check if @conn is blocked or **freed**
 * @n: the return value of the read() or write() system call
 */
static bool __blocked_or_freed(struct thread *t, struct conn *conn, ssize_t n)
{
	if (n > 0)
		return false;

	if (n == -1 && errno == EWOULDBLOCK)
		conn_blocked(t, conn);
	else
		free_conn(t, conn);

	return true;
}

/**
 * __io_done -  Check if IO is done
 * @msg: the IO message
 * @n: the number of bytes transferred
 * 
 * Note: @msg will be modified for next IO
 */
static bool __io_done(struct msghdr *msg, size_t n)
{
	for (size_t i = 0; i < msg->msg_iovlen; i++) {
		struct iovec *iov = &msg->msg_iov[i];

		if (iov->iov_len > n) {
			iov->iov_base = (char *)(iov->iov_base) + n;
			iov->iov_len -= n;
			return false;
		}

		n -= iov->iov_len;
		iov->iov_len = 0;
	}
	return true;
}

/**
 * conn_full_read - Full read from @conn
 * 
 * @return: true on success, false on @conn is blocked or **freed**
 * 
 * Note: caller should make sure @conn->unread is setted.
 */
static bool conn_full_read(struct thread *t, struct conn *conn,
			   struct iovec *iov, size_t iovlen)
{
	struct msghdr msg = {};
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	ssize_t n;
	do {
		assert(conn->unread > 0);
		n = recvmsg(conn->sockfd, &msg, 0);
		if (__blocked_or_freed(t, conn, n))
			return false;

		assert(conn->unread >= (size_t)n);
		conn->unread -= n;
	} while (!__io_done(&msg, n));

	return true;
}

/**
 * conn_full_write - Full write to @conn
 * 
 * @return: true on success, false on @conn is blocked or **freed**
 * 
 * Note: caller should make sure @conn->unwrite is setted.
 */
static bool conn_full_write(struct thread *t, struct conn *conn,
			    struct iovec *iov, size_t iovlen)
{
	struct msghdr msg = {};
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	ssize_t n;
	do {
		assert(conn->unwrite > 0);
		n = sendmsg(conn->sockfd, &msg, MSG_NOSIGNAL);
		if (__blocked_or_freed(t, conn, n))
			return false;

		assert(conn->unwrite >= (size_t)n);
		conn->unwrite -= n;
	} while (!__io_done(&msg, n));

	return true;
}

/**
 * conn_buffer_full_read - Full read from @conn to @conn->buffer
 * @sum: the total size to read (in bytes)
 * 
 * @return: true on success, false on @conn is blocked or **freed**
 * 
 * Note: caller should make sure @conn->unread is setted.
 */
static bool conn_buffer_full_read(struct thread *t, struct conn *conn, int sum)
{
	do {
		assert(conn->unread > 0);
		int readed = sum - conn->unread;
		ssize_t n;
		n = read(conn->sockfd, conn->buffer + readed, conn->unread);
		if (__blocked_or_freed(t, conn, n))
			return false;

		assert(conn->unread >= (size_t)n);
		conn->unread -= n;
	} while (conn->unread > 0);

	return true;
}

/**
 * conn_buffer_full_write - Full write from @conn->buffer to @conn
 * @sum: the total size to write (in bytes)
 * 
 * @return: true on success, false on @conn is blocked or **freed**
 * 
 * Note: caller should make sure @conn->unwrite is setted.
 */
static bool conn_buffer_full_write(struct thread *t, struct conn *conn, int sum)
{
	do {
		assert(conn->unwrite > 0);
		int written = sum - conn->unwrite;
		ssize_t n = send(conn->sockfd, conn->buffer + written,
				 conn->unwrite, MSG_NOSIGNAL);
		if (__blocked_or_freed(t, conn, n))
			return false;

		assert(conn->unwrite >= (size_t)n);
		conn->unwrite -= n;
	} while (conn->unwrite > 0);

	return true;
}

/**
 * conn_full_write_byte - Full write @ch to @conn
 * 
 * @return: true on success, false on @conn is blocked or **freed**
 */
static bool conn_full_write_byte(struct thread *t, struct conn *conn, char ch)
{
	ssize_t n = send(conn->sockfd, &ch, 1, MSG_NOSIGNAL);
	return !__blocked_or_freed(t, conn, n);
}

/**
 * __state_in_key - Read key from @conn
 * 
 * @return: true on success, false on @conn is blocked or **freed**
 */
static bool __state_in_key(struct thread *t, struct conn *conn)
{
	return conn->key_n == 0 || conn_buffer_full_read(t, conn, conn->key_n);
}

/**
 * __state_lock_kv - Borrow and lock kv
 * 
 * @return: true on success, false on kv is locked
 * 
 * Note: kv will be created if it is not exist.
 */
static bool __state_lock_kv(struct thread *t, struct conn *conn)
{
	if (!kv_borrow(t, conn))
		return false;

	if (conn->kv == NULL)
		kv_new_and_borrow(t, conn);

	kv_lock(conn->kv);
	return true;
}

static void state_change_to_in_cmd(struct conn *conn)
{
	assert(conn->kv == NULL);
	conn->state = CONN_STATE_IN_CMD;
	conn->unread = CONN_CMD_SIZE;
	/* don't call state_in_cmd() or other connections can be starved */
}

/**
 * state_out_errno - Write 1 byte errno from @conn->buffer to @conn
 */
static void state_out_errno(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_OUT_ERRNO: %d\n", conn->buffer[0]);

	if (conn_full_write_byte(t, conn, conn->buffer[0]))
		state_change_to_in_cmd(conn);
}

static void state_change_to_out_errno(
		struct thread *t, struct conn *conn, enum umem_cache_errno e)
{
	conn->state = CONN_STATE_OUT_ERRNO;
	conn->buffer[0] = (char)e;
	state_out_errno(t, conn);
}

/**
 * state_discard_value - Discard value and out errno
 * 
 * Note: caller should make sure errno is setted.
 */
static void state_discard_value(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_DISCARD_VALUE:\n");

	while (conn->unread > 0) {
		char trash[conn->unread];
		struct iovec iov = {trash, conn->unread};
		if (!conn_full_read(t, conn, &iov, 1))
			return;
	}

	conn->state = CONN_STATE_OUT_ERRNO;
	state_out_errno(t, conn);
}

static void state_change_to_discard_value(
		struct thread *t, struct conn *conn, enum umem_cache_errno e)
{
	conn->state = CONN_STATE_DISCARD_VALUE;
	conn->unread = conn->val_size;
	conn->buffer[0] = (char)e;
	state_discard_value(t, conn);
}

static void state_get_out_errno(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_OUT_ERRNO: %d\n", conn->buffer[0]);

	if (conn_buffer_full_write(t, conn, CONN_VAL_SIZE))
		state_change_to_in_cmd(conn);
}

static void state_change_to_get_out_errno(
		struct thread *t, struct conn *conn, enum umem_cache_errno e)
{
	conn->state = CONN_STATE_GET_OUT_ERRNO;
	conn->unwrite = CONN_VAL_SIZE;
	conn->buffer[0] = (char)e;
	state_get_out_errno(t, conn);
}

static void time_limit_state_in_value(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_TIME_LIMIT_STATE_IN_VALUE:\n");

	if (conn->kv->val_size > 0) {
		uint64_t readed = conn->kv->val_size - conn->unread;
		struct iovec iov[2];
		int iov_len = kv_val_to_iovec(conn->kv, readed, iov);
		if (!conn_full_read(t, conn, iov, iov_len))
			return;
	}

	kv_unlock(conn->kv);
	kv_return(t, conn);
	state_change_to_out_errno(t, conn, E_NONE);
}

static void state_change_to_in_value(struct thread *t, struct conn *conn)
{
	conn->state = CONN_TIME_LIMIT_STATE_IN_VALUE;
	conn->unread = conn->kv->val_size;
	time_limit_state_in_value(t, conn);
}

static void time_limit_state_in_value_size(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_TIME_LIMIT_STATE_IN_VALUE_SIZE:\n");

	if (!conn_buffer_full_read(t, conn, CONN_VAL_SIZE))
		return;

	unsigned char set = conn->buffer[0];
	if (!set) {
		assert(conn->kv->locked);
		kv_return(t, conn);
		state_change_to_out_errno(t, conn, E_NONE);
		return;
	}

	conn->val_size = big_endian_uint64(conn->buffer + 1);
	if (conn->val_size > CONFIG_VAL_SIZE_LIMIT) {
		free_conn(t, conn);
		return;
	}

	if (value_malloc(t, conn)) {
		state_change_to_in_value(t, conn);
	} else {
		assert(conn->kv->locked);
		kv_return(t, conn);
		state_change_to_discard_value(t, conn, E_NOMEM);
	}
}

static void state_change_to_in_value_size(struct thread *t, struct conn *conn)
{
	conn->state = CONN_TIME_LIMIT_STATE_IN_VALUE_SIZE;
	conn->unread = CONN_VAL_SIZE;
	time_limit_state_in_value_size(t, conn);
}

static bool cmd_set_non_block(struct conn *conn)
{
	return !!(conn->cmd_flag & CONN_SET_FLAG_NON_BLOCK);
}

static void state_set_lock_wait(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_SET_LOCK_WAIT:\n");

	if (!kv_down_to_one_ref(conn->kv)) {
		if (cmd_set_non_block(conn)) {
			kv_unlock(conn->kv);
			kv_return(t, conn);
			state_change_to_discard_value(t, conn, E_SET_WILL_BLOCK);
		}
		return;
	}

	kv_val_free(t, conn->kv);
	if (value_malloc(t, conn)) {
		state_change_to_in_value(t, conn);
	} else {
		assert(conn->kv->locked);
		kv_return(t, conn);
		state_change_to_discard_value(t, conn, E_SET_NOMEM);
	}
}

static void state_set_lock_kv(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_SET_LOCK_KV:\n");

	if (!__state_lock_kv(t, conn)) {
		if (cmd_set_non_block(conn))
			state_change_to_discard_value(t, conn, E_SET_WILL_BLOCK);
		return;
	}

	conn->state = CONN_STATE_SET_LOCK_WAIT;
	state_set_lock_wait(t, conn);
}

static void state_set_in_key(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_SET_IN_KEY:\n");

	if (__state_in_key(t, conn)) {
		conn->state = CONN_STATE_SET_LOCK_KV;
		state_set_lock_kv(t, conn);
	}
}

static void state_get_out_hit(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_OUT_HIT: %lu\n", conn->kv->val_size);

	uint64_t written = CONN_VAL_SIZE + conn->kv->val_size - conn->unwrite;
	struct iovec iov[3];
	uint64_t iov_len;
	if (written < CONN_VAL_SIZE) {
		iov[0].iov_base = conn->buffer + written;
		iov[0].iov_len = CONN_VAL_SIZE - written;
		iov_len = 1 + kv_val_to_iovec(conn->kv, 0, iov + 1);
	} else {
		uint64_t i = conn->kv->val_size - conn->unwrite;
		iov_len = kv_val_to_iovec(conn->kv, i, iov);
	}
	if (!conn_full_write(t, conn, iov, iov_len))
		return;

	kv_return(t, conn);
	state_change_to_in_cmd(conn);
}

static void state_get_set_out_miss(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_SET_OUT_MISS:\n");

	if (conn_buffer_full_write(t, conn, CONN_VAL_SIZE))
		state_change_to_in_value_size(t, conn);
}

static bool cmd_get_non_block(struct conn *conn)
{
	return !!(conn->cmd_flag & CONN_GET_FLAG_NON_BLOCK);
}

static bool cmd_get_or_set(struct conn *conn)
{
	return !!(conn->cmd_flag & CONN_GET_FLAG_SET_ON_MISS);
}

static void state_get_wait_lock(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_WAIT_LOCK:\n");

	if (!kv_borrow(t, conn)) {
		if (cmd_get_non_block(conn))
			state_change_to_get_out_errno(t, conn, E_GET_WILL_BLOCK);
		return;
	}

	if (conn->kv) {
		conn->state = CONN_STATE_GET_OUT_HIT;
		conn->unwrite = CONN_VAL_SIZE + conn->kv->val_size;
		conn->buffer[0] = E_NONE;
		big_endian_put_uint64(conn->buffer + 1, conn->kv->val_size);
		state_get_out_hit(t, conn);
	} else if (cmd_get_or_set(conn)){
		kv_new_and_borrow(t, conn);
		kv_lock(conn->kv);

		conn->state = CONN_STATE_GET_SET_OUT_MISS;
		conn->unwrite = CONN_VAL_SIZE;
		conn->buffer[0] = E_GET_MISS;
		state_get_set_out_miss(t, conn);
	} else {
		state_change_to_get_out_errno(t, conn, E_GET_MISS);
	}
}

static void state_get_in_key(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_IN_KEY:\n");

	if (__state_in_key(t, conn)) {
		conn->state = CONN_STATE_GET_WAIT_LOCK;
		state_get_wait_lock(t, conn);
	}
}

static bool cmd_del_non_block(struct conn *conn)
{
	return !!(conn->cmd_flag & CONN_DEL_FLAG_NON_BLOCK);
}

static void state_del_lock_kv(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_DEL_LOCK_KV:\n");

	if (!kv_borrow(t, conn)) {
		if (cmd_del_non_block(conn))
			state_change_to_out_errno(t, conn, E_DEL_WILL_BLOCK);
		return;
	}

	if(conn->kv) {
		kv_lock(conn->kv);
		kv_return(t, conn);
	}
	state_change_to_out_errno(t, conn, E_NONE);
}

static void state_del_in_key(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_DEL_IN_KEY:\n");

	if (__state_in_key(t, conn)) {
		conn->state = CONN_STATE_DEL_LOCK_KV;
		state_del_lock_kv(t, conn);
	}
}

static void state_in_cmd(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_IN_CMD: ..........................\n");

	if (!conn_buffer_full_read(t, conn, CONN_CMD_SIZE))
		return;

	char cmd = conn->buffer[0];
	conn->cmd_flag = conn->buffer[1];
	unsigned char key_n = conn->buffer[2];
	conn->val_size = big_endian_uint64(conn->buffer + 3);
	if (conn->val_size > CONFIG_VAL_SIZE_LIMIT) {
		free_conn(t, conn);
		return;
	}

	/* zero last uint64_t of key for easy key comparison */
	uint64_t *last;
	last = (uint64_t *)ALIGN_DOWN((unsigned long)(&conn->key_n) + key_n, 8);
	*last = 0;
	conn->key_n = key_n;

	conn->unread = key_n;
	switch (cmd) {
	case CONN_CMD_GET:
		debug_printf("CONN_CMD_GET: key_n: %u\n", conn->key_n);
		conn->state = CONN_STATE_GET_IN_KEY;
		state_get_in_key(t, conn);
		break;

	case CONN_CMD_SET:
		debug_printf("CONN_CMD_SET: key_n: %u val_size: %lu\n",
				conn->key_n, conn->val_size);
		conn->state = CONN_STATE_SET_IN_KEY;
		state_set_in_key(t, conn);
		break;

	case CONN_CMD_DEL:
		debug_printf("CONN_CMD_DEL:  key_n: %u\n", conn->key_n);
		conn->state = CONN_STATE_DEL_IN_KEY;
		state_del_in_key(t, conn);
		break;

	default:
		debug_printf("command not found: %d\n", cmd);
		free_conn(t, conn);
	}
}

static void process_conn(struct thread *t, struct conn *conn)
{
	switch (conn->state) {
	case CONN_STATE_IN_CMD:
		state_in_cmd(t, conn);
		break;

	case CONN_STATE_GET_IN_KEY:
		state_get_in_key(t, conn);
		break;
	case CONN_STATE_GET_WAIT_LOCK:
		state_get_wait_lock(t, conn);
		break;
	case CONN_STATE_GET_OUT_ERRNO:
		state_get_out_errno(t, conn);
		break;
	case CONN_STATE_GET_OUT_HIT:
		state_get_out_hit(t, conn);
		break;

	case CONN_STATE_GET_SET_OUT_MISS:
		state_get_set_out_miss(t, conn);
		break;

	case CONN_STATE_SET_IN_KEY:
		state_set_in_key(t, conn);
		break;
	case CONN_STATE_SET_LOCK_KV:
		state_set_lock_kv(t, conn);
		break;
	case CONN_STATE_SET_LOCK_WAIT:
		state_set_lock_wait(t, conn);
		break;

	case CONN_STATE_DEL_IN_KEY:
		state_del_in_key(t, conn);
		break;
	case CONN_STATE_DEL_LOCK_KV:
		state_del_lock_kv(t, conn);
		break;

	case CONN_TIME_LIMIT_STATE_IN_VALUE_SIZE:
		time_limit_state_in_value_size(t, conn);
		break;
	case CONN_TIME_LIMIT_STATE_IN_VALUE:
		time_limit_state_in_value(t, conn);
		break;
	case CONN_STATE_DISCARD_VALUE:
		state_discard_value(t, conn);
		break;

	case CONN_STATE_OUT_ERRNO:
		state_out_errno(t, conn);
		break;

	case CONN_STATE_IN_THREAD_INFO:
		break;
	}
}

/**
 * free_all_conn - Free all conns for version upgrade
 * @event_fd: used to signal main thread we have done the work
 * 
 * Note: we don't have to hold @t->mu because main thread is blocked waiting
 * our signal. And free_conn() don't have to hold @t->mu either, we can ignore
 * that time cost because this is not a frequently accessed function.
 */
static void free_all_conn(struct thread *t, int event_fd)
{
	int ret __attribute__((unused));
	ret = epoll_ctl(t->epfd, EPOLL_CTL_DEL, event_fd, NULL);
	assert(ret == 0);

	struct hlist_node *curr, *temp;
	hlist_for_each_safe(curr, temp, &t->conn_list) {
		struct conn *conn = container_of(curr, struct conn, thread_node);
		free_conn(t, conn);
	}

	hlist_for_each_safe(curr, temp, &t->active_conn_list) {
		struct conn *conn = container_of(curr, struct conn, thread_node);
		free_conn(t, conn);
	}

	uint64_t u = 1;
	ssize_t ret2 __attribute__((unused)) = write(event_fd, &u, sizeof(u));
	assert(ret2 == sizeof(u));
}

/**
 * grab_epoll_events - Grab events from epoll
 */
static void grab_epoll_events(struct thread *t)
{
	struct epoll_event events[CONFIG_MAX_CONN_PER_THREAD];
	int n;
	if (t->unblocked_conn_nr == 0)
		n = epoll_wait(t->epfd, events, CONFIG_MAX_CONN_PER_THREAD, -1);
	else
		n = epoll_wait(t->epfd, events, CONFIG_MAX_CONN_PER_THREAD, 0);

	for (int i = 0; i < n; i++) {
		static_assert(alignof(struct conn) % 2 == 0);
		/* this is an update version signal */
		if (events[i].data.u64 & 1) {
			free_all_conn(t, events[i].data.u64 >> 32);
			return;
		}

		struct conn *conn = events[i].data.ptr;
		conn_unblocked(t, conn);
		if (events[i].events & ~(EPOLLIN | EPOLLOUT))
			free_conn(t, conn);
	}
}

static void process_conns(struct thread *t)
{
	struct hlist_node *curr, *temp;
	hlist_for_each_safe(curr, temp, &t->active_conn_list) {
		struct conn *conn = container_of(curr, struct conn, thread_node);
		if (!conn->blocked)
			process_conn(t, conn);
	}
}

/**
 * __reclaim_memory - Reclaim memory by removing least recently used kv
 * 
 * @return: size of space reclaimed (in bytes) or 0 for nothing to reclaim
 * 
 * Note: don't use for_each (beware of memory migration)
 */
static uint64_t __reclaim_memory(struct thread *t)
{
	if (list_empty(&t->global_lru))
		return 0;

	struct list_head *entry = list_lru_peek(&t->global_lru);
	struct kv *kv = container_of(entry, struct kv, global_lru);
	/* just a close size */
	uint64_t size = kv->val_size + sizeof(*kv) + kv->key[0] + 1;
	kv_force_free(t, kv);
	return size;
}

static void reclaim_memory(struct thread *t)
{
	if (hash_desire_memory(&t->hash_table))
		grow_reclaim_bytes(t);

	int64_t free_bytes = t->memory.free_pages << PAGE_SHIFT;
	int64_t want = t->reclaim_bytes - free_bytes;
	/* want to keep t->reclaim_bytes as small as possible */
	if (want > 0)
		decay_reclaim_bytes(t);

	while (want > 0) {
		uint64_t n = __reclaim_memory(t);
		if (n == 0)
			break;

		want -= n;
	}
}

static void *loop_forever(void *ptr)
{
	struct thread *t = ptr;
	while (true) {
		debug_printf("--------------------loop--------------------\n");
		grab_epoll_events(t);
		process_conns(t);
		reclaim_memory(t);
	}
	__builtin_unreachable();
}

bool thread_run(struct thread *t, uint64_t page)
{
	memory_init(&t->memory, page);
	pthread_t thread_id;
	return pthread_create(&thread_id, NULL, loop_forever, t) == 0;
}

static bool kv_cache_list_init(
		struct mem_cache kv_cache_list[KEY_LEN], struct memory *m)
{
	for (int i = 0; i < KEY_LEN; i++) {
		unsigned int size = offsetof(struct kv, key) + 8 + (i << 3);
		if (!mem_cache_init(&kv_cache_list[i], size, m))
			return false;
	}
	return true;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

static bool sval_cache_list_init(
			struct mem_cache *sval_cache_list, struct memory *m)
{
	int i = 0;
	while (i < min(SVAL_LEN, __SVAL_HALF1_NR)) {
		unsigned int size = __SVAL_SLAB_MIN + i * 8;
		if (!mem_cache_init(&sval_cache_list[i], size, m))
			return false;

		i++;
	}

	for (int shift = __SVAL_HALF2_BASE_SHIFT_MIN; ; shift++) {
		unsigned int base = 1 << shift;
		unsigned int delta = base >> 4;
		unsigned int size = base;
		for (int j = 0; j < 16; j++, i++) {
			if (i >= SVAL_LEN)
				return true;

			size += delta;
			if (!mem_cache_init(&sval_cache_list[i], size, m))
				return false;
		}
	}
}

static bool hval_cache_list_init(
			struct page_cache *hval_cache_list, struct memory *m)
{
	if (HVAL_LEN <= 0)
		return true;

	for (int i = 0; i < HVAL_LEN - 1; i++) {
		uint64_t page = CONFIG_CACHE_MIN_OBJ_NR * ((1 << (1+i)) - 1);
		if (!page_cache_init(&hval_cache_list[i], page, m))
			return false;
	}

	uint64_t max_page = HVAL_PAGE(CONFIG_VAL_SIZE_LIMIT);
	uint64_t page = CONFIG_CACHE_MIN_OBJ_NR * max_page;
	return page_cache_init(&hval_cache_list[HVAL_LEN - 1], page, m);
}

bool thread_init(struct thread *t, struct memory *m)
{
	/* t->memory will be initialized at thread_run() */
	t->reclaim_bytes = 2 * CONFIG_RECLAIM_GROWTH;
	if (!hash_table_init(&t->hash_table, m))
		return false;

	list_head_init(&t->global_lru);
	pthread_mutex_init(&t->mu, NULL);
	hlist_head_init(&t->conn_list);
	t->conn_nr = 0;
	t->epfd = epoll_create1(0);
	if (t->epfd == -1)
		return false;
	t->unblocked_conn_nr = 0;
	hlist_head_init(&t->active_conn_list);

	return kv_cache_list_init(t->kv_cache_list, m) &&
		sval_cache_list_init(t->sval_cache_list, m) &&
		hval_cache_list_init(t->hval_cache_list, m);
}
