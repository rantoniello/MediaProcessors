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
 * @file comm.c
 * @author Rafael Antoniello
 */

#include "comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "log.h"
#include "stat_codes.h"
#include "check_utils.h"
#include "llist.h"
#include "uri_parser.h"

/* **** Definitions **** */

/**
 * Returns non-zero if 'tag' string is equal to given TAG string.
 */
#define TAG_IS(TAG) (strncmp(tag, TAG, strlen(TAG))== 0)

/**
 * Module's context structure.
 * This structure is statically defined in the program.
 */
typedef struct comm_module_ctx_s {
	/**
	 * Module's API mutual exclusion lock.
	 * This lock is used to provide a critical section for external
	 * applications to be able to operate concurrently and asynchronously on
	 * this module. API options are available through the function
	 * comm_module_opt().
	 */
	pthread_mutex_t module_api_mutex;
	/**
	 * List of supported/registered communication protocols.
	 * Each registered protocol will have a static interface (IF) entry
	 * in this linked list.
	 * @see comm_if_t
	 */
	llist_t *comm_if_llist;
} comm_module_ctx_t;

/* **** Prototypes **** */

static int register_comm_if(const comm_if_t *comm_if, log_ctx_t *log_ctx);
static int unregister_comm_if(const char *scheme, log_ctx_t *log_ctx);
static const comm_if_t* get_comm_if_by_scheme(const char *scheme,
		log_ctx_t *log_ctx);

static comm_if_t* comm_if_allocate();
static comm_if_t* comm_if_dup(const comm_if_t *comm_if_arg);
//static int comm_if_cmp(const comm_if_t* comm_if1, const comm_if_t* comm_if2);
static void comm_if_release(comm_if_t **ref_comm_if);

/* **** Implementations **** */

/**
 * Communication module static instance.
 */
static comm_module_ctx_t *comm_module_ctx= NULL;

