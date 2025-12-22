.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

RAFT PAPER
==========
https://raft.github.io/raft.pdf

OUR CHANGE
==========

cluster membership changes
--------------------------

It's about the third issue: removed servers (those not in Cnew ) can disrupt the cluster.

The paper says "if a server receives a RequestVote RPC within the minimum
election timeout of hearing from a current leader, it does not update its term
or grant its vote.", there is a corner case will broke the cluster.

- cluster (A, B, C, D)
- A is leader
- A lost connections to (C, D)

A is not able to commit any logs now, and (C, D) will election timeout, but
B is able to receive heartbeat from A, so it will disregard RequestVote RPCs,
and A can't receive any RequestVote RPCs because of the broken network, then
we will have a broken cluster even we have three healthy machines (B, C, D).

Our change to this is once leader found himself can't get heartbeats to majority
of it's cluster, it steps down.

AppendEntries RPC
-----------------

We separate AppendEntries RPC into two RPCs Heartbeat and AppendLog, because a
follower that not warmed up can't commit LOG_TYPE_GROW_TRANSFORM, we don't want
the leader send that log over and over again.

Cluster membership changes
--------------------------

We require cluster member size to be power of two, this limitation can simplify
the process of smooth cluster change.

Log compaction
--------------

Our compaction method is extreme aggressive, we only keep one log on every
servers, because we only have to maintain cluster members information.

OTHER
=====

unstable node disrupt the cluster
---------------------------------

A node with slow network will continue disrupt the cluster, because it will
always election timeout and start election increase its term, when leader
append-entry rpc arrives, leader will get a higher term, then leader step down.

There is nothing we can do about it, but we designed machines to have a property
called stability, it can help maintainers to find out the unstable nodes.
