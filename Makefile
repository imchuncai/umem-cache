# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

CFLAGS = *.c -O3 -g -Wall -Wextra -Wno-implicit-fallthrough -flto=auto	       \
	-fwhole-program -fshort-enums -D_GNU_SOURCE -o umem-cache

GNU23_CFLAGS = $(CFLAGS) -std=gnu23

GNU11_CFLAGS = $(CFLAGS) -std=gnu11 -Dfalse=0 -Dtrue=1 -Dalignof=__alignof__   \
		-include stdbool.h

DEBUG_CFLAGS = -fsanitize=address -fsanitize=undefined -fno-sanitize-recover   \
		-fno-omit-frame-pointer -DDEBUG

umem-cache: FORCE
	gcc $(GNU11_CFLAGS) -DNDEBUG $(EXTRA_CFLAGS)

debug:
	gcc $(GNU11_CFLAGS) $(DEBUG_CFLAGS) $(EXTRA_CFLAGS)

check:
	@(./test.sh)

FORCE:

.PHONY: FORCE debug check
