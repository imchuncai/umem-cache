.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

==========
UMEM-CACHE
==========

UMEM-CACHE is a key/value memory cache work in user space, it has built-in
anti-dogpiling, tls and raft cluster solution.

Multilingual 多语言
==================

- `简体中文 <https://github.com/imchuncai/umem-cache/tree/master/Documentation/translations/zh_CN/README.rst>`_

RUNNING REQUIREMENTS
====================

- 64bit linux
- linux 3.15 or later (not verified)
- 4k page size
- linux 4.17 or later is required for tls (not verified)
- require ktls enabled for tls

BUILD
=====

- GCC is required
- gnutls 3.5.6 or later is required for tls
- Go is required for make check

::

	cd umem-cache
	make RAFT=0 TLS=0 THREAD_NR=4 MAX_CONN=512 MEM_LIMIT=104857600 TCP_TIMEOUT=3000
	make check RAFT=0 TLS=0
	./umem-cache 10047

TESTS
=====

- functional tests: `umem-cache-client-Go <https://github.com/imchuncai/umem-cache-client-Go>`_
- benchmark  tests: `umem-cache-benchmark <https://github.com/imchuncai/umem-cache-benchmark>`_
- cluster benchmark tests: testing is scheduled for the end of 2026.

FEATURES
========

ARBITRARY KEY AND VALUE
-----------------------

Keys and values are arbitrary byte arrays, except key is at most 255 bytes for
singleton and at most 247 bytes for cluster.

ANTI-DOGPILING
--------------

We have built-in anti-dogpiling.

Dogpiling is the effect you get when a hot key first coming into the cache, and
everyone rushes to cache it, which puts pressure on the fallback database. The
anti-dogpiling tries to mitigate this by only let one connection have the
permission to do the cache work.

CLUSTER SOLUTION
----------------

We have built-in cluster solution, which is implement under the guidance of the
raft consensus algorithm, ensures consistency and availability.

- NOTE: check our changes to raft at `raft-paper.rst <https://github.com/imchuncai/umem-cache/tree/master/Documentation/raft-paper.rst>`_ .
- NOTE: every machine in the cluster should be built using the same THREAD_NR and MEM_LIMIT.
- WARNING: ipv6 link-local address is not supported.
- WARNING: do not add servers that are already in use to the cluster.
- WARNING: do not restart nodes, instead, remove it from the cluster and then add it back.
- WARNING: domain name resolution is not supported.

CLIENT PROTOCOL
===============

- Client should connect to the server using tcp over ipv6.

CONNECT
-------
::

	[        OUT         ] [ IN  ]
	[reserved] [thread-id] [error]
	[   4    ] [    4    ] [  1  ]

	NOTE: [error] is always 0

CLIENT PROTOCOL (BUILD WITH RAFT)
=================================

- Client should connect to the server using tcp over ipv6
- Client should use admin port(running_port + 1), if the proto is tagged (ADMIN)
- Check `raft_proto.h <https://github.com/imchuncai/umem-cache/tree/master/raft_proto.h>`_ for command code
- Check `cluster_cache_flow.txt <https://github.com/imchuncai/umem-cache/tree/master/Documentation/cluster_cache_flow.txt>`_ for cache flow

CLIENT HASH
-----------

We use client side hash to dispatch keys.

Keys in the cluster differ from those in the singleton, they have an 8-byte
prefix version.

Client should use MurmurHash3_x64_128 with seed 74 as hash method, and use
the first 64 bits of the hash value as a little-endian integer to dispatch keys
to threads. The key's version is the version of the machine the thread belongs
to. If the machine of the thread is unavailable, the key should be dispatched to
the same thread of the next available machine, but the version of the key should
remain unchanged. And the machines should be considered as arranged in a ring.

=MACHINE=
---------
::

	[sin6-address] [sin6-port] [reserved] [id] [stability] [version]
	[     16     ] [    2    ] [   2    ] [4 ] [    8    ] [   8   ]

	NOTE: machine with lower [stability] is more stable
	NOTE: machine is available if ((stability & 1) == 1)

