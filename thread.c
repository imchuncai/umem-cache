// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/socket.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include "thread.h"
#include "encoding.h"
#include "rwonce.h"
#include "epoll.h"
#include "debug.h"

static struct thread threads[CONFIG_THREAD_NR];

#define conn_kv(conn)	(conn->kv_borrower.kv)

#define SIZE_TO_IDX_IDX(size)	(((size) + 7 - KV_CACHE_OBJ_SIZE_MIN) >> 3)
#define SIZE_TO_IDX_LEN		(SIZE_TO_IDX_IDX(KV_CACHE_OBJ_SIZE_MAX) + 1)
static const unsigned char size_to_idx[SIZE_TO_IDX_LEN] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 
	18, 19, 20, 21, 22, 23, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 
	30, 30, 31, 31, 31, 32, 33, 33, 33, 34, 35, 35, 35, 35, 36, 36, 37, 37, 
	37, 38, 38, 38, 39, 39, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 41, 
	42, 42, 42, 42, 42, 43, 44, 44, 44, 44, 44, 44, 45, 45, 46, 46, 46, 46, 
	46, 46, 47, 47, 48, 48, 48, 48, 48, 48, 48, 49, 49, 49, 49, 50, 50, 50, 
	50, 50, 50, 50, 51, 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 52, 52, 52, 
	52, 53, 53, 53, 53, 53, 53, 53, 53, 54, 55, 55, 55, 55, 55, 55, 55, 55, 
	55, 56, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 58, 58, 59, 59, 59, 59, 
	59, 59, 59, 59, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61, 61, 
	61, 61, 61, 61, 61, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63, 63, 63, 
	63, 63, 63, 63, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 65, 65, 65, 65, 
	65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 66, 66, 66, 66, 66, 66, 66, 66, 
	66, 66, 66, 66, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 
	67, 67, 67, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 
	68, 68, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 
	69, 69, 69, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 
	70, 70, 70, 70, 70, 70, 70, 70, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 
	71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 72, 72, 
	72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 
	72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 73, 73, 73, 73, 73, 73, 73, 
	73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 
	73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 74, 74, 74, 74, 74, 74, 
	74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 
	74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 
	74, 74, 74, 74, };

#ifdef DEBUG
static void kv_cache_idx_generate_print()
{
	struct kv_cache cache;
	bool ok __attribute__((unused));
	ok = kv_cache_init(&cache, KV_CACHE_OBJ_SIZE_MIN);
	assert(ok);
	int i = 0;
	printf("{\n\t 0, ");
	for (unsigned int size = KV_CACHE_OBJ_SIZE_MIN + 8;
				size <= KV_CACHE_OBJ_SIZE_MAX; size += 8) {
		struct kv_cache temp;
		ok = kv_cache_init(&temp, size);
		assert(ok);
		if (temp.slab_page != cache.slab_page ||
			temp.slab_objects != cache.slab_objects) {
			i++;
			cache = temp;
		}

		if (SIZE_TO_IDX_IDX(size) % 18 == 0)
			printf("\n\t%2d, ", i);
		else
			printf("%2d, ", i);
	}
	printf("}\n\n");
}
#endif

static bool kv_cache_list_init(struct kv_cache kv_cache_list[KV_CACHE_LEN])
{
	assert(KV_CACHE_LEN == size_to_idx[SIZE_TO_IDX_LEN - 1] + 1);

	for (unsigned int i = 0; i < SIZE_TO_IDX_LEN - 1; i++) {
		if (size_to_idx[i] != size_to_idx[i + 1]) {
			uint16_t size = KV_CACHE_OBJ_SIZE_MIN + 8 * i;
			if(!kv_cache_init(&kv_cache_list[size_to_idx[i]], size))
				return false;
		}
	}
	return kv_cache_init(&kv_cache_list[KV_CACHE_LEN-1], KV_CACHE_OBJ_SIZE_MAX);
}

/**
 * conn_malloc - Allocate space for conn
 * 
 * @return: the allocated conn on success, or NULL on failure
 */
