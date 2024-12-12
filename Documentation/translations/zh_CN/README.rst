.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

==========
UMEM-CACHE
==========
UMEM-CACHE是一个用户空间键值对缓存。

我们对UMEM-CACHE, MEMCACHED和REDIS的缓存性能和速度进行了测试比较。在绝大多数测试中，
UMEM-CACHE表现出更好的缓存命中率以及更少的内存使用量。在缓存小对象的测试中，
UMEM-CACHE比REDIS快20%左右。

Multilingual 多语言
==================

- `简体中文 <https://github.com/imchuncai/umem-cache/tree/master/Documentation/translations/zh_CN/README.rst>`_

使用要求
=======
1. 64位linux
2. linux 2.6.32或更新(未验证)
3. 页面大小要求为4K
4. 运行测试需要安装Go和100MB内存

编译
=====
编译需要安装GCC

用于生产
-------
按需求配置config.h
::

	cd umem-cache
	make
	make check
	./umem-cache

或使用 c flags
::
	cd umem-cache
	make EXTRA_CFLAGS="-DCONFIG_THREAD_NR=4 -DCONFIG_MAX_CONN=512	       \
	-DCONFIG_MEM_LIMIT=\"(100<<20>>PAGE_SHIFT)\" -DCONFIG_TCP_TIMEOUT=3000"
	make check
	./umem-cache

用于测试
-------
按需求配置config.h
::

	cd umem-cache
	make debug
	./umem-cache

测试
====
移步 `umem-cache-client-Go <https://github.com/imchuncai/umem-cache-client-Go>`_,
它包含了功能测试，性能测试，基准测试以及与MEMCACHED和REDIS的性能及速度比较。

特性
====

任意的键和值
-----------
键和值可以是任意字节数组，除了键的长度需要限制在255字节以内。

反缓存击穿
---------
反缓存击穿是内置的。

缓存击穿是指当某个热键首次进入缓存时，每个人都争相将其缓存，从而给后备数据库带来压力。反缓存击
穿试图仅允许一个连接执行缓存工作，从而缓解这种情况。

缓存指令不会失败
--------------
缓存指令永远不会失败

没有额外的线程
------------
除了主线程和用户要求的工作线程之外没有运行其他的线程。

集群
====
我没有提供内置的集群方案，但是我们设计了“版本”系统，你可以轻易地搭建一个。

搭建要求
-------
1. 一个系统S用于存储和分发服务器集群信息。

如何工作
-------

系统S
~~~~~
1. 启动所有新加入的服务器，停止所有不再需要的服务器。
2. 更新S上服务器集群信息包括一个自增的服务器版本号。
3. 通知所有的客户端，或者用新的版本号与所有服务建立一次连接。

客户端
~~~~~
当客户端发现连接断开时，不要着急重连，应首先确认S上的集群信息。

1. 从S上获取新的集群信息。
2. 关闭所有的旧连接。
3. 用新的服务器版本号重新建立连接。
4. 当与所有服务器都建立连接之后客户端可以继续发送请求。

客户端协议
=========

- 客户端应使用 tcp over ipv6 连接到服务器
- 命令代码在conn.h
- 错误代码在errno.h

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
