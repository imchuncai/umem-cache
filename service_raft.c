// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <sys/timerfd.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "thread.h"
#include "encoding.h"
#include "epoll.h"
#include "tls.h"
#include "cluster.h"
#include "socket.h"
#include "debug.h"

#define TIMER_EVENT_U64		0
#define ACCEPT_EVENT_U64	1
#define ACCEPT_ADMIN_EVENT_U64	2

enum server_state {
	SERVER_STATE_LEADER,
	SERVER_STATE_CANDIDATE,
	SERVER_STATE_FOLLOWER,
} __attribute__((__packed__));

#define SERVER_MAX_EPOLL_EVENTS	512

/**
 * server -
 * @timer_ticks: +1 when received timer event
 */
struct server {
	int epfd;
	uint32_t id;
	uint64_t current_term;
	int timerfd;
	unsigned char timer_ticks;
	enum server_state state;
	union {
		struct {
			int32_t commit_entry_required_old_votes;
			int32_t commit_entry_required_new_votes;
			uint64_t replicate_entry_round;
			bool replicate_entry;
			bool entry_commited;

			bool available;
		} leader;
		struct {
			int32_t required_old_votes;
			int32_t required_new_votes;
		} candidate;
		struct {
			uint32_t voted_for;
			uint32_t leader;
		} follower;
	};
	struct log *log;
	struct list_head authority_list;
	struct cluster *stale_cluster;
	struct cluster *cluster;
	struct epoll_event events[SERVER_MAX_EPOLL_EVENTS];
};

static void server_borrow_log(struct server *s, struct log *log)
{
	log_borrow(log);
	s->log = log;
}

static void server_replace_log(struct server *s, struct log *new)
{
	debug_printf("replace log: index: %ld term: %ld type: %d version: %ld\n",
			new->index, new->term, new->type, new->version);

	log_return(s->log);
	server_borrow_log(s, new);
}

static int listen_user(int epfd, in_port_t port)
{
	return listen_port(port, epfd, ACCEPT_EVENT_U64);
}

static int listen_admin(int epfd, in_port_t port)
{
	return listen_port(port + 1, epfd, ACCEPT_ADMIN_EVENT_U64);
}

static bool _accept(struct server *s, int sockfd, bool admin)
{
	while (true) {
		struct in6_addr peer;
		int fd = accept2(sockfd, &peer);
		if (fd == -1)
			return errno == EWOULDBLOCK;

		struct raft_conn *conn = raft_in_conn_malloc(fd, admin, peer);
		if (conn == NULL)
			close(fd);
		else if (!epoll_add(s->epfd, conn->sockfd, (uint64_t)conn))
			raft_conn_free(conn);
	}
}

static bool leader_log_commited(struct server *s)
{
	struct cluster *cl = s->cluster;
	uint32_t old_commited = 0;
	uint32_t new_commited = 0;
	for (uint32_t i = 0; i < cl->members_n; i++) {
		struct member *m = cl->members + i;
		if (m->match_index >= s->log->index) {
			if (m->type & MEMBER_TYPE_OLD)
				old_commited++;

			if (m->type & MEMBER_TYPE_NEW)
				new_commited++;
		}
	}
	/* Note: we ignored leader warm up status for easy program */
	return old_commited >= cl->require_old_votes &&
	       new_commited >= cl->require_new_votes;
}

static void reset_timer(struct server *s)
{
	s->timer_ticks = 0;
}

static void reset_timer_hard(struct server *s)
{
	/**
	 * RAFT: 9.3 Performance
	 * We recommend using a con-
	 * servative election timeout such as 150–300ms; such time-
	 * outs are unlikely to cause unnecessary leader changes and
	 * will still provide good availability.
	 * 
	 * RAFT: 5.6 Timing and availability
	 * The broadcast time should be an order of mag-
	 * nitude less than the election timeout so that leaders can
	 * reliably send the heartbeat messages required to keep fol-
	 * lowers from starting elections;
	 * 
	 * RAFT: 6 Cluster membership changes
	 * Specif-
	 * ically, if a server receives a RequestVote RPC within
	 * the minimum election timeout of hearing from a cur-
	 * rent leader, it does not update its term or grant its vote.
	 * This does not affect normal elections, where each server
	 * waits at least a minimum election timeout before starting
	 * an election.
	 * 
	 * Note: according to (RAFT: 6 Cluster Membership Change), I believe
	 * that the raft implementation in (RAFT: Figure 16) must not have
	 * implemented cluster membership changes, otherwise the time without a
	 * leader would be at least twice the minimum election timeout.
	 * 
	 * Note: we don't have to persist log to stable storage, so we
	 * definitely have some room to reduce the election_timeout.
	 */
	int election_timeout = rand() % (150 * 1000000) + (300 - 150) * 1000000;
	int broadcast_time = election_timeout / 10;

	struct itimerspec spec;
	spec.it_value.tv_sec = 0;
	spec.it_value.tv_nsec = broadcast_time;
	spec.it_interval = spec.it_value;
	int ret __attribute__((unused));
	ret = timerfd_settime(s->timerfd, 0, &spec, NULL);
	assert(ret != -1);

	reset_timer(s);
}

static void set_timer(struct server *s, uint32_t id)
{
	srand(id);
	reset_timer_hard(s);
}

static bool election_timeout(struct server *s)
{
	return s->timer_ticks > 10;
}

static void server_replace_cluster(struct server *s, struct cluster *cl)
{
	debug_printf("cluster replaced:\n");

	if (s->cluster) {
		s->cluster->next_stale = s->stale_cluster;
		s->stale_cluster = s->cluster;
	}
	s->cluster = cl;
}

static void reset_follower(struct server *s)
{
	assert(s->state == SERVER_STATE_FOLLOWER);

	s->follower.voted_for = 0;
	s->follower.leader = 0;
}

static void convert_to_follower(struct server *s)
{
	assert(s->state != SERVER_STATE_FOLLOWER);
	debug_printf("convert to follower:\n");

	server_replace_cluster(s, NULL);

	s->state = SERVER_STATE_FOLLOWER;
	reset_follower(s);
	reset_timer(s);
}

/**
 * RAFT: 5.1 Raft basics
 * Current terms are exchanged
 * whenever servers communicate; if one server’s current
 * term is smaller than the other’s, then it updates its current
 * term to the larger value. If a candidate or leader discovers
 * that its term is out of date, it immediately reverts to fol-
 * lower state.
 */