int comm_module_open(log_ctx_t *log_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(log_ctx);

	/* Check module initialization */
	if(comm_module_ctx!= NULL) {
		LOGE("'COMM' module already initialized\n");
		return STAT_ERROR;
	}

	comm_module_ctx= (comm_module_ctx_t*)calloc(1, sizeof(comm_module_ctx_t));
	CHECK_DO(comm_module_ctx!= NULL, goto end);

	/* **** Initialize context **** */

	ret_code= pthread_mutex_init(&comm_module_ctx->module_api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	comm_module_ctx->comm_if_llist= NULL;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		comm_module_close();
	return STAT_SUCCESS;
}

void comm_module_close()
{
	LOG_CTX_INIT(NULL);

	/* Check module initialization */
	if(comm_module_ctx== NULL) {
		LOGE("'COMM' module must be initialized previously\n");
		return;
	}

	/* Module's API mutual exclusion lock */
	ASSERT(pthread_mutex_destroy(&comm_module_ctx->module_api_mutex)== 0);

	/* List of supported/registered communication protocols */
	LLIST_RELEASE(&comm_module_ctx->comm_if_llist, comm_if_release, comm_if_t);

	/* Release module's context structure */
	free(comm_module_ctx);
	comm_module_ctx= NULL;
}

int comm_module_opt(const char *tag, ...)
{
	va_list arg;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	if(comm_module_ctx== NULL) {
		LOGE("'COMM' module should be initialized previously\n");
		return STAT_ERROR;
	}
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	va_start(arg, tag);

	/* Lock module API critical section */
	ASSERT(pthread_mutex_lock(&comm_module_ctx->module_api_mutex)== 0);

	if(TAG_IS("COMM_REGISTER_PROTO")) {
		end_code= register_comm_if(va_arg(arg, comm_if_t*), LOG_CTX_GET());
	} else if (TAG_IS("COMM_UNREGISTER_PROTO")) {
		end_code= unregister_comm_if(va_arg(arg, const char*), LOG_CTX_GET());
	} else {
		LOGE("Unknown option\n");
		end_code= STAT_ENOTFOUND;
	}

	ASSERT(pthread_mutex_unlock(&comm_module_ctx->module_api_mutex)== 0);
	va_end(arg);
	return end_code;
}

comm_ctx_t* comm_open(const char *url, const char *local_url,
		comm_mode_t comm_mode, log_ctx_t *log_ctx, ...)
{
	va_list arg;
	const comm_if_t *comm_if;
	int ret_code, end_code= STAT_ERROR;
	char *uri_scheme= NULL;
	size_t uri_scheme_size= 0;
	comm_ctx_t* (*open)(const char*, const char*, comm_mode_t, log_ctx_t*,
			va_list)= NULL;
	comm_ctx_t *comm_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_module_ctx!= NULL, return NULL); // module!
	CHECK_DO(url!= NULL && strlen(url)> 0, return NULL);
	// argument 'local_url' is allowed to be NULL in certain implementations
	// (e.g. as "binding" is not needed in certain protocol-stacks)
	CHECK_DO(comm_mode< COMM_MODE_MAX, return NULL);
	// argument 'log_ctx' is allowed to be NULL

	va_start(arg, log_ctx);

	/* Set LOG module context */
	LOG_CTX_SET(log_ctx);

	/* Get protocol interface by scheme */
	uri_scheme= uri_parser_get_uri_part(url, SCHEME);
	if(uri_scheme== NULL || (uri_scheme_size= strlen(uri_scheme))<= 0) {
		end_code= STAT_ENOPROTOOPT;
		goto end;
	}
	ASSERT(pthread_mutex_lock(&comm_module_ctx->module_api_mutex)== 0);
	comm_if= get_comm_if_by_scheme(uri_scheme, LOG_CTX_GET());
	ASSERT(pthread_mutex_unlock(&comm_module_ctx->module_api_mutex)== 0);
	CHECK_DO(comm_if!= NULL, goto end);

	/* Check mandatory call-backs existence */
	CHECK_DO((open= comm_if->open)!= NULL, goto end);
	CHECK_DO(comm_if->close!= NULL, goto end);

	/* Open (allocate) the specific communication protocol instance */
	comm_ctx= open(url, local_url, comm_mode, LOG_CTX_GET(), arg);
	CHECK_DO(comm_ctx!= NULL, goto end);

	/* **** Initialize context structure **** */

	/* Communication interface structure prototype */
	comm_ctx->comm_if= comm_if;

	/* Module instance API mutual exclusion lock */
	ret_code= pthread_mutex_init(&comm_ctx->api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	/* External LOG module context structure instance */
	comm_ctx->log_ctx= LOG_CTX_GET();

	/* Module instance mode */
	comm_ctx->comm_mode= comm_mode;

	/* Local URL */
	if(local_url!= NULL) {
		comm_ctx->local_url= strdup(local_url);
		CHECK_DO(comm_ctx->local_url!= NULL, goto end);
	}

	/* Input/output URL */
	comm_ctx->url= strdup(url);
	CHECK_DO(comm_ctx->url!= NULL, goto end);

	end_code= STAT_SUCCESS;
end:
	va_end(arg);
	if(uri_scheme!= NULL) {
		free(uri_scheme);
		uri_scheme= NULL;
	}
	if(end_code!= STAT_SUCCESS)
		comm_close(&comm_ctx);
	return comm_ctx;
}

void comm_close(comm_ctx_t **ref_comm_ctx)
{
	comm_ctx_t *comm_ctx;
	const comm_if_t *comm_if;
	LOG_CTX_INIT(NULL);

	if(ref_comm_ctx== NULL || (comm_ctx= *ref_comm_ctx)== NULL)
		return;

	comm_if= comm_ctx->comm_if;
	ASSERT(comm_if!= NULL && comm_if->close!= NULL);

	ASSERT(pthread_mutex_destroy(&comm_ctx->api_mutex)== 0);

	if(comm_ctx->local_url!= NULL) {
		free(comm_ctx->local_url);
		comm_ctx->local_url= NULL;
	}

	if(comm_ctx->url!= NULL){
		free(comm_ctx->url);
		comm_ctx->url= NULL;
	}

	/* Close the specific module instance */
	if(comm_if!= NULL && comm_if->close!= NULL)
		comm_if->close(ref_comm_ctx);
}

int comm_send(comm_ctx_t *comm_ctx, const void *buf, size_t count,
		struct timeval *timeout)
{
	int ret_code;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(buf!= NULL, return STAT_ERROR);
	CHECK_DO(count> 0, return STAT_ERROR);
	// timeout NULL means indefinitely wait

	LOG_CTX_SET(comm_ctx->log_ctx);

	CHECK_DO(comm_ctx->comm_if!= NULL, return STAT_ERROR);

	if(comm_ctx->comm_mode!= COMM_MODE_OPUT || comm_ctx->comm_if->send== NULL) {
		LOGE("Communication interface does not implement 'send()' function.");
		return STAT_ERROR;
	}
	pthread_mutex_lock(&comm_ctx->api_mutex);
	ret_code= comm_ctx->comm_if->send(comm_ctx, buf, count, timeout);
	pthread_mutex_unlock(&comm_ctx->api_mutex);
	return ret_code;
}

int comm_recv(comm_ctx_t *comm_ctx, void** ref_buf, size_t *ref_count,
		char **ref_from, struct timeval* timeout)
{
	int ret_code;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_buf!= NULL, return STAT_ERROR);
	CHECK_DO(ref_count!= NULL, return STAT_ERROR);
	// argument 'ref_from' is allowed to be NULL
	// timeout NULL means indefinitely wait

	LOG_CTX_SET(comm_ctx->log_ctx);

	*ref_buf= NULL;
	*ref_count= 0;
	if(ref_from!= NULL)
		*ref_from= NULL;

	CHECK_DO(comm_ctx->comm_if!= NULL, return STAT_ERROR);

	if(comm_ctx->comm_mode!= COMM_MODE_IPUT || comm_ctx->comm_if->recv== NULL) {
		LOGE("Communication interface does not implement 'recv()' function.");
		return STAT_ERROR;
	}
	pthread_mutex_lock(&comm_ctx->api_mutex);
	ret_code= comm_ctx->comm_if->recv(comm_ctx, ref_buf, ref_count, ref_from,
			timeout);
	pthread_mutex_unlock(&comm_ctx->api_mutex);
	return ret_code;
}

