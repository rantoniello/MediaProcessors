/*
 * Copyright (c) 2017 Rafael Antoniello
 *
 * This file is part of MediaProcessors.
 *
 * MediaProcessors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MediaProcessors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with MediaProcessors. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file procs.c
 * @author Rafael Antoniello
 */

#include "procs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/fair_lock.h>
#include <libmediaprocsutils/llist.h>

#include "proc.h"
#include "proc_if.h"

/* **** Definitions **** */

//#define ENABLE_DEBUG_LOGS
#ifdef ENABLE_DEBUG_LOGS
	#define LOGD_CTX_INIT(CTX) LOG_CTX_INIT(CTX)
	#define LOGD(FORMAT, ...) LOGV(FORMAT, ##__VA_ARGS__)
#else
	#define LOGD_CTX_INIT(CTX)
	#define LOGD(...)
#endif

/**
 * Returns non-zero if given 'tag' string contains 'needle' sub-string.
 */
#define TAG_HAS(NEEDLE) (strstr(tag, NEEDLE)!= NULL)

/**
 * Returns non-zero if 'tag' string is equal to given TAG string.
 */
#define TAG_IS(TAG) (strncmp(tag, TAG, strlen(TAG))== 0)

/**
 * Limit value for the maximum number of processors that can be instantiated
 * in the system.
 * This value may be just modified, in compilation, if needed.
 */
#define PROCS_MAX_NUM_PROC_INSTANCES 16

/*
 * Input/output processor's FIFOs size.
 * //TODO: This value should be passed as a module setting in the future.
 */
#define PROCS_FIFO_SIZE 2

/**
 * PROCS module URL base-path.
 */
#define PROCS_URL_BASE_PATH 	"/procs/"

/**
 * Module's context structure.
 * PROCS module context structure is statically defined in the program.
 */
typedef struct procs_module_ctx_s {
	/**
	 * Module's API mutual exclusion lock.
	 * This lock is used to provide a critical section for external
	 * applications to be able to operate concurrently and asynchronously on
	 * this module. API options are available through the function
	 * procs_module_opt().
	 */
	pthread_mutex_t module_api_mutex;
	/**
	 * List of supported/registered processor types.
	 * Each registered processor will have a static interface (IF) entry
	 * in this linked list.
	 * @see proc_if_t
	 */
	llist_t *proc_if_llist;
} procs_module_ctx_t;

/**
 * Processors registration structure.
 */
typedef struct procs_reg_elem_s {
	/**
	 * Processor's API mutual exclusion lock.
	 * This lock is used to provide a critical section for external
	 * applications to be able to use the processor's API concurrently and
	 * asynchronously on any of the registered processor instances.
	 * Processor's API options are available through the function proc_opt()
	 * (see .proc.h).
	 * This lock is initialized when opening this module, and is kept
	 * through the whole life of the module.
	 */
	pthread_mutex_t api_mutex;
	/**
	 * A pair of locks used to provide a critical section to execute, in mutual
	 * exclusion, the processor's instantiating operations
	 * (register/unregister) and the input/output operations (send/receive).
	 * These locks are initialized when opening this module, and are kept
	 * through the whole life of the module.
	 */
	fair_lock_t *fair_lock_io_array[PROC_IO_NUM];
	/**
	 * Processor context structure.
	 * Each instantiated processor will be registered using this pointer, in a
	 * registration array. This pointer may be NULL in the register, meaning
	 * that an empty slot is available for registering new processors.
	 * To access a registered processor, this pointer will be fetched from the
	 * registration array.
	 * @see procs_module_ctx_t
	 * @see procs_reg_elem_array
	 */
	proc_ctx_t *proc_ctx;
} procs_reg_elem_t;

/**
 * Module's instance context structure.
 * PROCS module context structure is statically defined in the program.
 */
typedef struct procs_ctx_s {
	/**
	 * Module's instance API mutual exclusion lock.
	 * This lock is used to provide a critical section for external
	 * applications to be able to operate concurrently and asynchronously on
	 * this module. API options are available through the function procs_opt().
	 * @see procs_opt
	 */
	pthread_mutex_t api_mutex;
	/**
	 * Array listing the registered processor instances.
	 * The idea behind using an array is to have a mean to fast fetch
	 * a processor for input (receive) or output (send) operations.
	 * This array is defined with a fixed maximum size of
	 * PROCS_MAX_NUM_PROC_INSTANCES; each element of the array has a set of
	 * locks already initialized to enable concurrency.
	 */
	procs_reg_elem_t procs_reg_elem_array[PROCS_MAX_NUM_PROC_INSTANCES];
	/**
	 * Externally defined LOG module instance context structure.
	 */
	log_ctx_t *log_ctx;
} procs_ctx_t;

