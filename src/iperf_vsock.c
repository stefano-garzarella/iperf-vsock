/*
 * Copyright (c) 2019 Red Hat, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * (1) Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * (2) Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/ or other materials provided with the distribution.
 *
 * (3) Neither the name of Red Hat, Inc. nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "iperf_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "iperf.h"
#include "iperf_api.h"
#include "iperf_vsock.h"


#if !defined(HAVE_VSOCK)

static int
iperf_vsock_notsupported(void)
{
	fprintf(stderr, "VSOCK not supported\n");
	i_errno = IENOVSOCK;
	return -1;
}

int
iperf_vsock_accept(struct iperf_test *test)
{
	return iperf_vsock_notsupported();
}

int
iperf_vsock_recv(struct iperf_stream *sp)
{
	return iperf_vsock_notsupported();
}

int
iperf_vsock_send(struct iperf_stream *sp)
{
	return iperf_vsock_notsupported();
}

int
iperf_vsock_listen(struct iperf_test *test)
{
	return iperf_vsock_notsupported();
}

int
iperf_vsock_connect(struct iperf_test *test)
{
	return iperf_vsock_notsupported();
}

int
iperf_vsock_init(struct iperf_test *test)
{
	return iperf_vsock_notsupported();
}

#else /* HAVE_VSOCK */

#include <sys/socket.h>
#include <sys/un.h>
#include <linux/vm_sockets.h>

#include "net.h"
#include "vsock.h"

static struct sockaddr *
vsock_sockaddr(const char *cid_str, int port, int listen, socklen_t *len)
{
	char *end = NULL;
	long cid;

	if (cid_str == NULL) {
		return NULL;
	}

	cid = strtol(cid_str, &end, 10);
	if (cid_str != end && *end == '\0') {
		struct sockaddr_vm *svm = malloc(sizeof(struct sockaddr_vm));
		if (!svm) {
			return NULL;
		}

		*len = sizeof(*svm);
		memset(svm, 0, *len);
		svm->svm_family = AF_VSOCK;
		svm->svm_cid = cid;
		svm->svm_port = port;

		return (struct sockaddr *)svm;
	}

	/*
	 * VSOCK over AF_UNIX
	 * cid_str can contain the UDS path
	 */
	struct sockaddr_un *sun = malloc(sizeof(struct sockaddr_un));
	if (!sun) {
		return NULL;
	        }

	*len = sizeof(*sun);
	memset(sun, 0, *len);
	sun->sun_family = AF_UNIX;
	strncpy(sun->sun_path, cid_str, sizeof(sun->sun_path) - 1);

	if (listen) {
		snprintf(sun->sun_path, sizeof(sun->sun_path), "%s_%d",
			 cid_str, port);
	} else {
		snprintf(sun->sun_path, sizeof(sun->sun_path), "%s", cid_str);
	}

	/* AF_UNIX path is not removed on socket close */
	(void)unlink(sun->sun_path);

	return (struct sockaddr *)sun;
}

int
vsockannounce(char *local, int port)
{
	struct sockaddr *sa;
	socklen_t sa_len;
	int listen_fd, opt;

	if (!local) {
		sa = vsock_sockaddr("-1", port, 1, &sa_len);
	} else {
		sa = vsock_sockaddr(local, port, 1, &sa_len);
	}

	if (!sa) {
		goto err;
	}

	listen_fd = socket(sa->sa_family, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		goto err;
	}

	opt = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		goto err_close;
	}

	if (bind(listen_fd, sa, sa_len) != 0) {
		goto err_close;
	}

	if (listen(listen_fd, INT_MAX) != 0) {
		goto err_close;
	}

	return listen_fd;

err_close:
	close(listen_fd);
err:
	return -1;
}

