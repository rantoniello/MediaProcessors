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
 * @file comm.h
 * @brief Generic communication module.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_UTILS_SRC_COMM_H_
#define MEDIAPROCESSORS_UTILS_SRC_COMM_H_

#include <sys/types.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct log_ctx_s log_ctx_t;
typedef struct comm_ctx_s comm_ctx_t;

/**
 * Indicates the communication module instance mode: input or output.
 */
typedef enum comm_mode_enum {
	COMM_MODE_IPUT= 0,
	COMM_MODE_OPUT,
	COMM_MODE_MAX
} comm_mode_t;

/**
 * Communication protocol interface structure prototype.
 * Each specific communication module implementation will define a static and
 * unambiguous interface of this type.
 */
typedef struct comm_if_s {
	char *scheme;
	comm_ctx_t* (*open)(const char *url, const char *local_url,
			comm_mode_t comm_mode, log_ctx_t *log_ctx, va_list arg);
	void (*close)(comm_ctx_t **ref_comm_ctx);
	int (*send)(comm_ctx_t *comm_ctx, const void *buf, size_t count,
			struct timeval *timeout);
	int (*recv)(comm_ctx_t *comm_ctx, void** ref_buf, size_t *ref_count,
			char **ref_from, struct timeval *timeout);
	int (*unblock)(comm_ctx_t *comm_ctx);
} comm_if_t;

/**
 * Communication module instance context structure ('handler').
 */
typedef struct comm_ctx_s {
	/**
	 * Communication interface structure prototype.
	 */
	const comm_if_t *comm_if;
	/**
	 * Module instance API mutual exclusion lock.
	 */
	pthread_mutex_t api_mutex;
	/**
	 * External LOG module context structure instance.
	 */
	log_ctx_t *log_ctx;
	/**
	 * Module instance mode.
	 */
	comm_mode_t comm_mode;
	/**
	 * Local URL.
	 */
	char* local_url;
	/**
	 * Input/output URL.
	 */
	char* url;
} comm_ctx_t;

/* **** Prototypes **** */

/**
 * Open communication module.
 * This is a global function and should be called only once at the very
 * beginning and during the life of the application.
 * @param log_ctx Pointer to a externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int comm_module_open(log_ctx_t *log_ctx);

/**
 * Close communication module. This is a global function and should be called
 * only once at the end of the life of the application.
 */
void comm_module_close();

/**
 * Communication module options.
 * This function represents the API of the communication module.
 * This function is thread-safe and can be called concurrently.
 *
 * @param tag Option tag, namely, option identifier string.
 * The following options are available:
 *     -# "COMM_REGISTER_PROTO"
 *     -# "COMM_UNREGISTER_PROTO"
 *     .
 * @param ... Variable list of parameters according to selected option. Refer
 * to <b>Tags description</b> below to see the different additional parameters
 * corresponding to  each option tag.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 *
 * ### Tags description (additional variable arguments per tag)
 * <ul>
 * <li> <b>Tag "COMM_REGISTER_PROTO":</b><br>
 * Register the interface of an specific communication protocol.<br>
 * Additional variable arguments for function comm_module_opt() are:<br>
 * @param comm_if Pointer to the protocol interface structure (static
 * and unambiguous interface implementation of the type 'comm_if_t').
 * Code example:
 * @code
 * ...
 * const comm_if_t comm_if_udp= {
 *     "udp",
 *     comm_udp_open,
 *     comm_udp_close,
 *     comm_udp_send,
 *     comm_udp_recv,
 *     comm_udp_unblock
 * };
 * ...
 * ret_code= comm_module_opt("COMM_REGISTER_PROTO", &comm_if_udp);
 * @endcode
 *
 * <li> <b>Tag "COMM_UNREGISTER_PROTO":</b><br>
 * Unregister the interface of an specific communication protocol.<br>
 * Additional variable arguments for function comm_module_opt() are:<br>
 * @param scheme Pointer to a character string with the unambiguous
 * protocol scheme name (for example: "udp", "file", ...).
 * Code example:
 * @code
 * ret_code= comm_module_opt("COMM_UNREGISTER_PROTO", "udp");
 * @endcode
 * </ul>
 */
int comm_module_opt(const char *tag, ...);

comm_ctx_t* comm_open(const char *url, const char *local_url,
		comm_mode_t comm_mode, log_ctx_t *log_ctx, ...);

void comm_close(comm_ctx_t **ref_comm_ctx);

int comm_send(comm_ctx_t *comm_ctx, const void *buf, size_t count,
		struct timeval *timeout);

int comm_recv(comm_ctx_t *comm_ctx, void** ref_buf, size_t *ref_count,
		char **ref_from, struct timeval* timeout);

int comm_unblock(comm_ctx_t* comm_ctx);

#endif /* MEDIAPROCESSORS_UTILS_SRC_COMM_H_ */