static void server_increase_term(struct server *s, uint64_t term)
{
	assert(term > s->current_term);
	s->current_term = term;

	if (s->state == SERVER_STATE_FOLLOWER)
		reset_follower(s);
	else
		convert_to_follower(s);
}

static void must_server_init(struct server *s)
{
	s->epfd = epoll_create1(0);
	must(s->epfd != -1);

	s->current_term = 0;

	s->timerfd = timerfd_create(CLOCK_BOOTTIME, 0);
	must(s->timerfd != -1);
	must(epoll_add_in(s->epfd, s->timerfd, TIMER_EVENT_U64));

	s->state = SERVER_STATE_FOLLOWER;
	reset_follower(s);

	struct log *log = log_malloc(0);
	must(log);
	memset(log, 0, sizeof(struct log));
	server_borrow_log(s, log);

	list_head_init(&s->authority_list);
	s->stale_cluster = NULL;
	s->cluster = NULL;
}

static bool leader_change_available(struct server *s)
{
	struct log *log;
	log = log_malloc_change_available(s->cluster, s->log, s->current_term);
	if (log) {
		server_replace_log(s, log);
		// Note: cluster is not changed.
		s->leader.replicate_entry = true;
	}
	return log;
}

static bool leader_replace_log(struct server *s, struct log *new)
{
	assert(s->state == SERVER_STATE_LEADER);

	struct cluster *cl = cluster_malloc(new, s->id);
	if (cl) {
		server_replace_log(s, new);
		server_replace_cluster(s, cl);
		s->leader.replicate_entry = true;
	}
	return cl;
}

static void convert_to_leader(struct server *s)
{
	assert(s->state == SERVER_STATE_CANDIDATE);
	debug_printf("convert to leader:\n");
	s->state = SERVER_STATE_LEADER;

	/**
	 * RAFT: 5.4.2 Committing entries from previous terms
	 * To eliminate problems like the one in Figure 8, Raft
	 * never commits log entries from previous terms by count-
	 * ing replicas. Only log entries from the leader’s current
	 * term are committed by counting replicas; once an entry
	 * from the current term has been committed in this way,
	 * then all prior entries are committed indirectly because
	 * of the Log Matching Property.
	 */
	struct log *log = s->log;
	const struct machine *leader;
	if (log->type & LOG_TYPE_UNSTABLE_MASK) {
		log->index++;
		log->term = s->current_term;
		leader = log_machines_find_new(log, s->id);
	} else {
		leader = log_machines_find_old(log, s->id);
	}
	s->leader.replicate_entry_round = 0;
	s->leader.replicate_entry = true;
	s->leader.entry_commited = true;
	if (leader)
		s->leader.available = machine_available(leader);
	else
		s->leader.available = true;
}

static void win_election(struct server *s)
{
	convert_to_leader(s);
}

static void change_to_in_cmd(struct raft_conn *conn)
{
	raft_conn_set_io(conn, RAFT_CONN_STATE_IN_CMD, RAFT_CONN_BUFFER_SIZE);
	/* Don't call state_in_cmd(), it is very likely that we are blocked on
	read. And we just out something, so the read event can not be triggered
	this round, it will be triggered later. */
}

static void state_out_success(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_OUT_SUCCESS:\n");

	if (raft_conn_write_byte(conn, 0))
		change_to_in_cmd(conn);
}

static void change_to_out_success(struct raft_conn *conn)
{
	conn->state = RAFT_CONN_STATE_OUT_SUCCESS;
	state_out_success(conn);
}

static void state_vote_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_VOTE_OUT:\n");

	if (raft_conn_full_write_buffer(conn, sizeof(struct request_vote_res)))
		change_to_in_cmd(conn);
}

static void change_to_vote_out(struct server *s, struct raft_conn *conn, bool grant)
{
	debug_printf("term: %ld voted for: %d\n", s->current_term,
					grant ? s->follower.voted_for : 0);

	struct request_vote_res *res = &conn->request_vote_res;
	res->term = htonll(s->current_term);
	res->granted = grant;

	raft_conn_set_io(conn, RAFT_CONN_STATE_VOTE_OUT, sizeof(*res));
	state_vote_out(conn);
}

static void state_vote_in(struct server *s, struct raft_conn *conn)
{
	/**
	 * RAFT: 6
	 * if a server receives a RequestVote RPC within
	 * the minimum election timeout of hearing from a cur-
	 * rent leader, it does not update its term or grant its vote.
	 */
	if (s->state == SERVER_STATE_LEADER ||
		(s->state == SERVER_STATE_FOLLOWER && s->follower.leader != 0)) {
		change_to_vote_out(s, conn, false);
		return;
	}

	struct request_vote_req *req = &conn->request_vote_req;
	uint32_t candidate_id = ntohl(req->candidate_id);
	uint64_t term = ntohll(req->term);
	uint64_t log_index = ntohll(req->log_index);
	uint64_t log_term = ntohll(req->log_term);

	if (term > s->current_term)
		server_increase_term(s, term);

	/**
	 * RAFT: Figure 2: RequestVote RPC: Receiver implementation:
	 * 1. Reply false if term < currentTerm (§5.1)
	 * 2. If votedFor is null or candidateId, and candidate’s log is at
	 * least as up-to-date as receiver’s log, grant vote (§5.2, §5.4)
	 */
	if (s->state == SERVER_STATE_FOLLOWER && term >= s->current_term &&
		(s->follower.voted_for == 0 ||
		 s->follower.voted_for == candidate_id) &&
		log_at_least_up_to_date(s->log, log_index, log_term)) {
		assert(s->follower.leader == 0);
		reset_timer(s);
		s->follower.voted_for = candidate_id;
		change_to_vote_out(s, conn, true);
	} else {
		change_to_vote_out(s, conn, false);
	}
}

static void state_request_vote_in(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_REQUEST_VOTE_IN:\n");

	if (!raft_conn_full_read_to_buffer(conn, sizeof(struct request_vote_res)))
		return;

	struct request_vote_res *res = &conn->request_vote_res;
	uint64_t term = ntohll(res->term);
	bool vote_granted = res->granted;

	raft_conn_change_to_ready_for_use(conn);

	if (term > s->current_term) {
		server_increase_term(s, term);
		return;
	}

	if (s->state == SERVER_STATE_CANDIDATE && s->current_term == term && vote_granted) {
		struct member *m = container_of(conn, struct member, conn);
		if ((m->type & MEMBER_TYPE_OLD))
			s->candidate.required_old_votes--;

		if ((m->type & MEMBER_TYPE_NEW))
			s->candidate.required_new_votes--;

		debug_printf("vote granted, still require: %d, %d\n",
				s->candidate.required_old_votes,
				s->candidate.required_new_votes);
		if (s->candidate.required_old_votes <= 0 &&
		    s->candidate.required_new_votes <= 0) {
			win_election(s);
		}
	}
}