static struct conn *conn_malloc(struct thread *t, int sockfd)
{
	struct conn *conn = fixed_mem_cache_malloc(&t->conn_cache);
	if (conn) {
		conn->state = CONN_STATE_OUT_SUCCESS;
		conn->clock_time_left = 0;
		conn->sockfd = sockfd;
		kv_borrower_init(&conn->kv_borrower);
	}
	return conn;
}

/**
 * conn_free - Deallocates the space related to @conn
 */
static void conn_free(struct thread *t, struct conn *conn)
{
	close(conn->sockfd);
	fixed_mem_cache_free(&t->conn_cache, conn);
}

/**
 * thread_range - Check if @ptr is inside a thread
 */
static bool thread_range(void *ptr)
{
	return ptr >= (void *)threads &&
	       ptr < (void *)(threads + CONFIG_THREAD_NR);
}

/**
 * kv_cache_get - Get the kv_cache that manages memory for objects of
 * size @size bytes
 */
static struct kv_cache *kv_cache_get(struct thread *t, uint64_t size)
{
	struct kv_cache *cache;
	cache = &t->kv_cache_list[size_to_idx[SIZE_TO_IDX_IDX(size)]];
	assert(cache->obj_size >= size);
	assert(cache - 1 < t->kv_cache_list || (cache - 1)->obj_size < size ||
		(cache - 1)->slab_page > (cache)->slab_page);
	return cache;
}

#ifdef CONFIG_RAFT
static void warmed_up(struct thread *t)
{
	if (!t->__warmed_up)
		WRITE_ONCE(t->__warmed_up, true);
}

static bool thread_warmed_up(struct thread *t)
{
	return READ_ONCE(t->__warmed_up);
}

bool threads_warmed_up()
{
	for (int i = 0; i < CONFIG_THREAD_NR; i++) {
		if (thread_warmed_up(threads + i))
			return true;
	}
	return false;
}
#endif

static bool reclaim_lru(struct thread *t);

/**
 * __reserve_page - Try to reserve memory for allocating @page pages of space
 */
static void __reserve_page(struct thread *t, uint64_t page)
{
	while (t->memory.free_pages < page && reclaim_lru(t)) {}
}

/**
 * __reserve_page_aggressive - Try to reserve memory for allocating @page pages
 * of space, and current free space is ignored
 */
static void __reserve_page_aggressive(struct thread *t, uint64_t page)
{
	__reserve_page(t, t->memory.free_pages + page);
}

static void *memory_malloc_advance(struct thread *t, uint64_t page)
{
	__reserve_page(t, page);
	void *ptr = memory_malloc(&t->memory, page);
	if (ptr)
		return ptr;

	__reserve_page_aggressive(t, page);
	return memory_malloc(&t->memory, page);
}

/**
 * __reserve_kv_cache - Try to reserve memory for allocating from @cache
 */
static void __reserve_kv_cache(struct thread *t, struct kv_cache *cache)
{
	while (cache->free_objects == 0 &&
		t->memory.free_pages < cache->slab_page && reclaim_lru(t)) {}
}

/**
 * __reserve_kv_cache_aggressive - Try to reserve memory for allocating
 * from @cache, and current free space is ignored
 */
static void __reserve_kv_cache_aggressive(
			struct thread *t, struct kv_cache *cache)
{
	uint64_t page = t->memory.free_pages + cache->slab_page;
	while (cache->free_objects == 0 &&
		t->memory.free_pages < page && reclaim_lru(t)) {}
}

static struct kv *kv_cache_malloc_kv_advance(
			struct thread *t, struct kv_cache *cache)
{
	__reserve_kv_cache(t, cache);
	struct kv *kv = kv_cache_malloc_kv(cache, &t->memory);
	if (kv)
		return kv;

	__reserve_kv_cache_aggressive(t, cache);
	return kv_cache_malloc_kv(cache, &t->memory);
}