REQUEST-VOTE (INTERNAL)
-----------------------
::

	[                              OUT                              ] [      IN      ]
	[command] [reserved] [candidate-id] [term] [log-index] [log-term] [term] [granted]
	[   1   ] [   3    ] [     4      ] [ 8  ] [    8    ] [   8    ] [ 8  ] [   1   ]

APPEND-LOG (INTERNAL)
---------------------
::

	[                                   OUT                                    ]
	[command] [type] [reserved] [machines-size] [term] [leader-id] [follower-id]
	[   1   ] [ 1  ] [   6    ] [      8      ] [ 8  ] [    4    ] [     4     ]

	[                                  OUT                                  ]
	[log-index] [log-term] [version] [next-machine-version] [next-machine-id]
	[    8    ] [   8    ] [   8   ] [         8          ] [       4       ]

	[                        OUT                         ] [      IN      ]
	[new-machine-nr] [distinct_machines_n] [   machines  ] [term] [applied]
	[      4       ] [         8         ] [=MACHINE= * n] [ 8  ] [   1   ]

HEARTBEAT / LOG-APPLIED-CHECK (INTERNAL)
----------------------------------------
::

	[          OUT            ] [      IN      ]
	[command] [reserved] [term] [term] [applied]
	[   1   ] [   7    ] [ 8  ] [ 8  ] [   1   ]

INIT-CLUSTER / CHANGE-CLUSTER (ADMIN)
-------------------------------------
::

	[                       OUT                        ] [ IN  ]
	[command] [reserved] [machines-size] [   machines  ] [error]
	[   1   ] [   7    ] [      8      ] [=MACHINE= * n] [  1  ]

	NOTE: [error] is always 0, client should check whether the cluster is really changed
	NOTE: n should be a power of 2 and at least 4
	NOTE: use duplicate machines is not allowed
	NOTE: for adjust, change majority members is not allowed
	NOTE: for grow, only append the same number of members is allowed
	NOTE: for shrink, only reduce the number of members by half is allowed

LEADER
------
::

	[  OUT  ] [                    IN                     ]
	[command] [sin6-address] [sin6-port] [error] [reserved]
	[   1   ] [     16     ] [    2    ] [  1  ] [   1    ]

	NOTE: [error] 0 for success, 1 for lost leader

CLUSTER
-------
::

	[  OUT  ] [                           IN                            ]
	[command] [type] [reserved] [machines-size] [version] [  machines   ]
	[   1   ] [ 1  ] [   7    ] [      8      ] [   8   ] [=MACHINE= * n]

NOTE: check `log.h <https://github.com/imchuncai/umem-cache/tree/master/log.h>`_ for type

CONNECT
-------
::

	[             OUT              ] [ IN  ]
	[command] [reserved] [thread-id] [error]
	[   1   ] [   3    ] [    4    ] [  1  ]

	NOTE: [error] is always 0

=APPROVAL=
----------
::

	[           IN           ]
	[version] [approval-count]
	[   8   ] [      8       ]

AUTHORITY
---------
::

                               [ ASYNC  ] [  ASYNC   ]
	[  OUT  ]              [  OUT   ]
	[command] [=APPROVAL=] [reserved] [=APPROVAL=]
	[   1   ]              [   1    ]

	WARNING: client set TCP_NODELAY may hurt server performance

CACHE PROTOCOL
==============

Use the following protocol to interact with the thread after connecting to the target thread.

- check `conn.h <https://github.com/imchuncai/umem-cache/tree/master/conn.h>`_ for command code

=CMD=
-----
::

	[             OUT             ]
	[command] [key-size] [  key   ]
	[   1   ] [   1    ] [key-size]

=SET=
-----
::

	[          OUT          ] [ IN  ]
	[value-size] [  value   ] [error]
	[    8     ] [value-size] [  1  ]

	NOTE: [error] is always 0

CMD-GET-OR-SET
--------------
::

				     [[error] == 0] [[error] == 1]
		[        IN        ] [     IN     ]
	[=CMD=] [value-size] [error] [   value    ] [   =SET=    ]
		[    8     ] [  1  ] [ value-size ]

CMD-DEL
-------
::

                [ IN  ]
	[=CMD=] [error]
	        [  1  ]

	Note: [error] is always 0
