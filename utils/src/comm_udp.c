/*
 * Copyright (c) 2017, 2018 Rafael Antoniello
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file comm_udp.c
 * @author Rafael Antoniello
 */

#include "comm_udp.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/types.h>
#include <inttypes.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include "check_utils.h"
#include "log.h"
#include "stat_codes.h"
#include "comm.h"
#include "uri_parser.h"

/* **** Definitions **** */

#define UDP_COM_ADDR_IS_MULTICAST(addr)	IN_MULTICAST(htonl(addr))
#define UDP_COM_SOCKET_PROT 0
#define UDP_COM_DATAGRAM_BUF_SIZE (1024*1024*1024) // 1GB

/**
 * Module instance context structure
 */
typedef struct comm_udp_ctx_s {
	/**
	 * Generic communication interface structure.
	 * *MUST* be the first field in order to be able to cast to both
	 * comm_udp_ctx_t or comm_udp_ctx_t.
	 */
	struct comm_ctx_s comm_ctx;
	/**
	 * Socket file descriptor
	 */
	int fd;
	/**
	 * Exit flag: if set to non-zero value, module should
	 * finish/unblock transactions as fast as possible
	 */
	volatile int flag_exit;
	/**
	 * This pipe is used exclusively for the purpose of abruptly closing
	 * UDP-com module; the pipe is used to wake-up any 'select()' waiting
	 * for incoming data.
	 */
	int pipe_exit_signal[2];
} comm_udp_ctx_t;

/* **** Prototypes **** */

static comm_ctx_t* comm_udp_open(const char *url, const char *local_url,
		comm_mode_t comm_mode, log_ctx_t *log_ctx, va_list arg);
static void comm_udp_close(comm_ctx_t **ref_comm_ctx);
static int comm_udp_send(comm_ctx_t* comm_ctx, const void *buf, size_t count,
		struct timeval* timeout);
static int comm_udp_recv(comm_ctx_t *comm_ctx, void** ref_buf,
		size_t *ref_count, char **ref_from, struct timeval *timeout);
static int comm_udp_unblock(comm_ctx_t *comm_ctx);

/* **** Implementations **** */

const comm_if_t comm_if_udp=
{
	"udp",
	comm_udp_open,
	comm_udp_close,
	comm_udp_send,
	comm_udp_recv,
	comm_udp_unblock
};

/*
 * NOTE: On some linux systems stack configurations may apply; e.g.:
 * sudo sysctl -w net.core.rmem_max=8388608
 * sudo sysctl -w net.core.rmem_default=65536
 * sudo sysctl -w net.ipv4.udp_mem='4096 87380 1024000000'
 * (...)
 */