static bool kv_cache_malloc_concat_val_advance(
		struct thread *t, struct kv_cache *cache, struct kv *kv)
{
	__reserve_kv_cache(t, cache);
	if (kv_cache_malloc_concat_val(cache, &t->memory, &kv->soo))
		return true;

	__reserve_kv_cache_aggressive(t, cache);
	return kv_cache_malloc_concat_val(cache, &t->memory, &kv->soo);
}

static void hash_add_advance(struct thread *t, unsigned char *key)
{
	uint64_t page = hash_add(&t->hash_table, key, &t->memory);
	if (page > 0) {
		__reserve_page(t, page);
		if (!hash_grow(&t->hash_table, &t->memory)) {
			__reserve_page_aggressive(t, page);
			hash_grow(&t->hash_table, &t->memory);
		}
	}
}

static void hash_del_advance(struct thread *t, unsigned char *key)
{
	uint64_t page = hash_del(&t->hash_table, key, &t->memory);
	if (page > 0) {
		__reserve_page(t, page);
		if (!hash_shrink(&t->hash_table, &t->memory)) {
			__reserve_page_aggressive(t, page);
			hash_shrink(&t->hash_table, &t->memory);
		}
	}
}

static void call_clock(struct thread *t, struct conn *conn)
{
	if (conn->clock_time_left == 0) {
		conn->clock_time_left = 2;
		hlist_add(&t->clock_list, &conn->clock_node);
	}
}

static void cancel_clock(struct conn *conn)
{
	if (conn->clock_time_left > 0) {
		conn->clock_time_left = 0;
		hlist_del(&conn->clock_node);
	}
}

static void *kv_malloc(struct thread *t, unsigned char *key, uint64_t val_size)
{
	uint64_t size = sizeof(struct kv) + KEY_SIZE(key) + val_size;
	struct kv *kv;
	if (size <= KV_CACHE_OBJ_SIZE_MAX) {
		struct kv_cache *cache = kv_cache_get(t, size);
		return kv_cache_malloc_kv_advance(t, cache);
	}

	unsigned int overflow = size & PAGE_MASK;
	if (overflow == 0 || overflow + 8 > KV_CACHE_OBJ_SIZE_MAX) {
		uint64_t page = (size + PAGE_MASK) >> PAGE_SHIFT;
		kv = memory_malloc_advance(t, page);
		if (kv) {
			/* fake a soo for kv_is_concat() */
			kv->soo = SOO_MAKE(kv, 0);
		}
		return kv;
	}

	uint64_t page = size >> PAGE_SHIFT;
	kv = memory_malloc_advance(t, page);
	if (kv == NULL)
		return NULL;

	struct kv_cache *cache = kv_cache_get(t, overflow + 8);
	if (!kv_cache_malloc_concat_val_advance(t, cache, kv)) {
		memory_free(&t->memory, kv, page);
		return NULL;
	}

	return kv;
}

/**
 * kv_free -  Deallocates the space related to @kv
 * 
 * Note: caller should make sure @kv is disabled and has no borrower
 */
static void kv_free(struct thread *t, struct kv *kv)
{
	assert(kv_no_borrower(kv) && !kv_enabled(kv));

	uint64_t size = KV_SIZE(kv);
	if (size <= KV_CACHE_OBJ_SIZE_MAX) {
		struct kv_cache *cache = kv_cache_get(t, size);
		kv_cache_free(cache, kv->soo, &t->memory);
	} else if (kv_is_concat(kv)) {
		struct kv_cache *cache = kv_cache_get(t, (size & PAGE_MASK) + 8);
		kv_cache_free(cache, kv->soo, &t->memory);
		memory_free(&t->memory, kv, size >> PAGE_SHIFT);
	} else {
		memory_free(&t->memory, kv, (size + PAGE_MASK) >> PAGE_SHIFT);
	}
}

static void conn_borrow_kv(struct thread *t, struct conn *conn, struct kv *kv)
{
	assert(kv_enabled(kv));
	kv_borrow(kv, &conn->kv_borrower);
	list_lru_touch(&t->lru_head, &kv->lru);
}

