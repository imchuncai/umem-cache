// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_RWONCE_H
#define __UMEM_CACHE_RWONCE_H

#define READ_ONCE(x)		(*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val)	(*(volatile typeof(x) *)&(x) = (val))

#endif