/* **** Prototypes **** */

static int register_proc_if(const proc_if_t *proc_if, log_ctx_t *log_ctx);
static int unregister_proc_if(const char *proc_name, log_ctx_t *log_ctx);
static const proc_if_t* get_proc_if_by_name(const char *proc_name,
		log_ctx_t *log_ctx);

static int procs_instance_opt(procs_ctx_t *procs_ctx, const char *tag,
		log_ctx_t *log_ctx, va_list arg);

static int procs_rest_get(procs_ctx_t *procs_ctx, log_ctx_t *log_ctx,
		char **ref_rest_str);
static int proc_register(procs_ctx_t *procs_ctx, const char *proc_name,
		const char *settings_str, log_ctx_t *log_ctx, int *ref_id, va_list arg);
static int proc_unregister(procs_ctx_t *procs_ctx, int id, log_ctx_t *log_ctx);

static int procs_id_opt(procs_ctx_t *procs_ctx, const char *tag,
		log_ctx_t *log_ctx, va_list arg);

/* **** Implementations **** */

/**
 * PROCS module static instance.
 */
static procs_module_ctx_t *procs_module_ctx= NULL;

int procs_module_open(log_ctx_t *log_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(log_ctx);

	/* Check module initialization */
	if(procs_module_ctx!= NULL) {
		LOGE("'PROCS' module already initialized\n");
		return STAT_ERROR;
	}

	procs_module_ctx= (procs_module_ctx_t*)calloc(1, sizeof(
			procs_module_ctx_t));
	CHECK_DO(procs_module_ctx!= NULL, goto end);

	/* **** Initialize context **** */

	ret_code= pthread_mutex_init(&procs_module_ctx->module_api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	procs_module_ctx->proc_if_llist= NULL;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		procs_module_close();
	return STAT_SUCCESS;
}

void procs_module_close()
{
	LOG_CTX_INIT(NULL);

	/* Check module initialization */
	if(procs_module_ctx== NULL) {
		LOGE("'PROCS' module must be initialized previously\n");
		return;
	}

	/* Module's API mutual exclusion lock */
	ASSERT(pthread_mutex_destroy(&procs_module_ctx->module_api_mutex)== 0);

	/* List of supported/registered processor types */
	LLIST_RELEASE(&procs_module_ctx->proc_if_llist, proc_if_release, proc_if_t);

	/* Release module's context structure */
	free(procs_module_ctx);
	procs_module_ctx= NULL;
}

int procs_module_opt(const char *tag, ...)
{
	va_list arg;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	if(procs_module_ctx== NULL) {
		LOGE("'PROCS' module should be initialized previously\n");
		return STAT_ERROR;
	}
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	va_start(arg, tag);

	/* Lock module API critical section */
	ASSERT(pthread_mutex_lock(&procs_module_ctx->module_api_mutex)== 0);

	if(TAG_IS("PROCS_REGISTER_TYPE")) {
		end_code= register_proc_if(va_arg(arg, proc_if_t*), LOG_CTX_GET());
	} else if (TAG_IS("PROCS_UNREGISTER_TYPE")) {
		end_code= unregister_proc_if(va_arg(arg, const char*), LOG_CTX_GET());
	} else if (TAG_IS("PROCS_GET_TYPE")) {
		const proc_if_t *proc_if_register= NULL; // Do not release
		const char *proc_name= va_arg(arg, const char*);
		proc_if_t **ref_proc_if_cpy= va_arg(arg, proc_if_t**);
		proc_if_register= get_proc_if_by_name(proc_name, LOG_CTX_GET());
		if(proc_if_register!= NULL)
			*ref_proc_if_cpy= proc_if_dup(proc_if_register);
		else
			*ref_proc_if_cpy= NULL;
		end_code= (*ref_proc_if_cpy!= NULL)? STAT_SUCCESS: STAT_ENOTFOUND;
	} else {
		LOGE("Unknown option\n");
		end_code= STAT_ENOTFOUND;
	}

	ASSERT(pthread_mutex_unlock(&procs_module_ctx->module_api_mutex)== 0);
	va_end(arg);
	return end_code;
}

procs_ctx_t* procs_open(log_ctx_t *log_ctx)
{
	int proc_id, i, ret_code, end_code= STAT_ERROR;
	procs_ctx_t *procs_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check module initialization */
	if(procs_module_ctx== NULL) {
		LOGE("'PROCS' module should be initialized previously\n");
		return NULL;
	}

	procs_ctx= (procs_ctx_t*)calloc(1, sizeof(procs_ctx_t));
	CHECK_DO(procs_ctx!= NULL, goto end);

	/* **** Initialize context **** */

	ret_code= pthread_mutex_init(&procs_ctx->api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	for(proc_id= 0; proc_id< PROCS_MAX_NUM_PROC_INSTANCES; proc_id++) {
		procs_reg_elem_t *procs_reg_elem=
				&procs_ctx->procs_reg_elem_array[proc_id];

		ret_code= pthread_mutex_init(&procs_reg_elem->api_mutex, NULL);
		CHECK_DO(ret_code== 0, goto end);

		for(i= 0; i< PROC_IO_NUM; i++) {
			fair_lock_t *fair_lock= fair_lock_open();
			CHECK_DO(fair_lock!= NULL, goto end);
			procs_reg_elem->fair_lock_io_array[i]= fair_lock;
		}

		procs_reg_elem->proc_ctx= NULL;
	}

	procs_ctx->log_ctx= log_ctx;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		procs_close(&procs_ctx);
	return procs_ctx;
}

void procs_close(procs_ctx_t **ref_procs_ctx)
{
	procs_ctx_t *procs_ctx= NULL;
	LOG_CTX_INIT(NULL);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	if(ref_procs_ctx== NULL)
		return;

	if((procs_ctx= *ref_procs_ctx)!= NULL) {
		int proc_id, i;
		LOG_CTX_SET(procs_ctx->log_ctx);

		/* First of all release all the processors (note that for deleting
		 * the processors we need the processor IF type to be still available).
		 */
		ASSERT(pthread_mutex_lock(&procs_ctx->api_mutex)== 0);
		for(proc_id= 0; proc_id< PROCS_MAX_NUM_PROC_INSTANCES; proc_id++) {
			LOGD("unregistering proc with Id.: %d\n", proc_id); //comment-me
			proc_unregister(procs_ctx, proc_id, LOG_CTX_GET());
		}
		ASSERT(pthread_mutex_unlock(&procs_ctx->api_mutex)== 0);

		/* Module's instance API mutual exclusion lock */
		ASSERT(pthread_mutex_destroy(&procs_ctx->api_mutex)== 0);

		/* Array listing the registered processor instances */
		for(proc_id= 0; proc_id< PROCS_MAX_NUM_PROC_INSTANCES; proc_id++) {
			procs_reg_elem_t *procs_reg_elem=
					&procs_ctx->procs_reg_elem_array[proc_id];

			ASSERT(pthread_mutex_destroy(&procs_reg_elem->api_mutex)== 0);

			for(i= 0; i< PROC_IO_NUM; i++)
				fair_lock_close(&procs_reg_elem->fair_lock_io_array[i]);
		}

		/* Release module's instance context structure */
		free(procs_ctx);
		*ref_procs_ctx= NULL;
	}
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

int procs_opt(procs_ctx_t *procs_ctx, const char *tag, ...)
{
	va_list arg;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	/* Check module initialization */
	if(procs_module_ctx== NULL) {
		LOGE("'PROCS' module should be initialized previously\n");
		return STAT_ERROR;
	}

	va_start(arg, tag);

	LOG_CTX_SET(procs_ctx->log_ctx);

	if(TAG_HAS("PROCS_ID") && !TAG_IS("PROCS_ID_DELETE")) {
		end_code= procs_id_opt(procs_ctx, tag, LOG_CTX_GET(), arg);
	} else {
		end_code= procs_instance_opt(procs_ctx, tag, LOG_CTX_GET(), arg);
	}

	va_end(arg);
	return end_code;
}

int procs_send_frame(procs_ctx_t *procs_ctx, int proc_id,
		const proc_frame_ctx_t *proc_frame_ctx)
{
	int end_code= STAT_ERROR;
	procs_reg_elem_t *procs_reg_elem;
	fair_lock_t *p_fair_lock= NULL;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_id>= 0 && proc_id< PROCS_MAX_NUM_PROC_INSTANCES,
			return STAT_ERROR);
	CHECK_DO(proc_frame_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(procs_ctx->log_ctx);

	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];

	p_fair_lock= procs_reg_elem->fair_lock_io_array[PROC_IPUT];
	CHECK_DO(p_fair_lock!= NULL, goto end);

	fair_lock(p_fair_lock);

	/* Write frame to processor input FIFO if applicable.
	 * Note that we are operating in the i/o critical section; thus this
	 * operation is thread safe and can be executed concurrently with any
	 * other i/o or API operation.
	 */
	if((proc_ctx= procs_reg_elem->proc_ctx)== NULL) {
		end_code= STAT_ENOTFOUND;
		goto end;
	}
	end_code= proc_send_frame(proc_ctx, proc_frame_ctx);
end:
	fair_unlock(p_fair_lock);
	return end_code;
}

int procs_recv_frame(procs_ctx_t *procs_ctx, int proc_id,
		proc_frame_ctx_t **ref_proc_frame_ctx)
{
	int end_code= STAT_ERROR;
	procs_reg_elem_t *procs_reg_elem;
	fair_lock_t *p_fair_lock= NULL;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_id>= 0 && proc_id< PROCS_MAX_NUM_PROC_INSTANCES,
			return STAT_ERROR);
	CHECK_DO(ref_proc_frame_ctx!= NULL, return STAT_ERROR);

	*ref_proc_frame_ctx= NULL;

	LOG_CTX_SET(procs_ctx->log_ctx);

	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];

	p_fair_lock= procs_reg_elem->fair_lock_io_array[PROC_OPUT];
	CHECK_DO(p_fair_lock!= NULL, goto end);

	fair_lock(p_fair_lock);

	/* Read frame from processor output FIFO if applicable.
	 * Note that we are operating in the i/o critical section; thus this
	 * operation is thread safe and can be executed concurrently with any
	 * other i/o or API operation.
	 */
	if((proc_ctx= procs_reg_elem->proc_ctx)== NULL) {
		end_code= STAT_ENOTFOUND;
		goto end;
	}
	end_code= proc_recv_frame(proc_ctx, ref_proc_frame_ctx);
end:
	fair_unlock(p_fair_lock);
	return end_code;
}