static void conn_return_kv(struct thread *t, struct conn *conn)
{
	struct kv *kv = conn_kv(conn);
	kv_return(&conn->kv_borrower);

	if (kv_no_borrower(kv) && !kv_enabled(kv))
		kv_free(t, kv);
}

static void lru_add(struct thread *t, struct kv *kv)
{
	list_lru_add(&t->lru_head, &kv->lru);
}

static void lru_del(struct kv *kv)
{
	list_lru_del(&kv->lru);
	list_head_init(&kv->lru);
}

static void kv_enable(struct thread *t, struct conn *conn)
{
	struct kv *kv = conn_kv(conn);
	kv->hash_node = conn->hash_node;
	hlist_node_fix(&kv->hash_node);
	
	lru_add(t, kv);
}

/**
 * Note: we don't need to call clock on the kv if it is still borrowed after
 * disabled, because the conns that borrows this kv is busy on io writing,
 * TCP_USER_TIMEOUT will takes care of that for us.
 */
static void kv_disable(struct thread *t, struct kv *kv)
{
	assert(kv_enabled(kv));
	hash_del_advance(t, KV_KEY(kv));
	
	lru_del(kv);
}

/**
 * reclaim_lru - Reclaim one kv from lru
 */
static bool reclaim_lru(struct thread *t)
{
	if (list_empty(&t->lru_head))
		return false;
	
	struct list_head *entry = list_lru_peek(&t->lru_head);
	struct kv *kv = container_of(entry, struct kv, lru);
	if (!kv_no_borrower(kv))
		return false;

#ifdef CONFIG_RAFT
	warmed_up(t);
#endif

	kv_disable(t, kv);
	kv_free(t, kv);
	return true;
}

static void conn_lock_key(struct thread *t, struct conn *conn)
{
	hash_add_advance(t, conn->key);
	list_head_init(&conn->interest_list);
}

static bool conn_with_key_locked(struct conn *conn)
{
	return  conn->state > CONN_STATE_SET_DIVIDER;
}

static void cmd_get(struct thread *t, struct conn *conn);

static void conn_unlock_key_for_failure(struct thread *t, struct conn *conn)
{
	cancel_clock(conn);
	hash_del_advance(t, conn->key);
	if (conn_kv(conn))
		conn_return_kv(t, conn);

	struct conn *curr, *temp;
	list_for_each_entry_safe(curr, temp, &conn->interest_list, interest_list) {
		list_del(&curr->interest_list);
		cmd_get(t, curr);
	}
}

void thread_dispatch(uint32_t id, int sockfd)
{
	struct thread *t = threads + id;
	if (!epoll_add_out(t->epfd, sockfd, ((uint64_t)sockfd << 32) | 1))
		close(sockfd);
}

static void thread_accept(struct thread *t, int sockfd)
{
	struct conn *conn = conn_malloc(t, sockfd);
	if (conn) {
		struct epoll_event event;
		event.events = EPOLLIN | EPOLLOUT | EPOLLET;
		event.data.ptr = conn;
		int ret = epoll_ctl(t->epfd, EPOLL_CTL_MOD, conn->sockfd, &event);
		if (ret != 0)
			conn_free(t, conn);
	} else {
		close(sockfd);
	}
}

/**
 * free_conn - Close @conn and free
 */
static void free_conn(struct thread *t, struct conn *conn)
{
	debug_printf("free conn:\n");

	if (conn_with_key_locked(conn))
		conn_unlock_key_for_failure(t, conn);
	else if (conn_kv(conn))
		conn_return_kv(t, conn);
	else if (conn->state == CONN_STATE_GET_BLOCKED)
		list_del(&conn->interest_list);

	conn_free(t, conn);
}

/**
 * conn_check_io - Update @conn after an io
 * @n: the return value from read() or write()
 * 
 * @return: true on something is read or written, false otherwise
 */