static void change_to_request_vote_in(struct raft_conn *conn)
{
	raft_conn_set_io(conn, RAFT_CONN_STATE_REQUEST_VOTE_IN,
					sizeof(struct request_vote_res));
}

static void state_request_vote_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_REQUEST_VOTE_OUT:\n");

	if (raft_conn_full_write_buffer(conn, sizeof(struct request_vote_req)))
		change_to_request_vote_in(conn);
}

static void change_to_request_vote_out(struct server *s, struct raft_conn *conn)
{
	assert(s->state == SERVER_STATE_CANDIDATE);

	struct request_vote_req *req = &conn->request_vote_req;
	req->cmd = RAFT_CMD_REQUEST_VOTE;
	req->candidate_id = htonl(s->id);
	req->term = htonll(s->current_term);
	req->log_index = htonll(s->log->index);
	req->log_term = htonll(s->log->term);

	raft_conn_set_io(conn, RAFT_CONN_STATE_REQUEST_VOTE_OUT, sizeof(*req));
	state_request_vote_out(conn);
}

static void state_recv_entry_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_RECV_ENTRY_OUT:\n");

	if (raft_conn_full_write_buffer(conn, sizeof(struct append_entry_res)))
		change_to_in_cmd(conn);
}

static bool server_warmed_up(struct server *s)
{
	return s->log->type != LOG_TYPE_GROW_TRANSFORM || threads_warmed_up();
}

static void change_to_recv_entry_out(struct server *s, struct raft_conn *conn)
{
	struct append_entry_res *res = &conn->append_entry_res;
	res->term = htonll(s->current_term);
	res->applied = server_warmed_up(s);

	raft_conn_set_io(conn, RAFT_CONN_STATE_RECV_ENTRY_OUT, sizeof(*res));
	state_recv_entry_out(conn);
}

static void change_to_recv_log_out(struct server *s, struct raft_conn *conn)
{
	raft_conn_return_log(conn);
	change_to_recv_entry_out(s, conn);
}

/**
 * state_recv_log_in -
 * 
 * Note: server will replace the log even it is identical to the current one
 */
static void state_recv_log_in(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_RECV_LOG_IN:\n");

	uint64_t machines_size = ntohll(conn->append_log_req.machines_size);
	uint64_t readed = machines_size - conn->unio;
	struct log *log = conn->log;
	if (!raft_conn_full_read(conn, (unsigned char *)log->machines + readed))
		return;

	struct append_log_req *req = &conn->append_log_req;
	enum log_type type = req->type;
	uint64_t term = ntohll(req->term);
	uint32_t leader = ntohl(req->leader_id);
	uint32_t follower = ntohl(req->follower_id);
	uint64_t log_index = ntohll(req->log_index);
	uint64_t log_term = ntohll(req->log_term);
	uint64_t version = ntohll(req->version);
	uint64_t next_machine_version = ntohll(req->next_machine_version);
	uint32_t next_machine_id = ntohl(req->next_machine_id);
	uint32_t new_machine_nr = ntohl(req->new_machine_nr);
	uint64_t distinct_machines_n = ntohll(req->distinct_machines_n);

	/**
	 * RAFT: Figure 2: AppendEntries RPC
	 * 1. Reply false if term < currentTerm (§5.1)
	 * 2. Reply false if log doesn’t contain an entry at prevLogIndex
	 *    whose term matches prevLogTerm (§5.3)
	 */
	if (term < s->current_term) {
		change_to_recv_log_out(s, conn);
		return;
	}

	if (s->log->index == 0)
		set_timer(s, follower);

	if (term > s->current_term)
		server_increase_term(s, term);
	else if (s->state != SERVER_STATE_FOLLOWER)
		convert_to_follower(s);

	assert(s->state == SERVER_STATE_FOLLOWER);
	s->id = follower;
	s->follower.leader = leader;
	reset_timer(s);

	log->index = log_index;
	log->term = log_term;
	log->version = version;
	log->next_machine_version = next_machine_version;
	log->next_machine_id = next_machine_id;
	log->type = type;
	log->old_n = machines_size / MACHINE_SIZE - new_machine_nr;
	log->new_n = new_machine_nr;
	log->distinct_machines_n = distinct_machines_n;

	server_replace_log(s, log);

	change_to_recv_log_out(s, conn);
}

static void change_to_recv_log_in(struct server *s, struct raft_conn *conn)
{
	uint64_t machines_size = ntohll(conn->append_log_req.machines_size);
	struct log *log = log_malloc(machines_size);
	if (log) {
		raft_conn_borrow_log(
			conn, log, RAFT_CONN_STATE_RECV_LOG_IN, machines_size);
		state_recv_log_in(s, conn);
	} else {
		raft_conn_free(conn);
	}
}

static bool state_authority_in(struct server *s, struct raft_conn *conn) {
	ssize_t n = raft_conn_discard(conn);
	if (n == -1)
		return false;

	// Note: don't check (n > 0), make it faster
	conn->authority_pending_nr += n;
	s->leader.replicate_entry = true;
	return true;
}

static void change_to_authority_pending(struct raft_conn *conn)
{
	conn->state = RAFT_CONN_STATE_AUTHORITY_PENDING;
}

static void state_authority_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_AUTHORITY_OUT:\n");

	if (raft_conn_full_write_buffer(conn, sizeof(struct authority_approval)))
		change_to_authority_pending(conn);
}

static void change_to_authority_out(struct server *s, struct raft_conn *conn)
{
	struct authority_approval *res = &conn->authority_approval;
	res->version = htonll(s->log->version);
	res->count = htonll(conn->authority_succeed_nr);
	conn->authority_succeed_nr = 0;
	raft_conn_set_io(conn, RAFT_CONN_STATE_AUTHORITY_OUT, sizeof(*res));
	state_authority_out(conn);
}

