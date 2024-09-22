.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

==========
UMEM-CACHE
==========
UMEM-CACHE is an user-space key/value in-memory cache.

REQUIREMENTS
============
1. 64bit linux
2. linux 2.6.32 or later (not verified)
3. 4k page size
4. Go and 2GB memory is required for test

BUILD
=====
GCC is required for build.

FOR PRODUCTION
--------------
::

	config config.h as needed
	cd umem-cache
	make
	make test

FOR DEBUGGING
-------------
::

	config config.h as needed
	cd umem-cache
	make debug

TEST
====
see https://github.com/imchuncai/umem-cache-client-Go, includes functional tests,
performance tests, benchmark tests and comparison information with MEMCACHED.

DESIGN
======

SET NEVER FAIL
--------------
Set command will never fail unless mmap() system call fails. We achieve this by
reserve some space for each size of memory cache, once we are lack of memory,
we can always evict one (or two) object to make room.

NO EXTRA THREAD
---------------
There is no extra thread except main thread and user required working threads.

CLUSTER
=======
There is no built in cluster solution, but we have designed a VERSION system,
you can easily build one yourself.

REQUIREMENTS
------------
1. A distributed coordination system S to store and distribute server status.

HOW TO WORK
-----------

SERVER SIDE
~~~~~~~~~~~
1. start all new added server and stop all no longer needed.
2. update server status with increased server version.

CLIENT SIDE
~~~~~~~~~~~
If the connection is broken, client side should always try to get the server
status from S first before reconnection.

1. received new server status from S.
2. close all old connections.
3. publish new connections with new server version.
4. client ready to continue the commands after all server connected.


CLIENT PROTOCOL
===============
- Client should connect to the server using tcp over ipv6.
- See conn.h for command code and flag.
- See errno.h for errno.

ERRNO
-----
::

	[ IN  ]
	[errno]
	[  1  ]

CONNECT
-------
::

	[        OUT        ]
	[thread-id] [version] [ERRNO]
	[    4    ] [   4   ]

CMD
---
::

	[                      OUT                        ]
	[command] [flag] [key-size] [value-size] [  key   ]
	[   1   ] [ 1  ] [   1    ] [    8     ] [key-size]

	Note: [flag] or [value-size] should be set to 0 if not used

SET
---
::

	      [   OUT    ]
	[CMD] [  value   ] [ERRNO]
	      [value-size]

GET_SET
------
::

	                   [[set] == TRUE]
	[       OUT      ] [     OUT     ]
	[set] [value-size] [    value    ] [ERRNO]
	[ 1 ] [    8     ] [ value-size  ]

GET
---
::

	                           [[errno] == E_NONE]
	      [        IN        ] [       IN        ]
	[CMD] [errno] [value-size] [      value      ]
	      [  1  ] [    8     ] [   value-size    ]

GET OR SET
----------
::

	      [[errno] == E_MISS]
	[GET] [     GET_SET     ]

DEL
---
::

	[CMD] [ERRNO]