static bool conn_check_io(struct thread *t, struct conn *conn, ssize_t n)
{
	if (n > 0) {
		assert(conn->unio >= (size_t)n);
		conn->unio -= n;
		return true;
	}

	if (!(n == -1 && errno == EWOULDBLOCK))
		free_conn(t, conn);

	return false;
}

/**
 * __conn_full_io - Check if is full io
 * 
 * Note: should only be called after a success io (something is read or written)
 */
static bool __conn_full_io(const struct conn *conn)
{
	return conn->unio == 0;
}

/**
 * conn_read - Read from @conn to @buffer
 * 
 * @return: true on something is read, false on nothing is read
 */
static bool conn_read(
struct thread *t, struct conn *conn, unsigned char *buffer)
{
	assert(conn->unio > 0);
	ssize_t n = read(conn->sockfd, buffer, conn->unio);
	return conn_check_io(t, conn, n);
}

/**
 * conn_full_read - Read from @conn to @buffer
 * 
 * @return: true on full read, false on short read
 */
static bool conn_full_read(
struct thread *t, struct conn *conn, unsigned char *buffer)
{
	return conn_read(t, conn, buffer) && __conn_full_io(conn);
}

/**
 * conn_read_msg - Read message from @conn to @iov
 * @iovlen: length of @iov
 * 
 * @return: true on something is read, false on nothing is read
 */
static bool conn_read_msg(
struct thread *t, struct conn *conn, struct iovec *iov, size_t iovlen)
{
	struct msghdr msg = {};
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	assert(conn->unio > 0);
	ssize_t n = recvmsg(conn->sockfd, &msg, 0);
	return conn_check_io(t, conn, n);
}

/**
 * conn_full_read_msg - Read message from @conn to @iov
 * @iovlen: length of @iov
 * 
 * @return: true on full read, false on short read
 */
static bool conn_full_read_msg(
struct thread *t, struct conn *conn, struct iovec *iov, size_t iovlen)
{
	return conn_read_msg(t, conn, iov, iovlen) && __conn_full_io(conn);
}

/**
 * conn_write - Write from @buffer to @conn
 * 
 * @return: true on something is written, false on nothing is written
 */
static bool conn_write(
struct thread *t, struct conn *conn, const unsigned char *buffer)
{
	assert(conn->unio > 0);
	ssize_t n = send(conn->sockfd, buffer, conn->unio, MSG_NOSIGNAL);
	return conn_check_io(t, conn, n);
}

/**
 * conn_full_write - Write from @buffer to @conn
 * 
 * @return: true on full write, false on short write
 */
static bool conn_full_write(
struct thread *t, struct conn *conn, const unsigned char *buffer)
{
	return conn_write(t, conn, buffer) && __conn_full_io(conn);
}

/**
 * conn_write_msg - Write message from @iov to @conn
 * @iovlen: length of @iov
 * 
 * @return: true on something is written, false on nothing is written
 */
static bool conn_write_msg(
struct thread *t, struct conn *conn, struct iovec *iov, size_t iovlen)
{
	struct msghdr msg = {};
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	assert(conn->unio > 0);
	ssize_t n = sendmsg(conn->sockfd, &msg, MSG_NOSIGNAL);
	return conn_check_io(t, conn, n);
}

/**
 * conn_full_write_msg - Write message from @iov to @conn
 * @iovlen: length of @iov
 * 
 * @return: true on full write, false on short write
 */
static bool conn_full_write_msg(
struct thread *t, struct conn *conn, struct iovec *iov, size_t iovlen)
{
	return conn_write_msg(t, conn, iov, iovlen) && __conn_full_io(conn);
}

/**
 * conn_write_byte - Write @b to @conn
 * 
 * @return: true on @b is written, false on nothing is written
 */
static bool conn_write_byte(struct thread *t, struct conn *conn, char b)
{
	ssize_t n = send(conn->sockfd, &b, 1, MSG_NOSIGNAL);
	if (n > 0)
		return true;

	if (!(n == -1 && errno == EWOULDBLOCK))
		free_conn(t, conn);

	return false;
}

