.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

=================
Cache Consistency
=================

Cache consistency is the most critical feature of Umem-cache, and the design of
the cache protocol and cluster revolves around it. Cache consistency can reduces
the complexity of software development, because you don't have to worry about
the stale data. This is something that eventual consistency cannot achieve.

Choosing cache consistency comes at a cost: we sacrificed availability. That is,
if Umem-cache goes down, even if your other services are still working, the
entire system will be unusable. However, don't worry, we have a cluster solution
based on the Raft consensus algorithm, which provides very high fault tolerance.

Caching Protocol
================

Our caching protocol provides two commands: GetOrSet and Delete. Yes, we've
combined Get and Set.

Cache consistency is achieved by ensuring that the following events occurs at
the moment the key in the database finishes updating:

- No connection is reading the key.
- All connections that read the key before this point but did not complete the Set have been closed.
- The key does not exist in Umem-cache.

Next, I will explain in detail:

Cache Hit
---------

When a cache hit occurs, Umem-cache returns the data directly.

However, if the key is locked because it is being set, the get operation will
set an expiration time for the lock and queue up for the value. Additionally,
if the lock expires, the connection holding the lock will be closed, and the
lock will be handed over to the next connection in the queue.

::

	client              Umem-cache              Database
	         get key
	|---------------------->|
	                        | 1. wait lock
	       return value     |
	|<----------------------|

Cache Miss
----------

When a cache miss occurs, we lock the key and then try to set it and other get
requests will be queued and wait. This mechanism is to prevent dogpiling.

You may have noticed that Umem-cache doesn't respond to Set, which saves us
from one network transfer. This is because it is highly likely to succeed, and
even if it fails, it's not a big deal.

::

	client              Umem-cache              Database
	         get key
	|---------------------->|
	                        | 1. lock key
	       return miss      |
	|<----------------------|

                             get key
        |--------------------------------------------->|
	                                               | 2. wait read lock
		           return value                |
	|<---------------------------------------------|


        |---------------------->| 3. set value
	                        | 4. unlock key

Update Or Delete
----------------

The transaction and lock ensure that the key is not cached or on the way setting
at the time the database update is complete.

Placing step 4 at the end of the transaction is best practice because if the
key is already cached, all Get requests before this step will hit instead of
waiting in th queue. 

::

	client              Umem-cache              Database
               
	|--------------------------------------------->| 1. start transaction
	                                               | 2. lock key for update
	|<---------------------------------------------| 3. update or delete key

               delete key
	|---------------------->| 4. delete key or close the connection that	    
        |<----------------------|    holds the lock

	|--------------------------------------------->| 5. commit transaction

cluster
=======

The cluster's load balance relies on client-side hashing. In cluster mode,
each key have an 8-byte prefix version, which is the same as the version of
the member it hashed to. Once the member which the key is hashed to changes,
the version of the corresponding member will increase. And this version number
is maintained by the Raft consensus protocol, and we guarantee that all members
have different version numbers. Therefore, once the member which the key is
hashed to changes, the key on the old member will automatically become invalid
due to the version change. This mechanism ensures that it is impossible for two
members to have the same valid key at the same time.

The cluster also has a version number, which can be used to quickly determine
whether the cluster has changed. This version number is also maintained by the
Raft consensus protocol. Every time cluster member changes or a member
availability changes, the cluster version will increase. Therefore, the increase
of the cluster version indicates that the hash distribution of the keys in the
cluster has changed.

Our cluster protocol provides three commands: Leader, Authority, and Cluster.
Each caching command obtains execution authorization from the cluster, which is
executed via the Authority command. The Authority command returns the cluster
version.

The following is the client-side implementation:

::

	                                   -------
	                                   |begin|
	                                   -------
	                                      |
	                                      v
	                       ------------------------------
	                       |ask random server for leader|<----------
	                       ------------------------------          |
	                                      |                        |
	                                      v                        |
	                  ----------------------------------------     |
	                  |ask leader for cluster, get version V1|     |
	                  ----------------------------------------     |
	                                      |                        |
	                      ---------------------------------        |
	                      |            parallel           |        |
	                      v                               v        |
	 -------------------------------------------      ----------   |
	 |ask leader for authority, get version V2 |      |do cache|   |
	 -------------------------------------------      ----------   |
	                      |                               |        |
	                      ---------------------------------        |
	                                      |                        |
	                                      v                        |
	                                     / \                       |
	                                    /   \                      |
	                                   /     \                     |
	                                  /V1==V2 \                    |
	                                  \       /--------false--------
	                                   \     /
	                                    \   /
	                                     \ /
	                                      |
	                                     true
	                                      |
	                                      v
	                                    -----
	                                    |end|
	                                    -----