static int register_proc_if(const proc_if_t *proc_if, log_ctx_t *log_ctx)
{
	llist_t *n;
	int ret_code, end_code= STAT_ERROR;
	proc_if_t *proc_if_cpy= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_if!= NULL, return STAT_ERROR);

	/*  Check that module API critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_module_ctx->module_api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	/* Check if processor is already register with given "name" */
	for(n= procs_module_ctx->proc_if_llist; n!= NULL; n= n->next) {
		proc_if_t *proc_if_nth= (proc_if_t*)n->data;
		CHECK_DO(proc_if_nth!= NULL, continue);
		if(strcmp(proc_if_nth->proc_name, proc_if->proc_name)== 0) {
			end_code= STAT_ECONFLICT;
			goto end;
		}
	}

	/* Allocate a copy of the processor type in the list */
	proc_if_cpy= proc_if_dup(proc_if);
	CHECK_DO(proc_if_cpy!= NULL, goto end);
	ret_code= llist_push(&procs_module_ctx->proc_if_llist, proc_if_cpy);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		proc_if_release(&proc_if_cpy);
	return end_code;
}

static int unregister_proc_if(const char *proc_name, log_ctx_t *log_ctx)
{
	llist_t **ref_n;
	int ret_code;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_name!= NULL, return STAT_ERROR);

	/*  Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_module_ctx->module_api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	for(ref_n= &procs_module_ctx->proc_if_llist; (*ref_n)!= NULL;
			ref_n= &((*ref_n)->next)) {
		proc_if_t *proc_if_nth= (proc_if_t*)(*ref_n)->data;
		CHECK_DO(proc_if_nth!= NULL, continue);

		if(strcmp(proc_if_nth->proc_name, proc_name)== 0) { // Node found
			void *node;

			node= llist_pop(ref_n);
			ASSERT(node!= NULL && node== (void*)proc_if_nth);

			/* Once that node register was popped (and thus not accessible
			 * by any concurrent thread), release corresponding context
			 * structure.
			 */
			//LOGD("Unregistering processor '%s' succeed\n",
			//		proc_if_nth->proc_name); // comment-me
			proc_if_release(&proc_if_nth);
			ASSERT(proc_if_nth== NULL);
			return STAT_SUCCESS;
		}
	}
	return STAT_ENOTFOUND;
}