static void change_to_in_cmd(struct conn *conn)
{
	assert(conn->state == CONN_STATE_OUT_SUCCESS ||
	       conn->state == CONN_STATE_GET_OUT_HIT);
	assert(conn_kv(conn) == NULL);
	conn->state = CONN_STATE_IN_CMD;
	conn->unio = CMD_SIZE_MAX;
	/* Don't call state_in_cmd(), it is very likely that we are blocked on
	read. And we just out something, so the read event can not be triggered
	this round, it will be triggered later. */
}

static void state_out_success(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_OUT_SUCCESS:\n");

	if (conn_write_byte(t, conn, 0))
		change_to_in_cmd(conn);
}

static void change_to_out_success(struct thread *t, struct conn *conn)
{
	conn->state = CONN_STATE_OUT_SUCCESS;
	state_out_success(t, conn);
}

static void state_get_out_hit(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_OUT_HIT: %lu\n", conn_kv(conn)->val_size);

	uint64_t written = GET_RES_SIZE + conn_kv(conn)->val_size - conn->unio;
	struct iovec iov[3];
	uint64_t iov_len;
	if (written < GET_RES_SIZE) {
		iov[0].iov_base = conn->buffer + written;
		iov[0].iov_len = GET_RES_SIZE - written;
		iov_len = 1 + kv_val_to_iovec(conn_kv(conn), 0, iov + 1);
	} else {
		uint64_t i = conn_kv(conn)->val_size - conn->unio;
		iov_len = kv_val_to_iovec(conn_kv(conn), i, iov);
	}

	if (conn_full_write_msg(t, conn, iov, iov_len)) {
		conn_return_kv(t, conn);
		change_to_in_cmd(conn);
	}
}

static void change_to_get_out_hit(struct thread *t, struct conn *conn)
{
	conn->state = CONN_STATE_GET_OUT_HIT;
	conn->unio = GET_RES_SIZE + conn_kv(conn)->val_size;
	conn->miss = false;
	conn->size = htonll(conn_kv(conn)->val_size);
	state_get_out_hit(t, conn);
}

static void conn_unlock_key_for_success(struct thread *t, struct conn *conn)
{
	cancel_clock(conn);
	kv_enable(t, conn);
	struct kv *kv = conn_kv(conn);

	struct conn *curr, *temp;
	list_for_each_entry_safe(curr, temp, &conn->interest_list, interest_list) {
		list_del(&curr->interest_list);
		conn_borrow_kv(t, curr, kv);
		change_to_get_out_hit(t, curr);
	}

	conn_return_kv(t, conn);
}

static void change_to_set_in_value_success(struct thread *t, struct conn *conn)
{
	conn_unlock_key_for_success(t, conn);
	change_to_out_success(t, conn);
}

static void state_set_in_value(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_SET_IN_VALUE:\n");
	
	uint64_t readed = conn_kv(conn)->val_size - conn->unio;
	struct iovec iov[2];
	int iov_len = kv_val_to_iovec(conn_kv(conn), readed, iov);
	if (conn_full_read_msg(t, conn, iov, iov_len))
		change_to_set_in_value_success(t, conn);
}

static void change_to_set_in_value(struct thread *t, struct conn *conn)
{
	if (conn_kv(conn)->val_size == 0) {
		change_to_set_in_value_success(t, conn);
	} else {
		conn->state = CONN_STATE_SET_IN_VALUE;
		conn->unio = conn_kv(conn)->val_size;
		state_set_in_value(t, conn);
	}
}

static void state_set_in_value_size(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_SET_IN_VALUE_SIZE:\n");

	uint64_t readed = SET_REQ_SIZE - conn->unio;
	if (!conn_full_read(t, conn, conn->buffer + readed))
		return;

	conn->val_size = ntohll(conn->size);
	struct kv *kv = kv_malloc(t, conn->key, conn->val_size);
	if (kv) {
		kv_init(kv, conn->key, conn->val_size);
		kv_borrow(kv, &conn->kv_borrower);
		change_to_set_in_value(t, conn);
	} else {
		free_conn(t, conn);
	}
}