static comm_ctx_t* comm_udp_open(const char *url, const char *local_url,
		comm_mode_t comm_mode, log_ctx_t *log_ctx, va_list arg)
{
	int fd;
	uint16_t port;
	struct sockaddr_in service;
#ifdef ip_mreqn
	struct ip_mreqn mgroup;
#else
	/* In BSD/LINUX it is also possible to use ip_mreq instead of ip_mreqn */
	 struct ip_mreq mgroup;
#endif
	int multiple_apps;
	comm_udp_ctx_t *comm_udp_ctx= NULL;
	const int ttl= 16; // value associated with IP multicast traffic on socket
#ifdef UDP_COM_DATAGRAM_BUF_SIZE
	const int int_size= sizeof(int);
	int stack_buf_size= UDP_COM_DATAGRAM_BUF_SIZE;
#endif
	char *host_text= NULL, *port_text= NULL;
	int mode, end_code= STAT_EAFNOSUPPORT;
	const int so_priority= 7;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(url!= NULL && strlen(url)> 0, return NULL);
	// argument 'local_url' is allowed to be NULL in certain implementations
	// (not used in UDP implementation)
	CHECK_DO(comm_mode< COMM_MODE_MAX, return NULL);
	// argument 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	comm_udp_ctx= (comm_udp_ctx_t*)calloc(1, sizeof(comm_udp_ctx_t));
	CHECK_DO(comm_udp_ctx!= NULL, goto end);

	/* **** Initialize context structure **** */

	comm_udp_ctx->fd= -1; // set to non-valid file-descriptor value

	comm_udp_ctx->flag_exit= 0;

	CHECK_DO(pipe(comm_udp_ctx->pipe_exit_signal)== 0, goto end);

	/* **** Initialize protocol stack **** */

	/* Create a SOCKET object. Blocking mode is enabled by default. */
	fd= socket(AF_INET, SOCK_DGRAM, UDP_COM_SOCKET_PROT);
	CHECK_DO(fd>= 0, goto end);
	comm_udp_ctx->fd= fd;

	/* Set priority */
	CHECK_DO(setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &so_priority,
			sizeof(int))== 0, goto end);

	/* Configure host address and port for the socket that is being bound */
	if((host_text= uri_parser_get_uri_part(url, HOSTTEXT))== NULL) {
		end_code= STAT_EAFNOSUPPORT_HOSTNAME;
		goto end;
	}
	if((port_text= uri_parser_get_uri_part(url, PORTTEXT))== NULL) {
		end_code= STAT_EAFNOSUPPORT_PORT;
		goto end;
	}
	port= atoi(port_text);
	memset((char *) &service, 0, sizeof(service));
	service.sin_family= AF_INET;
	service.sin_port= htons(port);
	service.sin_addr.s_addr= inet_addr(host_text);

	/* Bind the socket */
	switch(comm_mode) {
	case COMM_MODE_IPUT:
		mode= SO_RCVBUF;
		/* Allow a socket to forcibly bind to a port in use by another socket
		 * (reuse)
		 */
		multiple_apps= 1;
		CHECK_DO(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &multiple_apps,
				sizeof(int))>= 0, goto end);
		multiple_apps= 1;
		CHECK_DO(setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &multiple_apps,
				sizeof(int))>= 0, goto end);
		/* Bind */
		CHECK_DO(bind(fd, (struct sockaddr*)&service, sizeof(
				struct sockaddr_in))== 0, goto end);
		break;
	case COMM_MODE_OPUT:
		mode= SO_SNDBUF;
		break;
	default:
		LOGE("Not supported argument\n");
		goto end;
	}

#ifdef UDP_COM_DATAGRAM_BUF_SIZE
	/* Redefine receiver/sender buffer size, and check if it is possible */
	CHECK_DO(setsockopt(fd, SOL_SOCKET, mode, &stack_buf_size,
			sizeof(stack_buf_size))== 0, goto end);
	stack_buf_size= 0;
	CHECK_DO(getsockopt(fd, SOL_SOCKET, mode, &stack_buf_size,
			(socklen_t*)&int_size)== 0, goto end);
	//LOGV("UDP Stack_buf_size: %d\n", stack_buf_size); //comment-me
	//CHECK_DO(stack_buf_size== UDP_COM_DATAGRAM_BUF_SIZE, goto end);
#endif

	if(UDP_COM_ADDR_IS_MULTICAST(inet_addr(host_text))) {
		/* Set time-to-live */
		setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(int));
		if(mode== SO_RCVBUF) {
			/* Join multicast group */
			memset((char*)&mgroup, 0, sizeof(mgroup));
			mgroup.imr_multiaddr.s_addr= inet_addr(host_text);
#ifdef ip_mreqn
			mgroup.imr_address.s_addr= htonl(INADDR_ANY);
#else
			mgroup.imr_interface.s_addr= htonl(INADDR_ANY);
#endif
			CHECK_DO(setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
					(char*)&mgroup, sizeof(mgroup))== 0, goto end);
		}
	} else {
		/* Set time-to-live */
		setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(int));
	}

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		comm_udp_close((comm_ctx_t**)&comm_udp_ctx);
	if(host_text!= NULL)
		free(host_text);
	if(port_text!= NULL)
		free(port_text);
	return (comm_ctx_t*)comm_udp_ctx;
}