static bool log_commited(struct server *s, struct member *m)
{
	if (m->match_index == m->next_index - 1)
		return true;

	m->match_index = m->next_index - 1;
	struct log *log = s->log;
	if (m->match_index != log->index || !leader_log_commited(s))
		return true;

	struct log *new = NULL;
	switch (log->type) {
	case LOG_TYPE_OLD:
		/**
		 * RAFT: 6
		 * The second issue is that the cluster leader may not be
		 * part of the new configuration. In this case, the leader steps
		 * down (returns to follower state) once it has committed the
		 * Cnew log entry.
		 */
		if (log->old_n == s->cluster->members_n) {
			debug_printf("EXIT.................................\n");
			exit(EXIT_SUCCESS);
		}
		return true;

	case LOG_TYPE_GROW_TRANSFORM:
		new = log_malloc_grow_complete(log, s->current_term);
		break;

	default:
		assert(log->type & LOG_TYPE_UNSTABLE_MASK);

	#ifdef TEST_ELECTION_WITH_UNSTABLE_LOG
		if (s->current_term == 1)
			exit(EXIT_SUCCESS);
	#endif

	#ifdef TEST_ELECTION_WITH_UNSTABLE_GROW_LOG
		if ((log->type == LOG_TYPE_GROW_COMPLETE ||
		     log->type == LOG_TYPE_GROW_CHANGE_AVAILABLE) &&
		     s->current_term == 1) {
			exit(EXIT_SUCCESS);
		     }
	#endif

		assert(s->current_term == log->term);
		new = log_malloc_stable(log);
		break;
	}

	if (new) {
		if ((log->type & LOG_TYPE_JOINT_MASK) == 0) {
			server_replace_log(s, new);
			return true;
		}
		if (leader_replace_log(s, new))
			return true;
		
		free(new);
	}
	return false;
}

static void change_to_append_log_in(struct raft_conn *conn)
{
	raft_conn_return_log(conn);
	raft_conn_set_io(conn, RAFT_CONN_STATE_APPEND_LOG_IN,
					sizeof(struct append_entry_res));
}

static void state_append_log_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_APPEND_LOG_OUT:\n");

	struct append_log_req *req = &conn->append_log_req;
	uint64_t machines_size = ntohll(req->machines_size);
	struct iovec iov[2];
	int iov_len;
	if (conn->unio <= machines_size) {
		iov_len = 1;
		uint64_t written = machines_size - conn->unio;
		iov[0].iov_base = (unsigned char *)conn->log->machines + written;
		iov[0].iov_len = conn->unio;
	} else {
		iov_len = 2;
		uint64_t written = sizeof(*req) + machines_size - conn->unio;
		iov[0].iov_base = conn->buffer + written;
		iov[0].iov_len = conn->unio - machines_size;
		iov[1].iov_base = conn->log->machines;
		iov[1].iov_len = machines_size;
	}
	if (raft_conn_full_write_msg(conn, iov, iov_len))
		change_to_append_log_in(conn);
}

static void change_to_append_log_out(struct server *s, struct member *member)
{
	struct raft_conn *conn = &member->conn;
	struct log *log = s->log;
	uint64_t machines_size = MACHINE_SIZE * (uint64_t)(log->old_n + log->new_n);

	struct append_log_req *req = &conn->append_log_req;
	req->cmd = RAFT_CMD_APPEND_LOG;
	req->type = log->type;
	req->machines_size = htonll(machines_size);
	req->term = htonll(s->current_term);
	req->leader_id = htonl(s->id);
	req->follower_id = htonl(member->id);
	req->log_index = htonll(log->index);
	req->log_term = htonll(log->term);
	req->version = htonll(log->version);
	req->next_machine_version = htonll(log->next_machine_version);
	req->next_machine_id = htonl(log->next_machine_id);
	req->new_machine_nr = htonl(log->new_n);
	req->distinct_machines_n = htonll(log->distinct_machines_n);

	uint64_t size = sizeof(*req) + machines_size;
	raft_conn_borrow_log(conn, log, RAFT_CONN_STATE_APPEND_LOG_OUT, size);
	member->next_index = log->index;
	state_append_log_out(conn);
}

static void state_recv_heartbeat_in(struct server *s, struct raft_conn *conn)
{
	/**
	 * RAFT: Figure 2: AppendEntries RPC
	 * 1. Reply false if term < currentTerm (§5.1)
	 * 2. Reply false if log doesn’t contain an entry at prevLogIndex
	 *    whose term matches prevLogTerm (§5.3)
	 * 
	 * Note: we don't have to check prevLogIndex, because leader always
	 * apply the log first before sending heartbeat.
	 */
	struct heartbeat_req *req = &conn->heartbeat_req;
	uint64_t term = ntohll(req->term);
	assert(term <= s->current_term);
	if (term == s->current_term) {
		assert(s->state == SERVER_STATE_FOLLOWER);
		reset_timer(s);
	}
	change_to_recv_entry_out(s, conn);
}

static void change_to_heartbeat_in(struct raft_conn *conn)
{
	raft_conn_set_io(conn, RAFT_CONN_STATE_HEARTBEAT_IN,
					sizeof(struct append_entry_res));
}

static void state_heartbeat_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_HEARTBEAT_OUT:\n");

	if (raft_conn_full_write_buffer(conn, sizeof(struct heartbeat_req)))
		change_to_heartbeat_in(conn);
}

static void change_to_heartbeat_out(struct server *s, struct raft_conn *conn)
{
	struct heartbeat_req *req = &conn->heartbeat_req;
	req->cmd = RAFT_CMD_HEARTBEAT;
	req->term = htonll(s->current_term);

	raft_conn_set_io(conn, RAFT_CONN_STATE_HEARTBEAT_OUT, sizeof(*req));
	state_heartbeat_out(conn);
}

static void change_to_append_entry_out(struct server *s, struct member *member)
{
	assert(s->state == SERVER_STATE_LEADER);
	member->available_since_last_timer_event = true;
	member->append_entry_round = s->leader.replicate_entry_round;

	if (member->next_index <= s->log->index)
		change_to_append_log_out(s, member);
	else
		change_to_heartbeat_out(s, &member->conn);
}