static void change_to_set_in_value_size(struct conn *conn)
{
	conn->state = CONN_STATE_SET_IN_VALUE_SIZE;
	conn->unio = SET_REQ_SIZE;
	/* Don't call state_set_in_value_size(), it is very likely that we are
	blocked on read. And we just out miss, so the read event can not be
	triggered this round, it will be triggered later. */
}

static void state_get_out_miss(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_GET_OUT_MISS:\n");

	uint64_t written = GET_RES_SIZE - conn->unio;
	if (conn_full_write(t, conn, conn->buffer + written))
		change_to_set_in_value_size(conn);
}

static void change_to_get_out_miss(struct thread *t, struct conn *conn)
{
	conn->state = CONN_STATE_GET_OUT_MISS;
	conn->unio = GET_RES_SIZE;
	conn->miss = true;
	state_get_out_miss(t, conn);
}

static void cmd_get(struct thread *t, struct conn *conn)
{
	struct hlist_node *node = hash_get(&t->hash_table, conn->key);
	if (node == NULL) {
		conn_lock_key(t, conn);
		change_to_get_out_miss(t, conn);
	} else if (thread_range(node)) {
		struct conn *lock_conn = container_of(node, struct conn, hash_node);
		conn->state = CONN_STATE_GET_BLOCKED;
		list_add(&lock_conn->interest_list, &conn->interest_list);
		call_clock(t, lock_conn);
	} else {
		struct kv *kv = container_of(node, struct kv, hash_node);
		conn_borrow_kv(t, conn, kv);
		change_to_get_out_hit(t, conn);
	}
}

static void cmd_del(struct thread *t, struct conn *conn)
{
	struct hlist_node *node = hash_get(&t->hash_table, conn->key);
	if (node == NULL) {
	} else if (thread_range(node)) {
		struct conn *lock_conn = container_of(node, struct conn, hash_node);
		assert(conn_with_key_locked(lock_conn));
		free_conn(t, lock_conn);
	} else {
		struct kv *kv = container_of(node, struct kv, hash_node);
		kv_disable(t, kv);
		if (kv_no_borrower(kv))
			kv_free(t, kv);
	}
	change_to_out_success(t, conn);
}

static void state_in_cmd(struct thread *t, struct conn *conn)
{
	debug_printf("CONN_STATE_IN_CMD: ..........................\n");
	assert(conn_kv(conn) == NULL);

	uint64_t readed = CMD_SIZE_MAX - conn->unio;
	if (!conn_read(t, conn, conn->key - 1 + readed))
		return;

	readed = CMD_SIZE_MAX - conn->unio;
	if (readed < CMD_SIZE_MIN + (uint64_t)conn->key[0])
		return;

	/* zero key padding, this is required by key comparison */
	for (int i = (int)conn->key[0] + 1; i & 7; i++)
		conn->key[i] = 0;

	char cmd = *(conn->key - 1);
	switch (cmd) {
	case CACHE_CMD_GET_OR_SET:
		debug_printf("CACHE_CMD_GET_OR_SET: key_n: %u\n", conn->key[0]);
		cmd_get(t, conn);
		break;

	case CACHE_CMD_DEL:
		debug_printf("CACHE_CMD_DEL: key_n: %u\n", conn->key[0]);
		cmd_del(t, conn);
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
	case CONN_STATE_OUT_SUCCESS:
		state_out_success(t, conn);
		break;
	case CONN_STATE_GET_OUT_HIT:
		state_get_out_hit(t, conn);
		break;

	case CONN_STATE_GET_OUT_MISS:
		state_get_out_miss(t, conn);
		break;
	case CONN_STATE_SET_IN_VALUE_SIZE:
		state_set_in_value_size(t, conn);
		break;
	case CONN_STATE_SET_IN_VALUE:
		state_set_in_value(t, conn);
		break;

	case CONN_STATE_GET_BLOCKED:
	case CONN_STATE_SET_DIVIDER:
		__builtin_unreachable();
	}
}