static void comm_udp_close(comm_ctx_t **ref_comm_ctx)
{
	comm_udp_ctx_t* comm_udp_ctx;

	/* check argument */
	if(ref_comm_ctx== NULL ||
			(comm_udp_ctx= (comm_udp_ctx_t*)*ref_comm_ctx)== NULL)
		return;

	comm_udp_unblock((comm_ctx_t*)comm_udp_ctx);

	/* Release associated sockets */
	if(comm_udp_ctx->fd>= 0) {
		close(comm_udp_ctx->fd);
		comm_udp_ctx->fd= -1;
	}

	/* Close 'exit' signaling pipe */
	if(comm_udp_ctx->pipe_exit_signal[0]>= 0) {
		close(comm_udp_ctx->pipe_exit_signal[0]);
		comm_udp_ctx->pipe_exit_signal[0]= -1;
	}
	if(comm_udp_ctx->pipe_exit_signal[1]>= 0) {
		close(comm_udp_ctx->pipe_exit_signal[1]);
		comm_udp_ctx->pipe_exit_signal[1]= -1;
	}

	free(comm_udp_ctx);
	*ref_comm_ctx= NULL;
}

static int comm_udp_send(comm_ctx_t* comm_ctx, const void *buf, size_t count,
		struct timeval* timeout)
{
	fd_set fds;
	uint16_t port;
	struct sockaddr_in service;
	int errno, bytes_io= 0, select_ret= -1, end_code= STAT_ERROR;
	comm_udp_ctx_t *comm_udp_ctx= NULL; // Do not release (alias)
	char *host_text= NULL, *port_text= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments.
	 * Note: 'timeout' is allowed to be NULL (which means "wait indefinitely").
	 */
	CHECK_DO(comm_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(buf!= NULL, return STAT_ERROR);
	CHECK_DO(count> 0, return STAT_ERROR);

	LOG_CTX_SET(comm_ctx->log_ctx);

	comm_udp_ctx= (comm_udp_ctx_t*)comm_ctx;

	/* Return "end of file" if module is requested to exit */
	if(comm_udp_ctx->flag_exit!= 0) {
		end_code= STAT_EOF;
		goto end;
	}

	/* Check output operation with select */
	FD_ZERO(&fds);
	FD_SET(comm_udp_ctx->fd, &fds);
	select_ret= select(FD_SETSIZE, NULL, &fds, NULL, timeout);
	if(select_ret< 0) {
		LOGE("'select()' failed\n");
		goto end;
	} else if(select_ret== 0) {
		//LOGV("'select()' timeout\n"); //comment-me
		end_code= STAT_ETIMEDOUT;
		goto end;
	}

	/* Configure destination address and port */
	if((host_text= uri_parser_get_uri_part(comm_ctx->url, HOSTTEXT))== NULL) {
		end_code= STAT_EAFNOSUPPORT_HOSTNAME;
		goto end;
	}
	if((port_text= uri_parser_get_uri_part(comm_ctx->url, PORTTEXT))== NULL) {
		end_code= STAT_EAFNOSUPPORT_PORT;
		goto end;
	}
	port= atoi(port_text);
	memset((char *) &service, 0, sizeof(service));
	service.sin_family= AF_INET;
	service.sin_port= htons(port);
	service.sin_addr.s_addr= inet_addr(host_text);

	/* Perform output operation */
	errno= 0;
	bytes_io= sendto(comm_udp_ctx->fd, buf, count, 0,
			(struct sockaddr*)&service, sizeof(struct sockaddr_in));
	if(bytes_io< 0) {
		if(comm_udp_ctx->flag_exit== 0)
			LOGE("Error occurred, errno: %d\n", errno);
		else
			end_code= STAT_EOF;
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(host_text!= NULL)
		free(host_text);
	if(port_text!= NULL)
		free(port_text);
	return end_code;
}

static int comm_udp_recv(comm_ctx_t *comm_ctx, void** ref_buf,
		size_t *ref_count, char **ref_from, struct timeval *timeout)
{
	fd_set fds;
	struct timeval* select_tv;
	uint8_t recv_buf[UDP_COM_RECV_DGRAM_MAXSIZE];
	int errno, bytes_io= 0, select_ret= -1, end_code= STAT_ERROR;
	comm_udp_ctx_t *comm_udp_ctx= NULL; // Do not release (alias)
	struct timeval tv_zero= {0, 0};
	struct sockaddr_in src_addr= {0};
	socklen_t src_addr_len= sizeof(struct sockaddr_in);
	void *buf= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments.
	 * Notes:
	 * - Argument 'timeout' is allowed to be NULL (which means "wait
	 * indefinitely");
	 * - Argument 'ref_from' is allowed to be NULL (no source address is
	 * returned).
	 */
	CHECK_DO(comm_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_buf!= NULL, return STAT_ERROR);
	CHECK_DO(ref_count!= NULL, return STAT_ERROR);

	LOG_CTX_SET(comm_ctx->log_ctx);

	comm_udp_ctx= (comm_udp_ctx_t*)comm_ctx;

	/* Return "end of file" if module is requested to exit */
	if(comm_udp_ctx->flag_exit!= 0) {
		end_code= STAT_EOF;
		goto end;
	}

	/* Check input operation with select */
	FD_ZERO(&fds);
	FD_SET(comm_udp_ctx->fd, &fds);
	FD_SET(comm_udp_ctx->pipe_exit_signal[0], &fds); // "exit" signal
	select_tv= (comm_udp_ctx->flag_exit== 0)? timeout: &tv_zero;
	select_ret= select(FD_SETSIZE, &fds, NULL, NULL, select_tv);
	if(select_ret< 0) {
		LOGE("'select()' failed\n");
		goto end;
	} else if(select_ret== 0) {
		LOGV("'select()' timeout\n"); // comment-me
		end_code= STAT_ETIMEDOUT;
		goto end;
	} else {
		//LOGV("'select()': i/o operation is available now!\n"); // comment-me
		if(FD_ISSET(comm_udp_ctx->pipe_exit_signal[0], &fds)) {
			end_code= STAT_EOF;
			goto end;
		}
	}
	CHECK_DO(FD_ISSET(comm_udp_ctx->fd, &fds)> 0, goto end);

	/* Perform input operation */
	errno= 0;
	bytes_io= recvfrom(comm_udp_ctx->fd, (void*)recv_buf,
			UDP_COM_RECV_DGRAM_MAXSIZE, 0, (struct sockaddr*)&src_addr,
			&src_addr_len);
	if(bytes_io< 0) {
		if(comm_udp_ctx->flag_exit== 0)
			LOGE("Error occurred, errno: %d\n", errno);
		else
			end_code= STAT_EOF;
		goto end;
	} else if(bytes_io> UDP_COM_RECV_DGRAM_MAXSIZE) {
		LOGE("Bad argument: The maximum datagram size that can be received is "
				"%d bytes length.\n", (int)UDP_COM_RECV_DGRAM_MAXSIZE);
		goto end;
	}

	if(bytes_io> 0) {
		buf= malloc((size_t)bytes_io);
		CHECK_DO(buf!= NULL, goto end);
		memcpy(buf, recv_buf, (size_t)bytes_io);
		*ref_buf= buf;
		buf= NULL; // Avoid double referencing
	}
	*ref_count= (size_t)bytes_io;
	if(ref_from!= NULL)
		*ref_from= strdup(inet_ntoa(src_addr.sin_addr));

	end_code= STAT_SUCCESS;
end:
	if(buf!= NULL) {
		free(buf);
		buf= NULL;
	}
	return end_code;
}

static int comm_udp_unblock(comm_ctx_t *comm_ctx)
{
	int fd;
	comm_udp_ctx_t *comm_udp_ctx= NULL; // Do not release (alias)
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_ctx!= NULL, return STAT_ERROR);

	comm_udp_ctx= (comm_udp_ctx_t*)comm_ctx;

	/* Mark "exit state" */
	comm_udp_ctx->flag_exit= 1;

	/* Send exit signal to force I/O 'select()' to unblock before closing */
	if((fd= comm_udp_ctx->pipe_exit_signal[1])>= 0) {
		fd_set fds;
		struct timeval tv_zero= {0, 0};

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if(select(FD_SETSIZE, NULL, &fds, NULL, &tv_zero)> 0) {
			ASSERT(write(comm_udp_ctx->pipe_exit_signal[1], "exit",
					strlen("exit"))== strlen("exit"));
		} else {
			/* Sanity check; this should never happen in a non-buggy
			 * implementation
			 */
			LOGE("Could not send 'exit' signal to COMM-UDP instance\n");
		}

		/* Close write-end of signaling pipe */
		close(fd); // reader will see EOF
		comm_udp_ctx->pipe_exit_signal[1]= -1;
	}
	return STAT_SUCCESS;
}
