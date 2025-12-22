# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

CFLAGS = -O3 -g -Wall -Wextra -flto=auto -fwhole-program -D_GNU_SOURCE

# CFLAGS += -std=gnu23
CFLAGS += -std=gnu11 -Dalignof=__alignof__ -include stdbool.h

targets  = fixed_mem_cache.c
targets += hash_table.c
targets += kv_cache.c
targets += kv.c
targets += main.c
targets += memory.c
targets += murmur_hash3.c
targets += slab.c
targets += socket.c
targets += thread.c

ifdef DEBUG
CFLAGS += -fsanitize=address -fsanitize=undefined -fno-sanitize-recover        \
		-fno-omit-frame-pointer -DDEBUG
else
CFLAGS += -DNDEBUG
endif

ifdef TLS
	ifneq ($(TLS),0)
		CFLAGS += -DCONFIG_KERNEL_TLS -lgnutls
		targets += tls.c
	endif
endif

ifdef RAFT
	ifneq ($(RAFT),0)
		CFLAGS += -DCONFIG_RAFT
		targets += cluster.c
		targets += log.c
		targets += machine.c
		targets += member.c
		targets += raft_conn.c
		targets += service_raft.c
	else
		targets += service.c
	endif
else
targets += service.c
endif

ifdef THREAD_NR
CFLAGS += -DCONFIG_THREAD_NR=$(THREAD_NR)
endif

ifdef MAX_CONN
CFLAGS += -DCONFIG_MAX_CONN=$(MAX_CONN)
endif

ifdef MEM_LIMIT
CFLAGS += -DCONFIG_MEM_LIMIT=$(MEM_LIMIT)
endif

ifdef TCP_TIMEOUT
CFLAGS += -DCONFIG_TCP_TIMEOUT=$(TCP_TIMEOUT)
endif

ifdef TEST_ELECTION_WITH_UNSTABLE_LOG
CFLAGS += -DTEST_ELECTION_WITH_UNSTABLE_LOG
endif

ifdef TEST_ELECTION_WITH_UNSTABLE_GROW_LOG
CFLAGS += -DTEST_ELECTION_WITH_UNSTABLE_GROW_LOG
endif

ifdef TEST_VOTE_WITH_LOG0
CFLAGS += -DTEST_VOTE_WITH_LOG0
endif

SILENT = $(findstring s,$(firstword -$(MAKEFLAGS)))

umem-cache: $(targets)
	gcc $(CFLAGS) -o umem-cache $^
	@if [[ "$(SILENT)" != "s" ]]; then				       \
		printf "\nExecute: ./umem-cache {{port}}";		       \
		if [[ "x$(TLS)" != "x" && "x$(TLS)" != "x0" ]]; then	       \
			printf " {{cert.pem}} {{key.pem}} {{ca-cert.pem}}";    \
		fi;							       \
		printf "\n";						       \
	fi

help:
	@echo make {{RAFT=0}} {{TLS=0}} {{THREAD_NR=4}} {{MAX_CONN=512}}       \
		{{MEM_LIMIT=104857600}} {{TCP_TIMEOUT=3000}}

check:
	@(./test.sh $(RAFT) $(TLS))

clean:
	rm -f umem-cache

enable-kernel-tls:
	modprobe tls

.PHONY: umem-cache help check clean enable-kernel-tls