static const proc_if_t* get_proc_if_by_name(const char *proc_name,
		log_ctx_t *log_ctx)
{
	llist_t *n;
	int ret_code;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return NULL);
	CHECK_DO(proc_name!= NULL && strlen(proc_name)> 0, return NULL);

	/*  Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_module_ctx->module_api_mutex);
	CHECK_DO(ret_code== EBUSY, return NULL);

	/* Check if processor is already register with given "name" */
	for(n= procs_module_ctx->proc_if_llist; n!= NULL; n= n->next) {
		proc_if_t *proc_if_nth= (proc_if_t*)n->data;
		CHECK_DO(proc_if_nth!= NULL, continue);
		if(strncmp(proc_if_nth->proc_name, proc_name, strlen(proc_name))== 0)
			return proc_if_nth;
	}
	return NULL;
}

static int procs_instance_opt(procs_ctx_t *procs_ctx, const char *tag,
		log_ctx_t *log_ctx, va_list arg)
{
#define PROC_ID_STR_FMT "{\"proc_id\":%d}"
	int end_code= STAT_ERROR;
	char ref_id_str[strlen(PROC_ID_STR_FMT)+ 64];
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	/* Lock module instance API critical section */
	ASSERT(pthread_mutex_lock(&procs_ctx->api_mutex)== 0);

	if(TAG_IS("PROCS_POST")) {
		int id;
		const char *proc_name= va_arg(arg, const char*);
		const char *settings_str= va_arg(arg, const char*);
		char **ref_rest_str= va_arg(arg, char**);
		end_code= proc_register(procs_ctx, proc_name, settings_str,
				LOG_CTX_GET(), &id, arg);
		if(end_code== STAT_SUCCESS && id>= 0) {
			snprintf(ref_id_str, sizeof(ref_id_str), PROC_ID_STR_FMT, id);
			*ref_rest_str= strdup(ref_id_str);
		} else {
			*ref_rest_str= NULL;
		}
	} else if(TAG_IS("PROCS_GET")) {
		end_code= procs_rest_get(procs_ctx, LOG_CTX_GET(), va_arg(arg, char**));
	} else if(TAG_IS("PROCS_ID_DELETE")) {
		register int id= va_arg(arg, int);
		end_code= proc_unregister(procs_ctx, id, LOG_CTX_GET());
	} else {
		LOGE("Unknown option\n");
		end_code= STAT_ENOTFOUND;
	}

	ASSERT(pthread_mutex_unlock(&procs_ctx->api_mutex)== 0);
	return end_code;
#undef PROC_ID_STR_FMT
}

