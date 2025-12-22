// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <stdio.h>
#include <signal.h>
#include "service.h"
#include "config.h"
#include "tls.h"

static void handle_signal()
{
	sighandler_t ret __attribute__((unused));
	ret = signal(SIGINT, _exit);
	assert(ret != SIG_ERR);
	ret = signal(SIGTERM, _exit);
	assert(ret != SIG_ERR);
}

static void must_meet_requirements()
{
	static_assert(sizeof(void *) == 8);
	must(sysconf(_SC_PAGESIZE) == (1 << PAGE_SHIFT));
}

int main(int argc, char *argv[])
{
#ifdef DEBUG
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
#endif

#ifdef CONFIG_KERNEL_TLS
	must(argc >= 5);
	must(tls_global_init(argv[2], argv[3], argv[4]));
#else
	must(argc >= 2);
#endif

	char *endptr;
	int port = strtol(argv[1], &endptr, 10);
	must(argv[1][0] != '\0' && endptr[0] == '\0');

	handle_signal();
	must_meet_requirements();
	must_service_run(port);
}