int comm_unblock(comm_ctx_t* comm_ctx)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(comm_ctx->log_ctx);

	CHECK_DO(comm_ctx->comm_if!= NULL, return STAT_ERROR);

	if(comm_ctx->comm_if->unblock) {
		/* NEVER do the following (as we are unblocking and another thread may
		 * have the MUTEX):
		 */
		//pthread_mutex_lock(&comm_ctx->api_mutex);
		comm_ctx->comm_if->unblock(comm_ctx);
		//pthread_mutex_unlock(&comm_ctx->api_mutex);
	}
	return STAT_SUCCESS;
}

static int register_comm_if(const comm_if_t *comm_if, log_ctx_t *log_ctx)
{
	llist_t *n;
	int ret_code, end_code= STAT_ERROR;
	comm_if_t *comm_if_cpy= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(comm_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(comm_if!= NULL, return STAT_ERROR);

	/*  Check that module API critical section is locked */
	ret_code= pthread_mutex_trylock(&comm_module_ctx->module_api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	/* Check if protocol is already register with given "scheme" */
	for(n= comm_module_ctx->comm_if_llist; n!= NULL; n= n->next) {
		comm_if_t *comm_if_nth= (comm_if_t*)n->data;
		CHECK_DO(comm_if_nth!= NULL, continue);
		if(strcmp(comm_if_nth->scheme, comm_if->scheme)== 0) {
			end_code= STAT_ECONFLICT;
			goto end;
		}
	}

	/* Allocate a copy of the protocol interface in the list */
	comm_if_cpy= comm_if_dup(comm_if);
	//LOGV("Registering communication protocol with scheme: '%s'\n",
	//		comm_if_cpy->scheme); //comment-me
	CHECK_DO(comm_if_cpy!= NULL, goto end);
	ret_code= llist_push(&comm_module_ctx->comm_if_llist, comm_if_cpy);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		comm_if_release(&comm_if_cpy);
	return end_code;
}

static int unregister_comm_if(const char *scheme, log_ctx_t *log_ctx)
{
	llist_t **ref_n;
	int ret_code;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(comm_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(scheme!= NULL, return STAT_ERROR);

	/*  Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&comm_module_ctx->module_api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	for(ref_n= &comm_module_ctx->comm_if_llist; (*ref_n)!= NULL;
			ref_n= &((*ref_n)->next)) {
		comm_if_t *comm_if_nth= (comm_if_t*)(*ref_n)->data;
		CHECK_DO(comm_if_nth!= NULL, continue);

		if(strcmp(comm_if_nth->scheme, scheme)== 0) { // Node found
			void *node;

			node= llist_pop(ref_n);
			ASSERT(node!= NULL && node== (void*)comm_if_nth);

			/* Once that node register was popped (and thus not accessible
			 * by any concurrent thread), release corresponding context
			 * structure.
			 */
			//LOGD("Unregistering communication protocol '%s' succeed\n",
			//		comm_if_nth->scheme); // comment-me
			comm_if_release(&comm_if_nth);
			ASSERT(comm_if_nth== NULL);
			return STAT_SUCCESS;
		}
	}
	return STAT_ENOTFOUND;
}

static const comm_if_t* get_comm_if_by_scheme(const char *scheme,
		log_ctx_t *log_ctx)
{
	llist_t *n;
	int ret_code;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(comm_module_ctx!= NULL, return NULL);
	CHECK_DO(scheme!= NULL && strlen(scheme)> 0, return NULL);

	/*  Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&comm_module_ctx->module_api_mutex);
	CHECK_DO(ret_code== EBUSY, return NULL);

	/* Check if protocol is already register with given "scheme" */
	for(n= comm_module_ctx->comm_if_llist; n!= NULL; n= n->next) {
		comm_if_t *comm_if_nth= (comm_if_t*)n->data;
		CHECK_DO(comm_if_nth!= NULL, continue);
		if(strcmp(comm_if_nth->scheme, scheme)== 0)
			return comm_if_nth;
	}
	return NULL;
}

static comm_if_t* comm_if_allocate()
{
	return (comm_if_t*)calloc(1, sizeof(comm_if_t));
}

static comm_if_t* comm_if_dup(const comm_if_t *comm_if_arg)
{
	int end_code= STAT_ERROR;
	comm_if_t *comm_if= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_if_arg!= NULL, return NULL);

	/* Allocate context structure */
	comm_if= comm_if_allocate();
	CHECK_DO(comm_if!= NULL, goto end);

	/* Copy simple-type members values.
	 * Note that pointers to callback are all supposed to be static values,
	 * for this reason we just copy (not duplicate) the pointer values.
	 */
	memcpy(comm_if, comm_if_arg, sizeof(comm_if_t));

	/* **** Duplicate members that use dynamic memory allocations **** */

	CHECK_DO(comm_if_arg->scheme!= NULL, goto end);
	comm_if->scheme= strdup(comm_if_arg->scheme);
	CHECK_DO(comm_if->scheme!= NULL, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		comm_if_release(&comm_if);
	return comm_if;
}

#if 0 // Not used
static int comm_if_cmp(const comm_if_t* comm_if1, const comm_if_t* comm_if2)
{
	int ret_value= 1; // means "non-equal"
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(comm_if1!= NULL, return 1);
	CHECK_DO(comm_if2!= NULL, return 1);

	/* Compare contexts fields */
	if(strcmp(comm_if1->scheme, comm_if2->scheme)!= 0)
		goto end;
	if(comm_if1->open!= comm_if2->open)
		goto end;
	if(comm_if1->close!= comm_if2->close)
		goto end;
	if(comm_if1->send!= comm_if2->send)
		goto end;
	if(comm_if1->recv!= comm_if2->recv)
		goto end;
	if(comm_if1->unblock!= comm_if2->unblock)
		goto end;

	// Reserved for future use: compare new fields here...

	ret_value= 0; // contexts are equal
end:
	return ret_value;
}
#endif

static void comm_if_release(comm_if_t **ref_comm_if)
{
	comm_if_t *comm_if;

	if(ref_comm_if== NULL || (comm_if= *ref_comm_if)== NULL)
		return;

	if(comm_if->scheme!= NULL) {
		free((void*)comm_if->scheme);
		comm_if->scheme= NULL;
	}

	free(comm_if);
	*ref_comm_if= NULL;
}