static void state_append_entry_in(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_APPEND_ENTRY_IN:\n");

	if (!raft_conn_full_read_to_buffer(conn, sizeof(struct append_entry_res)))
		return;

	enum raft_conn_state state = conn->state;
	struct append_entry_res *res = &conn->append_entry_res;
	uint64_t term = ntohll(res->term);
	bool applied = res->applied;

	raft_conn_change_to_ready_for_use(conn);

	if (term > s->current_term) {
		server_increase_term(s, term);
		return;
	}

	assert(term == s->current_term && s->state == SERVER_STATE_LEADER);

	struct member *member = container_of(conn, struct member, conn);
	if (state == RAFT_CONN_STATE_APPEND_LOG_IN)
		member->next_index++;

	if (applied && !log_commited(s, member)) {
		convert_to_follower(s);
	} else if (member->append_entry_round == s->leader.replicate_entry_round) {
		if (member->type & MEMBER_TYPE_OLD)
			s->leader.commit_entry_required_old_votes--;
		
		if (member->type & MEMBER_TYPE_NEW)
			s->leader.commit_entry_required_new_votes--;
	} else {
		change_to_append_entry_out(s, member);
	}
}

static void change_to_init_cluster_out(struct raft_conn *conn)
{
	raft_conn_return_log(conn);
	change_to_out_success(conn);
}

static void state_init_cluster_in(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_INIT_CLUSTER_IN:\n");

	struct log *log = conn->log;
	uint64_t machines_size = ntohll(conn->change_cluster_req.machines_size);
	uint64_t readed = machines_size - conn->unio;
	if (!raft_conn_full_read(conn, (unsigned char *)log->machines + readed))
		return;

	if (s->log->index == 0 && log_complete_init(log)) {
		s->id = 1;
		s->current_term = 1;
		assert(s->current_term == log->term);
		set_timer(s, 1);

		s->state = SERVER_STATE_LEADER;
		s->leader.replicate_entry_round = 0;
		s->leader.replicate_entry = true;
		s->leader.entry_commited = true;
		s->leader.available = true;

		must(leader_replace_log(s, log));
	}
	change_to_init_cluster_out(conn);
}

static void change_to_init_cluster_in(struct server *s, struct raft_conn *conn)
{
	struct change_cluster_req *req = &conn->change_cluster_req;
	uint64_t machines_size = ntohll(req->machines_size);
	if (machines_size_valid(machines_size)) {
		struct log *log = log_malloc_init(machines_size);
		if (log) {
			uint64_t preread = RAFT_CONN_BUFFER_SIZE - sizeof(*req);
			unsigned char *machines = conn->buffer + sizeof(*req);
			memcpy(log->machines, machines, preread);
			uint64_t size = machines_size - preread;
			raft_conn_borrow_log(conn, log,
					RAFT_CONN_STATE_INIT_CLUSTER_IN, size);
			state_init_cluster_in(s, conn);
			return;
		}
	}
	raft_conn_free(conn);
}

static void change_to_change_cluster_out(struct raft_conn *conn)
{
	raft_conn_return_log(conn);
	change_to_out_success(conn);
}

static void state_change_cluster_in(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_CHANGE_CLUSTER_IN:\n");

	struct log *log = conn->log;
	uint64_t machines_size = ntohll(conn->change_cluster_req.machines_size);
	uint64_t readed = machines_size - conn->unio;
	unsigned char *machines = (unsigned char *)(log->machines + log->old_n);
	if (!raft_conn_full_read(conn, machines + readed))
		return;

	if (s->state == SERVER_STATE_LEADER &&
	    s->log->type == LOG_TYPE_OLD &&
	    s->log->old_n == log->old_n &&
	    log_complete_change(log, s->log, s->current_term)) {
		leader_replace_log(s, log);

	#ifdef TEST_VOTE_WITH_LOG0
		convert_to_follower(s);
	#endif
	}
	change_to_change_cluster_out(conn);
}

static void change_to_change_cluster_in(struct server *s, struct raft_conn *conn)
{
	struct change_cluster_req *req = &conn->change_cluster_req;
	uint64_t machines_size = ntohll(req->machines_size);
	if (machines_size_valid(machines_size)) {
		uint32_t n = machines_size / MACHINE_SIZE;
		struct log *log = log_malloc_unstable(s->log->old_n, n);
		if (log) {
			uint64_t preread = RAFT_CONN_BUFFER_SIZE - sizeof(*req);
			unsigned char *machines = conn->buffer + sizeof(*req);
			memcpy(log->machines + log->old_n, machines, preread);
			uint64_t size = machines_size - preread;
			raft_conn_borrow_log(conn, log,
				RAFT_CONN_STATE_CHANGE_CLUSTER_IN, size);
			state_change_cluster_in(s, conn);
			return;
		}
	}
	raft_conn_free(conn);
}

static void state_leader_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_LEADER_OUT:\n");

	if (raft_conn_full_write_buffer(conn, sizeof(struct leader_res)))
		change_to_in_cmd(conn);
}

static void change_to_leader_out(struct server *s, struct raft_conn *conn)
{
	uint32_t leader = 0;
	switch (s->state) {
	case SERVER_STATE_LEADER:
		leader = s->id;
		break;
	case SERVER_STATE_FOLLOWER:
		leader = s->follower.leader;
		break;
	case SERVER_STATE_CANDIDATE:
	}

	struct leader_res *res = &conn->leader_res;
	res->lost = true;
	if (leader > 0) {
		// Note: when the cluster is changing and the leader is not in
		// the new-config, there is a possibility that the leader can
		// not be found
		const struct machine *m = log_machines_find(s->log, leader);
		if (m) {
			res->sin6_addr = m->sin6_addr;
			res->sin6_port = m->sin6_port;
			res->lost = false;
		}
	}

	raft_conn_set_io(conn, RAFT_CONN_STATE_LEADER_OUT, sizeof(*res));
	state_leader_out(conn);
}

static void state_cluster_out(struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_CLUSTER_OUT:\n");

	struct cluster_res *res = &conn->cluster_res;
	uint64_t machines_size = ntohll(res->machines_size);
	struct iovec iov[2];
	int iov_len;
	if (conn->unio <= machines_size) {
		iov_len = 1;
		uint64_t written = machines_size - conn->unio;
		iov[0].iov_base = (unsigned char *)conn->log->machines + written;
		iov[0].iov_len = conn->unio;
	} else {
		iov_len = 2;
		uint64_t written = sizeof(*res) + machines_size - conn->unio;
		iov[0].iov_base = conn->buffer + written;
		iov[0].iov_len = conn->unio - machines_size;
		iov[1].iov_base = conn->log->machines;
		iov[1].iov_len = machines_size;
	}
	if (raft_conn_full_write_msg(conn, iov, iov_len)) {
		raft_conn_return_log(conn);
		change_to_in_cmd(conn);
	}
}

