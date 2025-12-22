// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_TLS_H
#define __UMEM_CACHE_TLS_H

#ifdef CONFIG_KERNEL_TLS

#include <gnutls/gnutls.h>
#include <netinet/in.h>

struct tls_session {
	gnutls_session_t session;
	char peer_addr[INET6_ADDRSTRLEN];
};

bool tls_global_init(const char *cert_pem, const char *key_pem, const char *ca_pem);
bool tls_init_client(struct tls_session *client, int sockfd, struct in6_addr peer);
bool tls_init_server(struct tls_session *server, int sockfd, struct in6_addr peer);
int  tls_handshake(struct tls_session *session);
bool tls_record_require_write(struct tls_session *session);
void tls_deinit(struct tls_session *session);

#endif

#endif
