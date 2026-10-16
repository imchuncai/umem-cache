// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <string.h>
#include "stats.h"

void stats_init(struct stats *s)
{
	memset(s, 0, sizeof(*s));
}

void stats_connected(struct stats *s)
{
	s->connects++;
}

void stats_disconnected(struct stats *s)
{
	s->disconnects++;
}

void stats_hit(struct stats *s)
{
	s->hits++;
}

void stats_miss(struct stats *s)
{
	s->misses++;
}

void stats_del(struct stats *s)
{
	s->dels++;
}
