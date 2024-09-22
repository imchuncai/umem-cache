// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_ERRNO_H
#define __UMEM_CACHE_ERRNO_H

enum umem_cache_errno {
	E_NONE, E_OUTDATED, E_TOO_MANY, E_WILL_BLOCK, E_MISS, E_NOMEM, E_KILL,

	E_CONNECT_OUTDATED	= E_OUTDATED,
	E_CONNECT_TOO_MANY	= E_TOO_MANY,
	E_CONNECT_KILL		= E_KILL,

	E_GET_WILL_BLOCK	= E_WILL_BLOCK,
	E_GET_MISS		= E_MISS,
	E_GET_NOMEM		= E_NOMEM,

	E_SET_WILL_BLOCK	= E_WILL_BLOCK,
	E_SET_NOMEM		= E_NOMEM,

	E_DEL_WILL_BLOCK	= E_WILL_BLOCK,
};

#endif