int
vsockdial(char *server, int port, int timeout)
{
	struct sockaddr *sa;
	socklen_t sa_len;
	int fd;

	sa = vsock_sockaddr(server, port, 0, &sa_len);
	if (!sa)
		return -1;

	fd = socket(sa->sa_family, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}

	if (timeout_connect(fd, sa, sa_len, timeout) != 0) {
		close(fd);
		return -1;
	}

	/*
	 * VSOCK over AF_UNIX requires a little handshake as defined here:
	 * https://github.com/firecracker-microvm/firecracker/blob/master/docs/vsock.md
	 */
	if (sa->sa_family == AF_UNIX) {
		char buf[1024];

		/* Send "CONNECT $PORT\n" */
		snprintf(buf, 1024, "CONNECT %d\n", port);

		if (Nwrite(fd, buf, strnlen(buf, 1024), Pvsock) < 0) {
			close(fd);
			return -1;
		}

		/* Receive "OK $REMOTE_PORT\n" */
		buf[0] = '\0';
		while (buf[0] != '\n') {
			if (Nread(fd, buf, 1, Pvsock) <= 0) {
				close(fd);
				return -1;
			}
		}
	}

	return fd;
}

int
iperf_vsock_accept(struct iperf_test *test)
{
	int fd;
	signed char rbuf = ACCESS_DENIED;
	char cookie[COOKIE_SIZE];
	struct sockaddr_vm sa_client;
	socklen_t socklen_client = sizeof(sa_client);

	fd = accept(test->listener, (struct sockaddr *) &sa_client,
		    &socklen_client);
	if (fd < 0) {
		i_errno = IESTREAMCONNECT;
		return -1;
	}

	if (Nread(fd, cookie, COOKIE_SIZE, Pvsock) < 0) {
		i_errno = IERECVCOOKIE;
		return -1;
	}

	if (strcmp(test->cookie, cookie) != 0) {
		if (Nwrite(fd, (char*) &rbuf, sizeof(rbuf), Pvsock) < 0) {
			i_errno = IESENDMESSAGE;
			return -1;
		}
		close(fd);
	}

	return fd;
}

int
iperf_vsock_recv(struct iperf_stream *sp)
{
	int r;

	r = Nread(sp->socket, sp->buffer, sp->settings->blksize, Pvsock);
	if (r < 0) {
		/*
		 * VSOCK can return -1 with errno = ENOTCONN if the remote host
		 * closes the connection, but in the iperf3 code we expect
		 * return 0 in this case.
		 */
		if (errno == ENOTCONN)
			return 0;
		else
			return r;
	}

	/* Only count bytes received while we're in the correct state. */
	if (sp->test->state == TEST_RUNNING) {
		sp->result->bytes_received += r;
		sp->result->bytes_received_this_interval += r;
	}
	else if (sp->test->debug) {
		printf("Late receive, state = %d\n", sp->test->state);
	}

	return r;
}

int
iperf_vsock_send(struct iperf_stream *sp)
{
	int r;

	r = Nwrite(sp->socket, sp->buffer, sp->settings->blksize, Pvsock);
	if (r < 0) {
		/*
		 * VSOCK can return -1 with errno = ENOTCONN if the remote host
		 * closes the connection, but in the iperf3 code we expect
		 * return 0 in this case.
		 */
		if (errno == ENOTCONN)
			return 0;
		else
			return r;
	}

	sp->result->bytes_sent += r;
	sp->result->bytes_sent_this_interval += r;

	return r;
}

int
iperf_vsock_listen(struct iperf_test *test)
{
	/* We use the same socket used for control path (test->listener) */
	return test->listener;
}

int
iperf_vsock_connect(struct iperf_test *test)
{
	int fd;

	fd = vsockdial(test->server_hostname, test->server_port, -1);
	if (fd < 0) {
		goto err;
	}

	/* Send cookie for verification */
	if (Nwrite(fd, test->cookie, COOKIE_SIZE, Pvsock) < 0) {
		goto err_close;
	}

	return fd;

err_close:
	close(fd);
err:
	i_errno = IESTREAMCONNECT;
	return -1;
}

int
iperf_vsock_init(struct iperf_test *test)
{
	return 0;
}

#endif /* HAVE_VSOCK */
