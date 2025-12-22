.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

==========
UMEM-CACHE
==========

UMEM-CACHE是一个用户空间键值对缓存，包含内置的反缓存击穿，tls加密通信和raft集群方案。

Multilingual 多语言
==================

- `简体中文 <https://github.com/imchuncai/umem-cache/tree/master/Documentation/translations/zh_CN/README.rst>`_

运行要求
=======

- 64位linux
- linux 3.15或更新(未验证)
- 4K页面大小
- 如果tls使能，要求linux 4.17或更新(未验证)
- 如果tls使能，要求启用ktls

编译
====

- 要求安装GCC
- 如果tls使能，要求安装gnutls 3.5.6或更新
- 如果要执行make check，要求安装Go

::

	cd umem-cache
	make RAFT=0 TLS=0 THREAD_NR=4 MAX_CONN=512 MEM_LIMIT=104857600 TCP_TIMEOUT=3000
	make check RAFT=0 TLS=0
	./umem-cache 10047

测试
====

- 功能测试： `umem-cache-client-Go <https://github.com/imchuncai/umem-cache-client-Go>`_
- 基准测试： `umem-cache-benchmark <https://github.com/imchuncai/umem-cache-benchmark>`_
- 集群基准测试：计划于2026年底进行测试

特性
====

任意的键和值
-----------

键和值可以是任意字节数组，除了在使用单例时键的长度需要限制在255字节以内，使用集群时键的长度需要限制在247字节以内。

反缓存击穿
---------

反缓存击穿是内置的。

缓存击穿是指当某个热键首次进入缓存时，每个人都争相将其缓存，从而给后备数据库带来压力。反缓存击
穿通过仅允许一个连接执行缓存工作来避免这种情况。

集群方案
-------

我们提供了内置的集群方案，它基于raft共识算法实现，提供一致性和可用性保证。

- 注意：我们与raft的不同之处请查看 `raft-paper.rst <https://github.com/imchuncai/umem-cache/tree/master/Documentation/raft-paper.rst>`_
- 注意：所有集群中的机器应该使用相同的THREAD_NR和MEM_LIMIT编译运行
- 警告：不支持ipv6链路本地地址
- 警告：不要将使用过的服务端加入到集群中
- 警告：不支持直接重启节点，应将节点先踢出再重新加入
- 警告：不支持域名解析

客户端协议
=========

- 客户端应使用基于ipv6的tcp连接到服务器

CONNECT
-------
::

	[        OUT         ] [ IN  ]
	[reserved] [thread-id] [error]
	[   4    ] [    4    ] [  1  ]

	注意：[error]总是0

客户端协议 (编译参数包含RAFT)
=========================

- 客户端应使用基于ipv6的tcp连接到服务器
- 如果协议标签包含(ADMIN)，客户端应使用管理员端口（运行端口号 + 1）
- command编码请查看 `raft_proto.h <https://github.com/imchuncai/umem-cache/tree/master/raft_proto.h>`_
- 缓存工作流请查看 `cluster_cache_flow.txt <https://github.com/imchuncai/umem-cache/tree/master/Documentation/translations/zh_CN/cluster_cache_flow.txt>`_

客户端hash
=========

我们采用客户端hash来分发键。

键在集群中与在单例中有所不同，在集群中它多一个8字节的版本号前缀。

客户端应该使用MurmurHash3_x64_128散列函数，种子设置为74,然后使用前64位散列结果当作小端序整
数来分发键到指定线程。键的版本号为该线程所在机器的版本号。如果该机器被标记为不可用，这个键应该
被分发到下一个可用的机器相同的线程上，同时键的版本号保持不变。这些机器应该被视为呈环形排列。

=MACHINE=
---------
::

	[sin6-address] [sin6-port] [reserved] [id] [stability] [version]
	[     16     ] [    2    ] [   2    ] [4 ] [    8    ] [   8   ]

	注意：[stability]的值越小表示机器越稳定
	注意：((stability & 1) == 1)表示机器是可用状态

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

	注意：[error]总是0, 客户端需要检查集群是否确实变更
	注意：n要求为2的幂次并且最小为4
	注意：不允许使用重复的机器
	注意：集群调整时，不允许发生变更的成员超过半数
	注意：集群扩容只允许扩充一倍的成员
	注意：集群缩容只允许减少一半的成员

LEADER
------
::

	[  OUT  ] [                    IN                     ]
	[command] [sin6-address] [sin6-port] [error] [reserved]
	[   1   ] [     16     ] [    2    ] [  1  ] [   1    ]

	注意：[error]0表示成功, 1表示没有领导者信息

CLUSTER
-------
::

	[  OUT  ] [                           IN                            ]
	[command] [type] [reserved] [machines-size] [version] [  machines   ]
	[   1   ] [ 1  ] [   7    ] [      8      ] [   8   ] [=MACHINE= * n]

注意：集群类型请查看 `log.h <https://github.com/imchuncai/umem-cache/tree/master/log.h>`_

CONNECT
-------
::

	[             OUT              ] [ IN  ]
	[command] [reserved] [thread-id] [error]
	[   1   ] [   3    ] [    4    ] [  1  ]

	注意：[error]总是0

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

	警告：客户端设置TCP_NODELAY可能会损害服务端性能

缓存协议
=======

连接到目标线程后，使用以下协议与线程进行交互。

- command编码请查看 `conn.h <https://github.com/imchuncai/umem-cache/tree/master/conn.h>`_

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

	注意：[error]总是0

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

	注意：[error]总是0