static void clock_service(struct thread *t, int timerfd)
{
	uint64_t exp;
	size_t n __attribute__((unused)) = read(timerfd, &exp, sizeof(exp));
	assert(n == sizeof(exp));

	struct hlist_node *curr, *temp;
	hlist_for_each_safe(curr, temp, &t->clock_list) {
		struct conn *conn = container_of(curr, struct conn, clock_node);
		assert(conn_with_key_locked(conn));
		assert(conn->clock_time_left > 0);
		if (exp >= conn->clock_time_left)
			free_conn(t, conn);
		else
			conn->clock_time_left -= exp;
	}
}

/**
 * grab_epoll_events - Grab events from epoll
 */
static void grab_epoll_events(struct thread *t)
{
	struct epoll_event *events = t->events;
	int n = epoll_wait(t->epfd, events, THREAD_MAX_CONN, -1);
	int timerfd = -1;
	for (int i = 0; i < n; i++) {
		static_assert(alignof(struct conn) % 8 == 0);

		if (events[i].data.u64 & 1) {
			/* main thread distribute sockfd to us */
			thread_accept(t, events[i].data.u64 >> 32);
		} else if (events[i].data.u64 & 2) {
			/* this is a clock service */
			timerfd = events[i].data.u64 >> 32;
		} else {
			struct conn *conn = events[i].data.ptr;
			if (events[i].events & ~(EPOLLIN | EPOLLOUT)) {
				debug_printf("events: %u\n", events[i].events);
				free_conn(t, conn);
			} else if (events[i].events & conn->state) {
				process_conn(t, conn);
			}
		}
	}

	if (timerfd != -1)
		clock_service(t, timerfd);
}

static void *loop_forever(void *ptr)
{
	struct thread *t = ptr;
	while (true) {
		debug_printf("--------------loop: %d--------------\n", t->epfd);
		grab_epoll_events(t);
	}
	__builtin_unreachable();
}

static bool thread_create_clock_service(struct thread *t)
{
	int timerfd = timerfd_create(CLOCK_BOOTTIME, 0);
	if (timerfd == -1)
		return false;

	struct itimerspec timer;
	timer.it_value.tv_sec = CONFIG_TCP_TIMEOUT / 1000;
	timer.it_value.tv_nsec = (CONFIG_TCP_TIMEOUT % 1000) * 1000000;
	timer.it_interval = timer.it_value;
	if (timerfd_settime(timerfd, 0, &timer, NULL) == -1) {
		close(timerfd);
		return false;
	}
	
	if (epoll_add_in(t->epfd, timerfd, ((uint64_t)timerfd << 32) | 2))
		return true;

	close(timerfd);
	return false;
}

static bool thread_init(struct thread *t)
{
#ifdef CONFIG_RAFT
	t->__warmed_up = false;
#endif
	memory_init(&t->memory, THREAD_MAX_MEM >> PAGE_SHIFT);
	list_head_init(&t->lru_head);
	hlist_head_init(&t->clock_list);
	t->epfd = epoll_create1(0);
	if (t->epfd == -1)
		return false;
	fixed_mem_cache_init(&t->conn_cache, t->__conns, sizeof(struct conn),
							THREAD_MAX_CONN);

	return thread_create_clock_service(t) &&
		hash_table_init(&t->hash_table, &t->memory) &&
		kv_cache_list_init(t->kv_cache_list);
}

static bool thread_run(struct thread *t)
{
	thread_init(t);
	pthread_t thread_id;
	return pthread_create(&thread_id, NULL, loop_forever, t) == 0;
}

bool threads_run()
{
#ifdef DEBUG
	kv_cache_idx_generate_print();
#endif

	for (uint32_t i = 0; i < CONFIG_THREAD_NR; i++) {
		if (!thread_run(&threads[i]))
			return false;
	}
	return true;
}