static void change_to_cluster_out(struct server *s, struct raft_conn *conn)
{
	struct log *log = s->log;
	uint64_t machines_size = MACHINE_SIZE * (uint64_t)log->old_n;

	struct cluster_res *res = &conn->cluster_res;
	res->type = log->type;
	res->machines_size = htonll(machines_size);
	res->version = htonll(log->version);

	uint64_t size = sizeof(*res) + machines_size;
	raft_conn_borrow_log(conn, log, RAFT_CONN_STATE_CLUSTER_OUT, size);
	state_cluster_out(conn);
}

static void state_connect_in(struct server *s, struct raft_conn *conn)
{
	uint32_t thread_id = ntohl(conn->connect_req.thread_id);
	if (thread_id >= CONFIG_THREAD_NR) {
		raft_conn_free(conn);
	} else {
		epoll_del(s->epfd, conn->sockfd);
		thread_dispatch(thread_id, conn->sockfd);
		free(conn);
	}
}

static void state_in_cmd(struct server *s, struct raft_conn *conn)
{
	uint64_t readed = RAFT_CONN_BUFFER_SIZE - conn->unio;
	if (!raft_conn_read(conn, conn->buffer + readed))
		return;

	enum raft_cmd cmd = conn->buffer[0];
	if (!conn->admin && cmd < RAFT_CMD_ADMIN_DIVIDER) {
		raft_conn_free(conn);
		return;
	}

	readed = RAFT_CONN_BUFFER_SIZE - conn->unio;

	switch (cmd) {
	case RAFT_CMD_REQUEST_VOTE:
		debug_printf("RAFT_CMD_REQUEST_VOTE:\n");
		if (readed == sizeof(struct request_vote_req))
			state_vote_in(s, conn);

		break;
	case RAFT_CMD_APPEND_LOG:
		debug_printf("RAFT_CMD_APPEND_LOG:\n");
		if (readed == RAFT_CONN_BUFFER_SIZE)
			change_to_recv_log_in(s, conn);

		break;
	case RAFT_CMD_HEARTBEAT:
		debug_printf("RAFT_CMD_HEARTBEAT:\n");
		if (readed == sizeof(struct heartbeat_req))
			state_recv_heartbeat_in(s, conn);

		break;
	case RAFT_CMD_INIT_CLUSTER:
		debug_printf("RAFT_CMD_INIT_CLUSTER:\n");
		if (readed == RAFT_CONN_BUFFER_SIZE)
			change_to_init_cluster_in(s, conn);

		break;
	case RAFT_CMD_CHANGE_CLUSTER:
		debug_printf("RAFT_CMD_CHANGE_CLUSTER:\n");
		if (readed == RAFT_CONN_BUFFER_SIZE)
			change_to_change_cluster_in(s, conn);

		break;
	case RAFT_CMD_LEADER:
		assert(readed == 1);
		debug_printf("RAFT_CMD_LEADER:\n");
		change_to_leader_out(s, conn);
		break;
	case RAFT_CMD_CLUSTER:
		assert(readed == 1);
		debug_printf("RAFT_CMD_CLUSTER:\n");
		change_to_cluster_out(s, conn);
		break;
	case RAFT_CMD_CONNECT:
		debug_printf("RAFT_CMD_CONNECT:\n");
		if (readed == sizeof(struct connect_req))
			state_connect_in(s, conn);

		break;
	case RAFT_CMD_AUTHORITY:
		assert(readed == 1);
		debug_printf("RAFT_CMD_AUTHORITY:\n");
		list_add(&s->authority_list, &conn->authority_node);
		conn->authority_pending_nr = 0;
		conn->authority_processing_nr = 0;
		conn->authority_succeed_nr = 0;
		change_to_authority_out(s, conn);
		break;
	default:
		debug_printf("unrecognized command.........................\n");
		assert(0 == 1);
		raft_conn_free(conn);
	}
}

static void change_to_ready_for_use(struct server *s, struct raft_conn *conn)
{
	switch (s->state) {
	case SERVER_STATE_LEADER:
		struct member *m = container_of(conn, struct member, conn);
		change_to_append_entry_out(s, m);
		break;
	case SERVER_STATE_CANDIDATE:
		change_to_request_vote_out(s, conn);
		break;
	case SERVER_STATE_FOLLOWER:
	}
}

#ifdef CONFIG_KERNEL_TLS
static void state_tls_server_handshake(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE:\n");

	int ret = tls_handshake(&conn->session);
	if (ret == GNUTLS_E_SUCCESS) {
		tls_deinit(&conn->session);
		change_to_in_cmd(conn);
		state_in_cmd(s, conn);
	} else if (ret != GNUTLS_E_AGAIN) {
		raft_conn_free(conn);
	} else if (tls_record_require_write(&conn->session)) {
		conn->state = RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_OUT;
	} else {
		// FIXME: block once is expected,
		// but request from gnutls client will block twice
		conn->state = RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_IN;
	}
}

static void state_tls_client_handshake(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE:\n");

	int ret = tls_handshake(&conn->session);
	if (ret == GNUTLS_E_SUCCESS) {
		tls_deinit(&conn->session);
		change_to_ready_for_use(s, conn);
	} else if (ret != GNUTLS_E_AGAIN) {
		raft_conn_clear(conn);
	} else if (tls_record_require_write(&conn->session)) {
		conn->state = RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_OUT;
	} else {
		conn->state = RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_IN;
	}
}

static void change_to_conn_established(struct server *s, struct raft_conn *conn)
{
	assert(conn->state == RAFT_CONN_STATE_IN_PROGRESS);

	struct member *m = container_of(conn, struct member, conn);
	if (tls_init_client(&conn->session, conn->sockfd, m->sin6_addr)) {
		conn->state = RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_OUT;
		state_tls_client_handshake(s, conn);
	} else {
		raft_conn_clear(conn);
	}
}
#else
static void change_to_conn_established(struct server *s, struct raft_conn *conn)
{
	change_to_ready_for_use(s, conn);
}
#endif

static void state_in_progress(struct server *s, struct raft_conn *conn)
{
	debug_printf("RAFT_CONN_STATE_IN_PROGRESS:\n");

	int optval;
	socklen_t optlen = sizeof(int);
	int ret = getsockopt(conn->sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen);
	if (ret == 0 && optval == 0)
		change_to_conn_established(s, conn);
	else
		raft_conn_clear(conn);
}

