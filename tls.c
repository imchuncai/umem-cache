// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#include <assert.h>
#include <arpa/inet.h>
#include <gnutls/x509.h>
#include "tls.h"

static gnutls_certificate_credentials_t x509_cred;

#define GNUTLS_CLIENT_FLAG (GNUTLS_NO_DEFAULT_EXTENSIONS |		       \
			    GNUTLS_CLIENT | GNUTLS_NONBLOCK | GNUTLS_NO_SIGNAL)
#define GNUTLS_SERVER_FLAG (GNUTLS_SERVER | GNUTLS_NONBLOCK | GNUTLS_NO_SIGNAL)

static bool tls_session_init(struct tls_session *s, int sockfd,
				struct in6_addr peer, unsigned int flag)
{
	if (gnutls_init(&s->session, flag))
		return false;

	/* It is recommended to use the default priorities */
	if (gnutls_set_default_priority(s->session) ||
	    gnutls_credentials_set(s->session, GNUTLS_CRD_CERTIFICATE, x509_cred)) {
		gnutls_deinit(s->session);
		return false;
	}

	const char * ret __attribute__((unused));
	ret = inet_ntop(AF_INET6, &peer, s->peer_addr, INET6_ADDRSTRLEN);
	assert(ret);

	gnutls_session_set_verify_cert(s->session, s->peer_addr, 0);
	gnutls_transport_set_int(s->session, sockfd);
	gnutls_handshake_set_timeout(s->session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
	return true;
}

bool tls_init_client(struct tls_session *client, int sockfd, struct in6_addr peer)
{
	return tls_session_init(client, sockfd, peer, GNUTLS_CLIENT_FLAG);
}

bool tls_init_server(struct tls_session *server, int sockfd, struct in6_addr peer)
{
	if (!tls_session_init(server, sockfd, peer, GNUTLS_SERVER_FLAG))
		return false;

	gnutls_certificate_server_set_request(server->session, GNUTLS_CERT_REQUEST);
	return true;
}

bool tls_global_init(const char *cert_pem, const char *key_pem, const char *ca_pem)
{
	return gnutls_check_version("3.5.6") &&
		0 == gnutls_certificate_allocate_credentials(&x509_cred) &&
		0 == gnutls_certificate_set_x509_key_file(
			x509_cred, cert_pem, key_pem, GNUTLS_X509_FMT_PEM) &&
		0 == gnutls_certificate_set_known_dh_params(
			x509_cred, GNUTLS_SEC_PARAM_MEDIUM) &&
		// gnutls_certificate_set_x509_system_trust(x509_cred) > 0
		gnutls_certificate_set_x509_trust_file(
			x509_cred, ca_pem, GNUTLS_X509_FMT_PEM) > 0;
}

int tls_handshake(struct tls_session *session)
{
	int ret = gnutls_handshake(session->session);
	if (ret == GNUTLS_E_SUCCESS) {
		gnutls_transport_ktls_enable_flags_t flags __attribute__((unused));
		flags = gnutls_transport_is_ktls_enabled(session->session);
		assert(flags == GNUTLS_KTLS_DUPLEX);
	}
	return ret;
}

bool tls_record_require_write(struct tls_session *session)
{
	return gnutls_record_get_direction(session->session);
}

void tls_deinit(struct tls_session *session)
{
	gnutls_deinit(session->session);
}