static int procs_rest_get(procs_ctx_t *procs_ctx, log_ctx_t *log_ctx,
		char **ref_rest_str)
{
	int i, ret_code, end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL;
	cJSON *cjson_aux= NULL, *cjson_procs; // Do not release
	char href[128]= {0};
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_rest_str!= NULL, return STAT_ERROR);

	/*  Check that module instance API critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	*ref_rest_str= NULL;

	/* JSON structure is as follows:
	 * {
	 *     "procs":
	 *     [
	 *         {
	 *             "proc_id":number,
	 *             "type_name":string,
	 *             "links":
	 *             [
	 *                 {"rel":"self", "href":string}
	 *             ]
	 *         },
	 *         ....
	 *     ]
	 * }
	 */

	/* Create cJSON tree-root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* Create and attach 'PROCS' array */
	cjson_procs= cJSON_CreateArray();
	CHECK_DO(cjson_procs!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "procs", cjson_procs);

	for(i= 0; i< PROCS_MAX_NUM_PROC_INSTANCES; i++) {
		const char *proc_type_name;
		const proc_if_t *proc_if;
		register int proc_instance_index;
		cJSON *cjson_proc, *cjson_links, *cjson_link;
		proc_ctx_t *proc_ctx= NULL;
		procs_reg_elem_t *procs_reg_elem=
				&procs_ctx->procs_reg_elem_array[i];

		if((proc_ctx= procs_reg_elem->proc_ctx)== NULL)
			continue;

		proc_instance_index= proc_ctx->proc_instance_index;
		CHECK_DO(proc_instance_index== i, continue);

		cjson_proc= cJSON_CreateObject();
		CHECK_DO(cjson_proc!= NULL, goto end);
		cJSON_AddItemToArray(cjson_procs, cjson_proc);

		/* 'proc_id' */
		cjson_aux= cJSON_CreateNumber((double)proc_instance_index);
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_proc, "proc_id", cjson_aux);

		/* 'type_name' */
		proc_if= proc_ctx->proc_if;
		CHECK_DO(proc_if!= NULL, continue);
		proc_type_name= proc_if->proc_name;
		CHECK_DO(proc_type_name!= NULL, continue);
		cjson_aux= cJSON_CreateString(proc_type_name);
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_proc, "type_name", cjson_aux);

		/* 'links' */
		cjson_links= cJSON_CreateArray();
		CHECK_DO(cjson_links!= NULL, goto end);
		cJSON_AddItemToObject(cjson_proc, "links", cjson_links);

		cjson_link= cJSON_CreateObject();
		CHECK_DO(cjson_link!= NULL, goto end);
		cJSON_AddItemToArray(cjson_links, cjson_link);

		cjson_aux= cJSON_CreateString("self");
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_link, "rel", cjson_aux);

		snprintf(href, sizeof(href), PROCS_URL_BASE_PATH"%d.json",
				proc_instance_index);
		cjson_aux= cJSON_CreateString(href);
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_link, "href", cjson_aux);
	}

	/* Print cJSON structure data to char string */
	*ref_rest_str= cJSON_PrintUnformatted(cjson_rest);
	CHECK_DO(*ref_rest_str!= NULL && strlen(*ref_rest_str)> 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

static int proc_register(procs_ctx_t *procs_ctx, const char *proc_name,
		const char *settings_str, log_ctx_t *log_ctx, int *ref_id, va_list arg)
{
	procs_reg_elem_t *procs_reg_elem;
	const proc_if_t *proc_if;
	int proc_id, ret_code, end_code= STAT_ERROR;
	proc_ctx_t *proc_ctx= NULL;
	uint32_t fifo_ctx_maxsize[PROC_IO_NUM]= {PROCS_FIFO_SIZE, PROCS_FIFO_SIZE};
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_name!= NULL, return STAT_ERROR);
	CHECK_DO(settings_str!= NULL, return STAT_ERROR);
	// Note: argument 'log_ctx' is allowed to be NULL
	CHECK_DO(ref_id!= NULL, return STAT_ERROR);

	*ref_id= -1; // Set to invalid (undefined) value

	/* Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, goto end);

	/* Get free slot where to register new processor.
	 * Note that working on a locked module instance API critical section
	 * guarantee that 'procs_reg_elem_array' is not accessed concurrently for
	 * registering/unregistering.
	 * Nevertheless, 'procs_reg_elem_array' is still accessible concurrently
	 * by module's i/o operations, until corresponding "fair-lock" is locked.
	 */
	for(proc_id= 0; proc_id< PROCS_MAX_NUM_PROC_INSTANCES; proc_id++) {
		if(procs_ctx->procs_reg_elem_array[proc_id].proc_ctx== NULL)
			break;
	}
	if(proc_id>= PROCS_MAX_NUM_PROC_INSTANCES) {
		LOGE("Maximum number of allowed processor instances exceeded\n");
		end_code= STAT_ENOMEM;
		goto end;
	}

	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];

	/* Get processor interface (lock module!) */
	ASSERT(pthread_mutex_lock(&procs_module_ctx->module_api_mutex)== 0);
	proc_if= get_proc_if_by_name(proc_name, LOG_CTX_GET());
	ASSERT(pthread_mutex_unlock(&procs_module_ctx->module_api_mutex)== 0);
	if(proc_if== NULL) {
		end_code= STAT_ENOTFOUND;
		goto end;
	}

	/* Open processor */
	proc_ctx= proc_open(proc_if, settings_str, proc_id, fifo_ctx_maxsize,
			LOG_CTX_GET(), arg);
	CHECK_DO(proc_ctx!= NULL, goto end);
	*ref_id= proc_id;

	/* Register processor context structure.
	 * At this point we lock corresponding i/o critical section ("fair" lock),
	 * as this register slot is still accessible concurrently by processor
	 * API and i/o operations.
	 */
	ASSERT(pthread_mutex_lock(&procs_reg_elem->api_mutex)== 0);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	procs_reg_elem->proc_ctx= proc_ctx;
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	ASSERT(pthread_mutex_unlock(&procs_reg_elem->api_mutex)== 0);
	proc_ctx= NULL; // Avoid double referencing

	end_code= STAT_SUCCESS;