void member_connect(struct server *s, struct member *m)
{
	struct raft_conn *conn = &m->conn;
	assert(conn->state == RAFT_CONN_STATE_NOT_CONNECTED);

	char str[INET6_ADDRSTRLEN] __attribute__((unused));
	debug_printf("try connect: id: %d addr: %s port: %d\n", m->id,
			member_string_address(m, str), ntohs(m->sin6_port));

	int sockfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (sockfd == -1)
		return;

	int opt = 1;
	struct linger ling = {0, 0};
	if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling))  ||
	    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) ||
	    !epoll_add(s->epfd, sockfd, (uint64_t)conn | 1)) {
		close(sockfd);
		return;
	};

	struct sockaddr_storage __addr;
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&__addr;
	addr->sin6_family = AF_INET6;
	addr->sin6_port = m->sin6_port;
	addr->sin6_flowinfo = 0;
	addr->sin6_addr = m->sin6_addr;
	addr->sin6_scope_id = 0;

	conn->sockfd = sockfd;
	int ret = connect(sockfd, (struct sockaddr *)addr, sizeof(*addr));
	if (ret == 0) {
		conn->state = RAFT_CONN_STATE_IN_PROGRESS;
		change_to_conn_established(s, conn);
	} else if (errno == EINPROGRESS) {
		conn->state = RAFT_CONN_STATE_IN_PROGRESS;
	} else {
		close(sockfd);
	}
}

static void replicate_entry(struct server *s)
{
	assert(s->state == SERVER_STATE_LEADER);

	struct raft_conn *curr;
	list_for_each_entry(curr, &s->authority_list, authority_node) {
		curr->authority_processing_nr += curr->authority_pending_nr;
		curr->authority_pending_nr = 0;
	}

	struct cluster *cl = s->cluster;
	s->leader.commit_entry_required_old_votes = cl->require_old_votes;
	s->leader.commit_entry_required_new_votes = cl->require_new_votes;
	s->leader.replicate_entry_round++;
	s->leader.replicate_entry = false;
	s->leader.entry_commited = false;

	for (uint32_t i = 0; i < cl->members_n; i++) {
		struct member *m = cl->members + i;
		switch (m->conn.state) {
		case RAFT_CONN_STATE_READY_FOR_USE:
			change_to_append_entry_out(s, m);
			break;
		case RAFT_CONN_STATE_NOT_CONNECTED:
			member_connect(s, m);
			break;
		default:
			break;
		}
	}
}

/**
 * RAFT: Figure 2: Rules for Servers: Candidates (§5.2)
 * • On conversion to candidate, start election:
 * • Increment currentTerm
 * • Vote for self
 * • Reset election timer
 * • Send RequestVote RPCs to all other servers
 * 
 * RAFT: 5.2 Leader election
 * Each candidate
 * restarts its randomized election timeout at the start of an
 * election, and it waits for that timeout to elapse before
 * starting the next election; this reduces the likelihood of
 * another split vote in the new election.
 */
static void convert_to_candidate(struct server *s)
{
	if (s->state == SERVER_STATE_FOLLOWER) {
		struct cluster *cl = cluster_malloc(s->log, s->id);
		if (cl == NULL)
			return;

		s->state = SERVER_STATE_CANDIDATE;
		server_replace_cluster(s, cl);
	}

	struct cluster *cl = s->cluster;
	s->current_term++;
	s->candidate.required_old_votes = cl->require_old_votes;
	s->candidate.required_new_votes = cl->require_new_votes;
	reset_timer_hard(s);
	debug_printf("convert to candidate: %ld\n", s->current_term);

	for (uint64_t i = 0; i < cl->members_n; i++) {
		struct member *m = &cl->members[i];
		struct raft_conn *conn = &m->conn;
		switch (conn->state) {
		case RAFT_CONN_STATE_READY_FOR_USE:
			change_to_request_vote_out(s, conn);
			break;
		case RAFT_CONN_STATE_NOT_CONNECTED:
			member_connect(s, m);
			break;
		default:
			break;
		}
	}
}

static void start_election(struct server *s)
{
	convert_to_candidate(s);
}

static bool leader_timer_ticked(struct server *s)
{
	bool available_changed = !s->leader.available;
	struct cluster *cl = s->cluster;
	uint32_t old_available = 0;
	uint32_t new_available = 0;
	for (uint64_t i = 0; i < cl->members_n; i++) {
		struct member *m = cl->members + i;
		if (m->available_since_last_timer_event == m->available) {
			m->unstable_round = 0;
		} else {
			m->unstable_round++;
			if (m->unstable_round >= 10) {
				m->available = !m->available;
				m->unstable_round = 0;
				available_changed = true;
			}
		}
		m->available_since_last_timer_event = false;

		if (m->available) {
			if (m->type & MEMBER_TYPE_OLD)
				old_available++;

			if (m->type & MEMBER_TYPE_NEW)
				new_available++;
		}
	}

	if (old_available < cl->require_old_votes ||
	    new_available < cl->require_new_votes) {
		return false;
	}

	if ((s->log->type & LOG_TYPE_UNSTABLE_MASK) == 0 && available_changed) {
		s->leader.available = true;
		debug_printf("leader change available:\n");
		return leader_change_available(s);
	}

	return true;
}

static void process_timer_event(struct server *s)
{
	uint64_t exp;
	size_t tn __attribute__((unused)) = read(s->timerfd, &exp, sizeof(exp));
	assert(tn == sizeof(exp) && exp > 0);
	/* ignore exp, we may miss some timer event */
	s->timer_ticks++;
	debug_printf("timer ticks: %d\n", s->timer_ticks);

	if (s->state == SERVER_STATE_LEADER) {
		if (leader_timer_ticked(s))
			s->leader.replicate_entry = true;
		else
			convert_to_follower(s);
	} else if (election_timeout(s)) {
		/**
		 * RAFT:
		 * Candidates (§5.2):
		 * • If election timeout elapses: start new election
		 * 
		 * RAFT: Figure 2: Rules for Servers
		 * Followers (§5.2):
		 * • Respond to RPCs from candidates and leaders
		 * • If election timeout elapses without receiving AppendEntries
		 * RPC from current leader or granting vote to candidate:
		 * convert to candidate
		 */
		start_election(s);
	}
}

