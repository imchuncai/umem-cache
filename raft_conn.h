// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RAFT_CONN_H
#define __UMEM_CACHE_RAFT_CONN_H

#include <sys/epoll.h>
#include <sys/uio.h>
#include "raft_proto.h"
#include "tls.h"
#include "list.h"

#define EPOLLLOG 2

enum raft_conn_state {
#ifdef CONFIG_KERNEL_TLS
	RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_IN	= ( 0 << 3) + EPOLLIN,
	RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_OUT= ( 1 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_TLS_CLIENT_DIVIDER	= ( 2 << 3) + 0,
#endif
	RAFT_CONN_STATE_NOT_CONNECTED		= ( 3 << 3) + 0,
	RAFT_CONN_STATE_IN_PROGRESS		= ( 4 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_READY_FOR_USE		= ( 5 << 3) + 0,
	RAFT_CONN_STATE_REQUEST_VOTE_OUT	= ( 6 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_REQUEST_VOTE_IN		= ( 7 << 3) + EPOLLIN,
	RAFT_CONN_STATE_APPEND_LOG_OUT		= ( 8 << 3) + EPOLLOUT + EPOLLLOG,
	RAFT_CONN_STATE_APPEND_LOG_IN		= ( 9 << 3) + EPOLLIN,
	RAFT_CONN_STATE_HEARTBEAT_OUT		= (10 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_HEARTBEAT_IN		= (11 << 3) + EPOLLIN,

	RAFT_CONN_OUTGOING_INCOMING_DIVIDER	= (12 << 3) + 0,
#ifdef CONFIG_KERNEL_TLS
	RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_IN	= (13 << 3) + EPOLLIN,
	RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_OUT= (14 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_TLS_SERVER_DIVIDER	= (15 << 3) + 0,
#endif
	RAFT_CONN_STATE_IN_CMD			= (16 << 3) + EPOLLIN,
	RAFT_CONN_STATE_OUT_SUCCESS		= (17 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_VOTE_OUT		= (18 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_RECV_ENTRY_OUT		= (19 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_RECV_LOG_IN		= (20 << 3) + EPOLLIN  + EPOLLLOG,
	RAFT_CONN_STATE_LEADER_OUT		= (21 << 3) + EPOLLOUT,
	RAFT_CONN_STATE_CLUSTER_OUT		= (22 << 3) + EPOLLOUT + EPOLLLOG,
	RAFT_CONN_STATE_INIT_CLUSTER_IN		= (23 << 3) + EPOLLIN  + EPOLLLOG,
	RAFT_CONN_STATE_CHANGE_CLUSTER_IN	= (24 << 3) + EPOLLIN  + EPOLLLOG,

	RAFT_CONN_STATE_AUTHORITY_DIVIDER	= (25 << 3) + 0,
	RAFT_CONN_STATE_AUTHORITY_PENDING	= (26 << 3) + EPOLLIN,
	RAFT_CONN_STATE_AUTHORITY_OUT		= (27 << 3) + EPOLLOUT,
} __attribute__((__packed__));

struct raft_conn {
	struct log *log;
	uint64_t unio;
	int sockfd;
	bool admin;
	enum raft_conn_state state;
	union {
		struct request_vote_req request_vote_req;
		struct request_vote_res request_vote_res;
		struct append_log_req append_log_req;
		struct heartbeat_req heartbeat_req;
		struct append_entry_res append_entry_res;
		struct change_cluster_req change_cluster_req;
		struct leader_res leader_res;
		struct cluster_res cluster_res;
		struct connect_req connect_req;
		unsigned char buffer[RAFT_CONN_BUFFER_SIZE];
		struct {
			struct authority_approval authority_approval;
			struct list_head authority_node;
			uint64_t authority_pending_nr;
			uint64_t authority_processing_nr;
			uint64_t authority_succeed_nr;
		};
	#ifdef CONFIG_KERNEL_TLS
		struct tls_session session;
	#endif
	};
} __attribute__((aligned(8)));
/* alignment is required for epoll to distinguish incoming and outgoing connections */

struct raft_conn *raft_in_conn_malloc(int sockfd, bool admin, struct in6_addr peer);
ssize_t raft_conn_discard(struct raft_conn *conn);
void raft_out_conn_init(struct raft_conn *conn);
void raft_conn_set_io(
	struct raft_conn *conn, enum raft_conn_state state, uint64_t size);
void raft_conn_borrow_log(struct raft_conn *conn, struct log *log,
				enum raft_conn_state state, uint64_t size);
void raft_conn_return_log(struct raft_conn *conn);
void raft_conn_change_to_ready_for_use(struct raft_conn *conn);
void raft_conn_free(struct raft_conn *conn);
void raft_conn_clear(struct raft_conn *conn);
void raft_conn_free_or_clear(struct raft_conn *conn);
bool raft_conn_read(struct raft_conn *conn, unsigned char *buffer);
bool raft_conn_full_read(struct raft_conn *conn, unsigned char *buffer);
bool raft_conn_full_read_to_buffer(struct raft_conn *conn, uint64_t size);
bool raft_conn_full_write_buffer(struct raft_conn *conn, uint64_t size);
bool raft_conn_write_byte(struct raft_conn *conn, char b);
bool raft_conn_full_write_msg(
		struct raft_conn *conn, struct iovec *iov, size_t iovlen);

#endif