end:
	if(proc_ctx!= NULL) {
		proc_close(&proc_ctx);
	}
	return end_code;
}

static int proc_unregister(procs_ctx_t *procs_ctx, int proc_id,
		log_ctx_t *log_ctx)
{
	procs_reg_elem_t *procs_reg_elem;
	int ret_code;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_id>= 0 && proc_id< PROCS_MAX_NUM_PROC_INSTANCES,
			return STAT_ERROR);
	// Note: argument 'log_ctx' is allowed to be NULL

	/* Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];

	/* Fetch processor.
	 * Note that working on locked module's API critical section guarantee that
	 * 'procs_reg_elem_array' is not accessed concurrently for
	 * registering/unregistering.
	 */
	proc_ctx= procs_reg_elem->proc_ctx;
	if(proc_ctx== NULL)
		return STAT_ENOTFOUND;
	ASSERT(proc_ctx->proc_instance_index== proc_id);

	/* Unblock processor input/output FIFOs to be able to acquire i/o locks */
	ret_code= proc_opt(proc_ctx, "PROC_UNBLOCK");
	CHECK_DO(ret_code== STAT_SUCCESS, return STAT_ERROR);

	/* Lock processor API and i/o critical sections.
	 * Delete processor reference from array register.
	 */
	ASSERT(pthread_mutex_lock(&procs_reg_elem->api_mutex)== 0);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	procs_reg_elem->proc_ctx= NULL;
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	ASSERT(pthread_mutex_unlock(&procs_reg_elem->api_mutex)== 0);

	/* Once processor register was deleted (and thus not accessible
	 * by any concurrent thread performing i/o), release corresponding context
	 * structure.
	 */
	proc_close(&proc_ctx);
	ASSERT(proc_ctx== NULL);
	LOGD("<<%s\n", __FUNCTION__); //comment-me
	return STAT_SUCCESS;
}