static void process(struct server *s, struct raft_conn *conn)
{
	switch (conn->state) {
	case RAFT_CONN_STATE_IN_PROGRESS:
		state_in_progress(s, conn);
		break;
#ifdef CONFIG_KERNEL_TLS
	case RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_IN:
	case RAFT_CONN_STATE_TLS_CLIENT_HANDSHAKE_OUT:
		state_tls_client_handshake(s, conn);
		break;
	case RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_IN:
	case RAFT_CONN_STATE_TLS_SERVER_HANDSHAKE_OUT:
		state_tls_server_handshake(s, conn);
		break;
#endif
	case RAFT_CONN_STATE_IN_CMD:
		state_in_cmd(s, conn);
		break;
	case RAFT_CONN_STATE_REQUEST_VOTE_OUT:
		state_request_vote_out(conn);
		break;
	case RAFT_CONN_STATE_REQUEST_VOTE_IN:
		state_request_vote_in(s, conn);
		break;
	case RAFT_CONN_STATE_VOTE_OUT:
		state_vote_out(conn);
		break;
	case RAFT_CONN_STATE_APPEND_LOG_OUT:
		state_append_log_out(conn);
		break;
	case RAFT_CONN_STATE_HEARTBEAT_OUT:
		state_heartbeat_out(conn);
		break;
	case RAFT_CONN_STATE_APPEND_LOG_IN:
	case RAFT_CONN_STATE_HEARTBEAT_IN:
		state_append_entry_in(s, conn);
		break;
	case RAFT_CONN_STATE_RECV_LOG_IN:
		state_recv_log_in(s, conn);
		break;
	case RAFT_CONN_STATE_RECV_ENTRY_OUT:
		state_recv_entry_out(conn);
		break;
	case RAFT_CONN_STATE_INIT_CLUSTER_IN:
		state_init_cluster_in(s, conn);
		break;
	case RAFT_CONN_STATE_CHANGE_CLUSTER_IN:
		state_change_cluster_in(s, conn);
		break;
	case RAFT_CONN_STATE_LEADER_OUT:
		state_leader_out(conn);
		break;
	case RAFT_CONN_STATE_CLUSTER_OUT:
		state_cluster_out(conn);
		break;
	case RAFT_CONN_STATE_OUT_SUCCESS:
		state_out_success(conn);
		break;
	case RAFT_CONN_STATE_AUTHORITY_OUT:
		if (state_authority_in(s, conn))
			state_authority_out(conn);
		break;
	case RAFT_CONN_STATE_AUTHORITY_PENDING:
		state_authority_in(s, conn);
		break;
	case RAFT_CONN_STATE_NOT_CONNECTED:
	case RAFT_CONN_STATE_READY_FOR_USE:
	case RAFT_CONN_OUTGOING_INCOMING_DIVIDER:
	case RAFT_CONN_STATE_AUTHORITY_DIVIDER:
#ifdef CONFIG_KERNEL_TLS
	case RAFT_CONN_STATE_TLS_CLIENT_DIVIDER:
	case RAFT_CONN_STATE_TLS_SERVER_DIVIDER:
#endif
		__builtin_unreachable();
	}
}

static void conn_authority_approved(struct server *s, struct raft_conn *conn)
{
	conn->authority_succeed_nr += conn->authority_processing_nr;
	conn->authority_processing_nr = 0;

	if (conn->state == RAFT_CONN_STATE_AUTHORITY_PENDING && conn->authority_succeed_nr > 0)
		change_to_authority_out(s, conn);
}

static void authority_approved(struct server *s)
{
	s->leader.entry_commited = true;
	struct raft_conn *curr, *temp;
	list_for_each_entry_safe(curr, temp, &s->authority_list, authority_node)
		conn_authority_approved(s, curr);
}

static void loop_forever(struct server *s, int sockfd, int admin_sockfd, in_port_t port)
{
	while (true) {
		struct epoll_event *events = s->events;
		int n;
		if (sockfd != -1 && admin_sockfd != -1) {
			n = epoll_wait(s->epfd, events, SERVER_MAX_EPOLL_EVENTS, -1);
		} else {
			sleep(3);
			if (sockfd == -1)
				sockfd = listen_user(s->epfd, port);

			if (admin_sockfd == -1)
				admin_sockfd = listen_admin(s->epfd, port);

			/* in case listen failed, don't wait epoll */
			n = epoll_wait(s->epfd, events, SERVER_MAX_EPOLL_EVENTS, 0);
		}

		for (int i = 0; i < n; i++) {
			static_assert(alignof(struct raft_conn) % 8 == 0);

			switch (events[i].data.u64) {
			case TIMER_EVENT_U64:
				process_timer_event(s);
				break;
			case ACCEPT_EVENT_U64:
				if (!_accept(s, sockfd, false)) {
					close(sockfd);
					sockfd = -1;
				}
				break;
			case ACCEPT_ADMIN_EVENT_U64:
				if (!_accept(s, admin_sockfd, true)) {
					close(admin_sockfd);
					admin_sockfd = -1;
				}
				break;
			default:
				struct raft_conn *conn;
				if (events[i].data.u64 & 1) {
					/* outgoing connection */
					conn = (void *)(events[i].data.u64 & ~1);
					if (!cluster_has_conn(s->cluster, conn))
						break;
				} else {
					/* incoming connection */
					conn = events[i].data.ptr;
				}

				if (events[i].events & ~(EPOLLIN | EPOLLOUT))
					raft_conn_free_or_clear(conn);
				else if (events[i].events & conn->state)
					process(s, conn);
			}
		}

		if (s->state == SERVER_STATE_LEADER) {
			if (!s->leader.entry_commited &&
				s->leader.commit_entry_required_old_votes <= 0 &&
			   	s->leader.commit_entry_required_new_votes <= 0) {
				authority_approved(s);
			}
			if (s->leader.entry_commited && s->leader.replicate_entry)
				replicate_entry(s);
		} else {
			struct raft_conn *curr, *temp;
			list_for_each_entry_safe(curr, temp, &s->authority_list, authority_node)
				raft_conn_free(curr);
		}

		while (s->stale_cluster) {
			struct cluster *stale = s->stale_cluster;
			s->stale_cluster = stale->next_stale;
			cluster_free(stale);
		}
	}
}

static struct server __s;

void must_service_run(int port)
{
	must(threads_run());
	must_server_init(&__s);

	int sockfd = listen_user(__s.epfd, port);
	must(sockfd != -1);
	int admin_sockfd = listen_admin(__s.epfd, port);
	must(admin_sockfd != -1);

	loop_forever(&__s, sockfd, admin_sockfd, port);
}
