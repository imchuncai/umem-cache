// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_ERRNO_H
#define __UMEM_CACHE_ERRNO_H

enum umem_cache_errno {
	E_NONE,

	E_CONNECT_OUTDATED,
	E_CONNECT_TOO_MANY,
	E_CONNECT_KILL,

	E_GET_MISS,
};

#endif