static int procs_id_opt(procs_ctx_t *procs_ctx, const char *tag,
		log_ctx_t *log_ctx, va_list arg)
{
	procs_reg_elem_t *procs_reg_elem;
	int proc_id, end_code= STAT_ERROR;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	/* Double lock technique to access processor instance:
	 * - lock module instance API critical section;
	 * - Fetch processor using Id. argument;
	 * - If fetching succeed, lock processor API level critical section;
	 * - release module instance API level critical section.
	 */
	ASSERT(pthread_mutex_lock(&procs_ctx->api_mutex)== 0);
	proc_id= va_arg(arg, int);
	CHECK_DO(proc_id>= 0 && proc_id< PROCS_MAX_NUM_PROC_INSTANCES, goto end);
	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];
	proc_ctx= procs_reg_elem->proc_ctx;
	if(proc_ctx!= NULL)
		ASSERT(pthread_mutex_lock(&procs_reg_elem->api_mutex)== 0);
	ASSERT(pthread_mutex_unlock(&procs_ctx->api_mutex)== 0);
	if(proc_ctx== NULL) {
		end_code= STAT_ENOTFOUND;
		goto end;
	}
	ASSERT(proc_ctx->proc_instance_index== proc_id);

	if(TAG_IS("PROCS_ID_GET")) {
		end_code= proc_opt(proc_ctx, "PROC_GET", va_arg(arg, char**));
	} else if(TAG_IS("PROCS_ID_PUT")) {
		end_code= proc_opt(proc_ctx, "PROC_PUT", va_arg(arg, const char*));
	} else if(TAG_IS("PROCS_ID_UNBLOCK")) {
		end_code= proc_opt(proc_ctx, "PROC_UNBLOCK");
	} else {
		// We assume is a private option for the processor
		end_code= proc_vopt(proc_ctx, tag, arg);
	}

end:
	/* Do not forget to unlock processor API critical section if applicable */
	if(proc_ctx!= NULL)
		pthread_mutex_unlock(&procs_reg_elem->api_mutex);
	return end_code;
}
