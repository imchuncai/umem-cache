.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

==========
UMEM-CACHE
==========
UMEM-CACHE is an user-space key/value in-memory cache.

We compared the performance and speed of UMEM-CACHE, MEMCACHED and REDIS with
tests. In most tests, UMEM-CACHE shows better cache hit rate and less memory
usage. In the test of caching small objects, UMEM-CACHE is about 20% faster
than REDIS.

Multilingual 多语言
==================

- `简体中文 <https://github.com/imchuncai/umem-cache/tree/master/Documentation/translations/zh_CN/README.rst>`_

REQUIREMENTS
============
1. 64bit linux
2. linux 2.6.32 or later (not verified)
3. 4k page size
4. Go installed and 100MB memory is required for test

BUILD
=====
GCC is required for build.

FOR PRODUCTION
--------------
config config.h as needed
::

	cd umem-cache
	make
	make check
	./umem-cache

FOR DEBUGGING
-------------
::

	config config.h as needed
	cd umem-cache
	make debug
	./umem-cache

TEST
====
see `umem-cache-client-Go <https://github.com/imchuncai/umem-cache-client-Go>`_,
includes functional tests, performance tests, benchmark tests and comparison
information with MEMCACHED and REDIS.

FEATURES
========

ARBITRARY KEY AND VALUE
-----------------------
Keys and values are arbitrary byte arrays, except key is at most 255 bytes.

ANTI-DOGPILING
---------------
We have built-in anti-dogpiling.

Dogpiling is the effect you get when a hot key first coming into the cache, and
everyone rushes to cache it, which puts pressure on the fallback database. The
anti-dogpiling tries to mitigate this by only let one connection have the
permission to do the cache work.

SET NEVER FAIL
--------------
Set command will never fail.

NO EXTRA THREAD
---------------
There is no extra thread except main thread and user required working threads.

CLUSTER
=======
There is no built-in cluster solution, but we have designed a VERSION system,
you can easily build one yourself.

REQUIREMENTS
------------
1. A system S to store and distribute server cluster information.

HOW TO WORK
-----------

SYSTEM S
~~~~~~~~
1. start all new added server and stop all no longer needed.
2. update server status with increased server version.
3. notify the clients, may be just establish connections to each server with
   the new version.

CLIENT SIDE
~~~~~~~~~~~
If the connection is broken, client side should always try to get the server
status from S first before reconnection.

1. received new server information from S.
2. close all old connections.
3. publish new connections with new server version.
4. client ready to continue the commands after all server connected.

CLIENT PROTOCOL
===============
- Client should connect to the server using tcp over ipv6.
- See conn.h for command code.
- See errno.h for errno.

=ERRNO=
-------
::

	[ IN  ]
	[errno]
	[  1  ]

CONNECT
-------
::

	[        OUT        ]
	[thread-id] [version] [=ERRNO=]
	[    4    ] [   4   ]

=CMD=
-----
::

	[             OUT             ]
	[command] [key-size] [  key   ]
	[   1   ] [   1    ] [key-size]

=SET=
-----
::

			   [[set] == TRUE]
	[       OUT      ] [     OUT     ]
	[set] [value-size] [    value    ] [=ERRNO=]
	[ 1 ] [    8     ] [ value-size  ]

	Note: [=ERRNO=] is always E_NONE, is required for connection reuse

CMD-GET-OR-SET
--------------
::

				     [[errno] == E_NONE] [[errno] == E_GET_MISS]
		[        IN        ] [       IN        ]
	[=CMD=] [errno] [value-size] [      value      ] [        =SET=        ]
		[  1  ] [    8     ] [   value-size    ]

CMD-DEL
-------
::

	[=CMD=] [=ERRNO=]
