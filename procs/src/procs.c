/*
 * Copyright (c) 2017, 2018 Rafael Antoniello
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
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/uri_parser.h>
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
#define TAG_IS(TAG) (strcmp(tag, TAG)== 0)

/**
 * Macro: lock PROCS module *instance* API critical section.
 */
#define LOCK_PROCS_CTX_API(PROCS_CTX) \
	ASSERT(pthread_mutex_lock(&PROCS_CTX->api_mutex)== 0);

/**
 * Macro: unlock PROCS module *instance* API critical section.
 */
#define UNLOCK_PROCS_CTX_API(PROCS_CTX) \
	ASSERT(pthread_mutex_unlock(&PROCS_CTX->api_mutex)== 0);

/**
 * Macro: lock PROCS instance *register element* API critical section.
 * It is important to remark that this macro is thought to make sure the
 * developer lock *instance* API critical section first.
 */
#define LOCK_PROCS_REG_ELEM_API(PROCS_CTX, REG_ELEM, EXIT_CODE_ON_FAILURE) \
	if(pthread_mutex_trylock(&PROCS_CTX->api_mutex)!= EBUSY) {\
		EXIT_CODE_ON_FAILURE;\
	}\
	ASSERT(pthread_mutex_lock(&REG_ELEM->api_mutex)== 0);

/**
 * Macro: unlock PROCS instance *register element* API critical section.
 */
#define UNLOCK_PROCS_REG_ELEM_API(REG_ELEM) \
	ASSERT(pthread_mutex_unlock(&REG_ELEM->api_mutex)== 0);

/**
 * Limit value for the maximum number of processors that can be instantiated
 * in the system.
 * This value may be just modified, in compilation, if needed.
 */
#define PROCS_MAX_NUM_PROC_INSTANCES 8192

/*
 * Input/output processor's FIFOs size.
 * //TODO: This value should be passed as a module setting in the future.
 */
#define PROCS_FIFO_SIZE 2

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
	 * Processor's API options are available through the function 'proc_opt()'
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
	 * Module's API REST prefix name (256 characters maximum).
	 * This parameter can be set by 'procs_open()' function; if not
	 * (NULL is specified), the default name "procs" is used.
	 */
	char prefix_name[256];
	/**
	 * Module's API REST href attribute specifying the URL path
	 * the API refers to. This parameter is optional (may be NULL).
	 */
	char *procs_href;
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
	 * This array is defined with a fixed maximum size (set when calling
	 * the function 'procs_open()', but limited to a maximum of
	 * PROCS_MAX_NUM_PROC_INSTANCES); each element of the array has a set of
	 * locks already initialized to enable concurrency.
	 */
	procs_reg_elem_t *procs_reg_elem_array;
	/**
	 * Size of the array of registered processors.
	 * See 'procs_ctx_s::procs_reg_elem_array'
	 */
	size_t procs_reg_elem_array_size;
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
		char **ref_rest_str, const char *filter_str);
static int proc_register(procs_ctx_t *procs_ctx, const char *proc_name,
		const char *settings_str, log_ctx_t *log_ctx, int *ref_id, va_list arg);
static int proc_unregister(procs_ctx_t *procs_ctx, int id, log_ctx_t *log_ctx);

static int procs_id_opt(procs_ctx_t *procs_ctx, const char *tag,
		log_ctx_t *log_ctx, va_list arg);

static int procs_id_get(procs_reg_elem_t *procs_reg_elem, proc_ctx_t *proc_ctx,
		log_ctx_t *log_ctx, void **ref_reponse);
static proc_ctx_t* procs_id_opt_fetch_proc_ctx(procs_ctx_t *procs_ctx,
		int proc_id, const char *tag, va_list arg, log_ctx_t *log_ctx);

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
		LOGW("'PROCS' module already initialized\n");
		return STAT_NOTMODIFIED;
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

procs_ctx_t* procs_open(log_ctx_t *log_ctx, size_t max_procs_num,
		const char *prefix_name, const char *procs_href)
{
	int proc_id, i, ret_code, end_code= STAT_ERROR;
	procs_ctx_t *procs_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check module initialization */
	if(procs_module_ctx== NULL) {
		LOGE("'PROCS' module should be initialized previously\n");
		return NULL;
	}

	/* Check arguments */
	// Argument 'log_ctx' is allowed to be NULL
	if(max_procs_num> PROCS_MAX_NUM_PROC_INSTANCES) {
		LOGE("Specified maximum number of processor exceeds system capacity\n");
		return NULL;
	}
	// Argument 'prefix_name' is allowed to be NULL

	/* Allocate context structure */
	procs_ctx= (procs_ctx_t*)calloc(1, sizeof(procs_ctx_t));
	CHECK_DO(procs_ctx!= NULL, goto end);

	/* **** Initialize context **** */

	if(prefix_name!= NULL && strlen(prefix_name)> 0) {
		CHECK_DO(strlen(prefix_name)< sizeof(procs_ctx->prefix_name),
				goto end);
		snprintf(procs_ctx->prefix_name, sizeof(procs_ctx->prefix_name),
				"%s", prefix_name);
	} else {
		snprintf(procs_ctx->prefix_name, sizeof(procs_ctx->prefix_name),
				"procs");
	}

	if(procs_href!= NULL && strlen(procs_href)> 0) {
		procs_ctx->procs_href= strdup(procs_href);
		CHECK_DO(procs_ctx->procs_href!= NULL, goto end);
	}

	ret_code= pthread_mutex_init(&procs_ctx->api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	procs_ctx->procs_reg_elem_array= (procs_reg_elem_t*)malloc(max_procs_num*
			sizeof(procs_reg_elem_t));
	CHECK_DO(procs_ctx->procs_reg_elem_array!= NULL, goto end)
	for(proc_id= 0; proc_id< max_procs_num; proc_id++) {
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

	/* Important note:
	 * We update the register array size *after* we successfully allocated and
	 * initialized the array; otherwise the size keeps set to zero
	 * (thus, we can securely execute 'procs_close()' on failure, as other
	 * PROCS module functions).
	 */
	procs_ctx->procs_reg_elem_array_size= max_procs_num;

	procs_ctx->log_ctx= log_ctx;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		procs_close(&procs_ctx);
	return procs_ctx;
}

void procs_close(procs_ctx_t **ref_procs_ctx)
{
	procs_ctx_t *procs_ctx;
	int procs_reg_elem_array_size, proc_id, i;
	LOG_CTX_INIT(NULL);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	if(ref_procs_ctx== NULL || (procs_ctx= *ref_procs_ctx)== NULL)
		return;

	LOG_CTX_SET(procs_ctx->log_ctx);

	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;

	/* First of all release all the processors (note that for deleting
	 * the processors we need the processor IF type to be still available).
	 */
	LOCK_PROCS_CTX_API(procs_ctx);
	if(procs_ctx->procs_reg_elem_array!= NULL) {
		for(proc_id= 0; proc_id< procs_reg_elem_array_size; proc_id++) {
			LOGD("unregistering proc with Id.: %d\n", proc_id); //comment-me
			proc_unregister(procs_ctx, proc_id, LOG_CTX_GET());
		}
	}
	UNLOCK_PROCS_CTX_API(procs_ctx);

	/* Module's API REST href attribute */
	if(procs_ctx->procs_href!= NULL) {
		free(procs_ctx->procs_href);
		procs_ctx->procs_href= NULL;
	}

	/* Module's instance API mutual exclusion lock */
	ASSERT(pthread_mutex_destroy(&procs_ctx->api_mutex)== 0);

	/* Array listing the registered processor instances */
	if(procs_ctx->procs_reg_elem_array!= NULL) {
		for(proc_id= 0; proc_id< procs_reg_elem_array_size; proc_id++) {
			procs_reg_elem_t *procs_reg_elem=
					&procs_ctx->procs_reg_elem_array[proc_id];

			ASSERT(pthread_mutex_destroy(&procs_reg_elem->api_mutex)== 0);

			for(i= 0; i< PROC_IO_NUM; i++)
				fair_lock_close(&procs_reg_elem->fair_lock_io_array[i]);
		}
		free(procs_ctx->procs_reg_elem_array);
		procs_ctx->procs_reg_elem_array= NULL;
	}

	/* Release module's instance context structure */
	free(procs_ctx);
	*ref_procs_ctx= NULL;

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
	int procs_reg_elem_array_size, end_code= STAT_ERROR;
	procs_reg_elem_t *procs_reg_elem;
	fair_lock_t *p_fair_lock= NULL;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;
	CHECK_DO(proc_id>= 0 && proc_id< procs_reg_elem_array_size,
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
	int procs_reg_elem_array_size, end_code= STAT_ERROR;
	procs_reg_elem_t *procs_reg_elem;
	fair_lock_t *p_fair_lock= NULL;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;
	CHECK_DO(proc_id>= 0 && proc_id< procs_reg_elem_array_size,
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
	//LOGV("Registering processor with name: '%s'\n",
	//		proc_if_cpy->proc_name); //comment-me
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
		if(strcmp(proc_if_nth->proc_name, proc_name)== 0)
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
	LOCK_PROCS_CTX_API(procs_ctx);

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
		char **ref_rest_str= va_arg(arg, char**);
		const char *filter_str= va_arg(arg, const char*);
		end_code= procs_rest_get(procs_ctx, LOG_CTX_GET(), ref_rest_str,
				filter_str);
	}  else if(TAG_IS("PROCS_ID_DELETE")) {
		register int id= va_arg(arg, int);
		end_code= proc_unregister(procs_ctx, id, LOG_CTX_GET());
	} else {
		LOGE("Unknown option\n");
		end_code= STAT_ENOTFOUND;
	}

	UNLOCK_PROCS_CTX_API(procs_ctx);
	return end_code;
#undef PROC_ID_STR_FMT
}

static int procs_rest_get(procs_ctx_t *procs_ctx, log_ctx_t *log_ctx,
		char **ref_rest_str, const char *filter_str)
{
	int i, ret_code, end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL, *cjson_proc= NULL;
	cJSON *cjson_aux= NULL, *cjson_procs; // Do not release
	const char *filter_proc_name= NULL, *filter_proc_notname= NULL;
	char href[1024]= {0};
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_rest_str!= NULL, return STAT_ERROR);
	//argument 'filter_str' is allowed to be NULL

	/*  Check that module instance API critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	*ref_rest_str= NULL;

	/* JSON structure is as follows:
	 * {
	 *     <selected prefix_name>("procs" by default):[
	 *         {
	 *             "proc_id":number,
	 *             "proc_name":string,
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
	cJSON_AddItemToObject(cjson_rest, procs_ctx->prefix_name, cjson_procs);

	/* Parse the filter if available */
	if(filter_str!= NULL) {
		size_t filter_proc_name_len= strlen("proc_nameX=");
		if(strncmp(filter_str, "proc_name==", filter_proc_name_len)== 0)
			filter_proc_name= filter_str+ filter_proc_name_len;
		else if(strncmp(filter_str, "proc_name!=", filter_proc_name_len)== 0)
			filter_proc_notname= filter_str+ filter_proc_name_len;
	}

	/* Compose the REST list */
	for(i= 0; i< procs_ctx->procs_reg_elem_array_size; i++) {
		const char *proc_name;
		const proc_if_t *proc_if;
		register int proc_instance_index;
		cJSON *cjson_links, *cjson_link;
		proc_ctx_t *proc_ctx= NULL;
		procs_reg_elem_t *procs_reg_elem=
				&procs_ctx->procs_reg_elem_array[i];

		if((proc_ctx= procs_reg_elem->proc_ctx)== NULL)
			continue;

		proc_instance_index= proc_ctx->proc_instance_index;
		CHECK_DO(proc_instance_index== i, continue);

		proc_if= proc_ctx->proc_if;
		CHECK_DO(proc_if!= NULL, continue);
		proc_name= proc_if->proc_name;
		CHECK_DO(proc_name!= NULL, continue);

		/* Check filters */
		if(filter_proc_name!= NULL) {
			if(strcmp(filter_proc_name, proc_name)!= 0)
				continue;
		} else if(filter_proc_notname!= NULL) {
			if(strcmp(filter_proc_notname, proc_name)== 0)
				continue;
		}

		if(cjson_proc!= NULL) {
			cJSON_Delete(cjson_proc);
			cjson_proc= NULL;
		}
		cjson_proc= cJSON_CreateObject();
		CHECK_DO(cjson_proc!= NULL, goto end);
		//cJSON_AddItemToArray(cjson_procs, cjson_proc); //at the end of loop

		/* 'proc_id' */
		cjson_aux= cJSON_CreateNumber((double)proc_instance_index);
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_proc, "proc_id", cjson_aux);

		/* 'proc_name' */
		cjson_aux= cJSON_CreateString(proc_name);
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_proc, "proc_name", cjson_aux);

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

		snprintf(href, sizeof(href), "%s/%s/%d.json",
				procs_ctx->procs_href!= NULL? procs_ctx->procs_href: "",
						procs_ctx->prefix_name, proc_instance_index);
		cjson_aux= cJSON_CreateString(href);
		CHECK_DO(cjson_aux!= NULL, goto end);
		cJSON_AddItemToObject(cjson_link, "href", cjson_aux);

		/* Finally attach to REST list */
		cJSON_AddItemToArray(cjson_procs, cjson_proc);
		cjson_proc= NULL; // Avoid double referencing
	}

	/* Print cJSON structure data to char string */
	*ref_rest_str= CJSON_PRINT(cjson_rest);
	CHECK_DO(*ref_rest_str!= NULL && strlen(*ref_rest_str)> 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(cjson_proc!= NULL)
		cJSON_Delete(cjson_proc);
	return end_code;
}

static int proc_register(procs_ctx_t *procs_ctx, const char *proc_name,
		const char *settings_str, log_ctx_t *log_ctx, int *ref_id, va_list arg)
{
	procs_reg_elem_t *procs_reg_elem;
	const proc_if_t *proc_if;
	int procs_reg_elem_array_size, ret_code, end_code= STAT_ERROR;
	int proc_id= -1, flag_force_proc_id= 0;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	char *proc_id_str= NULL;
	cJSON *cjson_settings= NULL;
	cJSON *cjson_aux= NULL; // Do not release
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

	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;

	*ref_id= -1; // Set to invalid (undefined) value

	/* Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, goto end);

	/* **** Get processor Id. ****
	 * API user has the option to request to force the processor Id. to a
	 * proposed value (passed as setting: 'forced_proc_id=number').
	 */
	/* Guess settings string representation format (JSON-REST or Query) */
	flag_is_query= (settings_str[0]=='{' &&
			settings_str[strlen(settings_str)-1]=='}')? 0: 1;

	/* Parse JSON or query string and check for 'forced_proc_id' field */
	if(flag_is_query== 1) {
		proc_id_str= uri_parser_query_str_get_value("forced_proc_id",
				settings_str);
		if(proc_id_str!= NULL) {
			proc_id= atoll(proc_id_str);
			flag_force_proc_id= 1;
		}
	} else {
		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_settings= cJSON_Parse(settings_str);
		CHECK_DO(cjson_settings!= NULL, goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_settings, "forced_proc_id");
		if(cjson_aux!= NULL) {
			proc_id= cjson_aux->valuedouble;
			flag_force_proc_id= 1;
		}
	}

	/* If a forced processor Id. was not requested, get one */
	if(flag_force_proc_id== 0) {
		/* Get free slot where to register new processor */
		for(proc_id= 0; proc_id< procs_reg_elem_array_size; proc_id++) {
			if(procs_ctx->procs_reg_elem_array[proc_id].proc_ctx== NULL)
				break;
		}
	}
	if(proc_id< 0) {
		LOGE("Invalid procesor identifier requested (Id. %d)\n", proc_id);
		end_code= STAT_EINVAL;
		goto end;
	}
	if(proc_id>= procs_reg_elem_array_size) {
		LOGE("Maximum number of allowed processor instances exceeded\n");
		end_code= STAT_ENOMEM;
		goto end;
	}
	// In case Id. was forced, we need to check if slot is empty
	if(procs_ctx->procs_reg_elem_array[proc_id].proc_ctx!= NULL) {
		LOGE("Processor Id. conflict: requested Id. is being used.\n");
		end_code= STAT_ECONFLICT;
		goto end;
	}

	/* Note that working on a locked module instance API critical section
	 * guarantee that 'procs_reg_elem_array' is not accessed concurrently
	 * for registering/unregistering. Nevertheless, 'procs_reg_elem_array'
	 * is still accessible concurrently by module's i/o operations, until
	 * corresponding "fair-lock" is locked (in the code below we will lock
	 * i/o "fair-locks" to register the new processor).
	 */
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

	/* Register processor context structure.
	 * At this point we lock corresponding i/o critical section ("fair" lock),
	 * as this register slot is still accessible concurrently by processor
	 * API and i/o operations.
	 */
	LOCK_PROCS_REG_ELEM_API(procs_ctx, procs_reg_elem, goto end);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	procs_reg_elem->proc_ctx= proc_ctx;
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	UNLOCK_PROCS_REG_ELEM_API(procs_reg_elem);
	proc_ctx= NULL; // Avoid double referencing

	*ref_id= proc_id;
	end_code= STAT_SUCCESS;
end:
	if(proc_ctx!= NULL)
		proc_close(&proc_ctx);
	if(proc_id_str!= NULL)
		free(proc_id_str);
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	return end_code;
}

static int proc_unregister(procs_ctx_t *procs_ctx, int proc_id,
		log_ctx_t *log_ctx)
{
	procs_reg_elem_t *procs_reg_elem;
	int procs_reg_elem_array_size, ret_code;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;
	CHECK_DO(proc_id>= 0 && proc_id< procs_reg_elem_array_size,
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
	LOCK_PROCS_REG_ELEM_API(procs_ctx, procs_reg_elem, return STAT_ERROR);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	procs_reg_elem->proc_ctx= NULL;
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	UNLOCK_PROCS_REG_ELEM_API(procs_reg_elem);

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
	int procs_reg_elem_array_size, end_code= STAT_ERROR, proc_id= -1;
	int flag_procs_api_locked= 0, flag_proc_ctx_api_locked= 0;
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_module_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	/* Implementation note:
	 * We use a double lock technique to access processor instance:
	 * - Lock PROCS module instance API critical section (while we are
	 * accessing processors register);
	 * - Lock processor (PROC) API critical section;
	 * - Fetch processor using Id. argument;
	 * - If fetching succeed, unlock PROCS module instance API critical
	 * section (we are able to leave processors register critical section);
	 * - ... use processor instance to treat options ...
	 * - Unlock processor (PROC) API critical section.
	 */

	/* Lock PROCS module instance API critical section */
	LOCK_PROCS_CTX_API(procs_ctx);
	flag_procs_api_locked= 1;

	/* Lock processor (PROC) API critical section */
	proc_id= va_arg(arg, int);
	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;
	CHECK_DO(proc_id>= 0 && proc_id< procs_reg_elem_array_size, goto end);
	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];
	LOCK_PROCS_REG_ELEM_API(procs_ctx, procs_reg_elem, goto end);
	flag_proc_ctx_api_locked= 1;

	/* Fetch processor */
	proc_ctx= procs_id_opt_fetch_proc_ctx(procs_ctx, proc_id, tag, arg,
			LOG_CTX_GET());
	if(proc_ctx== NULL) {
		end_code= STAT_ENOTFOUND;
		goto end;
	}
	ASSERT(proc_ctx->proc_instance_index== proc_id); // sanity check

	/* Unlock PROCS module instance API critical section */
	UNLOCK_PROCS_CTX_API(procs_ctx);
	flag_procs_api_locked= 0;

	/* Process options */
	if(TAG_IS("PROCS_ID_GET")) {
		end_code= procs_id_get(procs_reg_elem, proc_ctx, LOG_CTX_GET(),
				(void**)va_arg(arg, char**));
	} else if(TAG_IS("PROCS_ID_PUT")) {
		end_code= proc_opt(proc_ctx, "PROC_PUT", va_arg(arg, const char*));
	} else if(TAG_IS("PROCS_ID_UNBLOCK")) {
		end_code= proc_opt(proc_ctx, "PROC_UNBLOCK");
	} else {
		// We assume is a private option for the processor
		end_code= proc_vopt(proc_ctx, tag, arg);
	}

end:
	/* Check critical sections and unlock if applicable */
	if(flag_procs_api_locked!= 0)
		UNLOCK_PROCS_CTX_API(procs_ctx);
	if(flag_proc_ctx_api_locked!= 0)
		UNLOCK_PROCS_REG_ELEM_API(procs_reg_elem);
	return end_code;
}

static int procs_id_get(procs_reg_elem_t *procs_reg_elem, proc_ctx_t *proc_ctx,
		log_ctx_t *log_ctx, void **ref_reponse)
{
	int ret_code, end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL, *cjson_settings= NULL, *cjson_aux= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_reg_elem!= NULL, return STAT_ERROR);
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//log_ctx allowed to be NULL
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	*ref_reponse= NULL;

	/* Check that processor API level critical section is locked */
	ret_code= pthread_mutex_trylock(&procs_reg_elem->api_mutex);
	CHECK_DO(ret_code== EBUSY, goto end);

	/* GET processor's REST response */
	ret_code= proc_opt(proc_ctx, "PROC_GET", PROC_IF_REST_FMT_CJSON,
			&cjson_rest);
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_rest!= NULL, goto end);

	/* Get settings */
	cjson_settings= cJSON_GetObjectItem(cjson_rest, "settings");
	if(cjson_settings== NULL) {
		cjson_settings= cJSON_CreateObject();
		CHECK_DO(cjson_settings!= NULL, goto end);
		cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	}

	/* **** Add some REST elements at settings top ****
	 * We do a little HACK to insert elements at top as cJSON library does not
	 * support it natively -it always insert at the bottom-
	 * We do this at the risk of braking in a future library version, as we
	 * base current solution on the internal implementation of function
	 * 'cJSON_AddItemToObject()' -may change in future-.
	 */

	/* 'proc_name' */
	cjson_aux= cJSON_CreateString(proc_ctx->proc_if->proc_name);
	CHECK_DO(cjson_aux!= NULL, goto end);
	// Hack of 'cJSON_AddItemToObject(cjson_rest, "proc_name", cjson_aux);':
	cjson_aux->string= (char*)strdup("proc_name");
	cjson_aux->type|= cJSON_StringIsConst;
    //cJSON_AddItemToArray(cjson_rest, cjson_aux);
    cJSON_InsertItemInArray(cjson_settings, 0, cjson_aux); // Insert at top
    cjson_aux->type&= ~cJSON_StringIsConst;

	*ref_reponse= (void*)CJSON_PRINT(cjson_rest);
	CHECK_DO(*ref_reponse!= NULL && strlen((char*)*ref_reponse)> 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

/**
 * This function fetches the processor context structure to be used for
 * API options requests.
 * As a special case to take into account, this function checks if a new
 * processor name is specified on an eventual PUT settings (namely, it checks
 * if 'proc_name' setting is present on a PUT operation, and if it is the
 * case, checks if 'proc_name' actually changes).
 * If a new processor name is requested, the current processor should be
 * released and substituted by a new processor of the new type given
 * (recycling the same processors register slot).
 * Substituted processor settings are backup, and set to the new one. This way,
 * if some setting on the old processor apply, will be set on the new
 * processor. Otherwise, not applicable settings will be just ignored.
 */
static proc_ctx_t* procs_id_opt_fetch_proc_ctx(procs_ctx_t *procs_ctx,
		int proc_id, const char *tag, va_list arg, log_ctx_t *log_ctx)
{
	va_list arg_cpy, va_list_empty;
	procs_reg_elem_t *procs_reg_elem;
	int procs_reg_elem_array_size, ret_code;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	proc_ctx_t *proc_ctx_ret= NULL, *proc_ctx_curr= NULL, *proc_ctx_new= NULL;
	const char *settings_str_arg= NULL; // Do not release
	const proc_if_t *proc_if_curr= NULL, *proc_if_new= NULL; // Do not release
	const char *proc_name_curr= NULL; // Do not release
	char *proc_name_str= NULL;
	cJSON *cjson_rest_arg= NULL, *cjson_rest_curr= NULL;
	cJSON *cjson_aux= NULL; // Do not release
	char *settings_str_curr= NULL;
	uint32_t fifo_ctx_maxsize[PROC_IO_NUM]= {PROCS_FIFO_SIZE, PROCS_FIFO_SIZE};
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_ctx!= NULL, return NULL);
	procs_reg_elem_array_size= procs_ctx->procs_reg_elem_array_size;
	CHECK_DO(proc_id>= 0 && proc_id< procs_reg_elem_array_size, return NULL);
	CHECK_DO(tag!= NULL, return NULL);
	//arg nothing to check
	//log_ctx allowed to be NULL

	/* Get register element and current processor references */
	procs_reg_elem= &procs_ctx->procs_reg_elem_array[proc_id];
	proc_ctx_curr= procs_reg_elem->proc_ctx;
	if(proc_ctx_curr== NULL)
		goto end;

	/* Check that module instance critical section is locked and processor API
	 * level critical section is also locked ("double locked").
	 */
	ret_code= pthread_mutex_trylock(&procs_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, goto end);
	ret_code= pthread_mutex_trylock(&procs_reg_elem->api_mutex);
	CHECK_DO(ret_code== EBUSY, goto end);

	/* In the case we have a PUT operation request we may have to treat a
	 * special case (code below). Otherwise, we're done.
	 */
	if(!TAG_IS("PROCS_ID_PUT")) {
		proc_ctx_ret= proc_ctx_curr;
		goto end;
	}

	/* **** Treat possible special case in PUT operation ****
	 * Check if a new processor type is requested (namely, check for a PUT
	 * operation with a new processor name specified).
	 */

	/* Copy variable list of arguments to avoid interfering with original.
	 * Get settings string argument corresponding to the PUT operation.
	 */
	va_copy(arg_cpy, arg);
	settings_str_arg= va_arg(arg_cpy, const char*);
	if(settings_str_arg== NULL || strlen(settings_str_arg)== 0) {
		// No new processor name requested, we're done.
		proc_ctx_ret= proc_ctx_curr;
		goto end;
	}

	/* Get current processor name */
	proc_if_curr= proc_ctx_curr->proc_if;
	CHECK_DO(proc_if_curr!= NULL, goto end);
	proc_name_curr= proc_if_curr->proc_name;
	CHECK_DO(proc_name_curr!= NULL && strlen(proc_name_curr)> 0, goto end);

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (settings_str_arg[0]=='{' &&
			settings_str_arg[strlen(settings_str_arg)-1]=='}')? 0: 1;

	/* Parse JSON or query string and check for 'proc_name' field */
	if(flag_is_query== 1) {
		proc_name_str= uri_parser_query_str_get_value("proc_name",
				settings_str_arg);
	} else {
		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest_arg= cJSON_Parse(settings_str_arg);
		CHECK_DO(cjson_rest_arg!= NULL, goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest_arg, "proc_name");
		if(cjson_aux!= NULL)
			proc_name_str= strdup(cjson_aux->valuestring);
	}
	if(proc_name_str== NULL) {
		// No new processor name requested, we're done.
		proc_ctx_ret= proc_ctx_curr;
		goto end;
	}
	//LOGV("'proc_name'= '%s' in PUT request\n", proc_name_str); //comment-me

	/* Check if processor name actually change or is the same as current */
	if(strcmp(proc_name_str, proc_name_curr)== 0) {
		// No new processor name requested, we're done.
		proc_ctx_ret= proc_ctx_curr;
		goto end;
	}

	/* Check and get processor interface (lock PROCS module API!) */
	ASSERT(pthread_mutex_lock(&procs_module_ctx->module_api_mutex)== 0);
	proc_if_new= get_proc_if_by_name(proc_name_str, LOG_CTX_GET());
	ASSERT(pthread_mutex_unlock(&procs_module_ctx->module_api_mutex)== 0);
	if(proc_if_new== NULL) {
		LOGE("New processor name specified '%s' is not registered.\n",
				proc_name_str);
		goto end;
	}

	/* **** At this point we have a valid new processor request **** */
	LOGW("Changing processor type from '%s' to '%s'\n",
			proc_if_curr->proc_mime? proc_if_curr->proc_mime: proc_name_curr,
			proc_if_new->proc_mime? proc_if_new->proc_mime: proc_name_str);

	/* Get processor's current settings */
	ret_code= proc_opt(proc_ctx_curr, "PROC_GET", PROC_IF_REST_FMT_CJSON,
			&cjson_rest_curr);
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_rest_curr!= NULL, goto end);
	cjson_aux= cJSON_GetObjectItem(cjson_rest_curr, "settings");
	if(cjson_aux!= NULL) { // May have settings object or not...
		settings_str_curr= cJSON_PrintUnformatted(cjson_aux);
	} else {
		settings_str_curr= strdup("");
	}
	CHECK_DO(settings_str_curr!= NULL, goto end);
	//LOGV("Current processor setting: '%s'\n", settings_str_curr); //comment-me

	/* Open (instantiate) new processor passing current settings and Id.
	 * Note that all the settings fields that do not apply to the new
	 * processor type will be just ignored.
	 */
	proc_ctx_new= proc_open(proc_if_new, settings_str_curr,
			proc_ctx_curr->proc_instance_index, fifo_ctx_maxsize,
			LOG_CTX_GET(), va_list_empty);
	CHECK_DO(proc_ctx_new!= NULL, goto end);

	/* Unblock current processor input/output FIFOs to be able to acquire
	 * i/o locks next.
	 */
	ret_code= proc_opt(proc_ctx_curr, "PROC_UNBLOCK");
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Register new processor context structure.
	 * At this point we lock corresponding i/o critical section ("fair" lock),
	 * as this register slot is still accessible concurrently by processor
	 * API and i/o operations.
	 */
	//LOCK_PROCS_REG_ELEM_API(...); //already
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_lock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	procs_reg_elem->proc_ctx= proc_ctx_new;
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_IPUT]);
	fair_unlock(procs_reg_elem->fair_lock_io_array[PROC_OPUT]);
	//UNLOCK_PROCS_REG_ELEM_API(...); //already
	proc_ctx_new= NULL; // Avoid double referencing

	/* Finally, we release the old and successfully substituted processor */
	proc_close(&proc_ctx_curr);
	ASSERT(proc_ctx_curr== NULL);

	/* Success! */
	proc_ctx_ret= procs_reg_elem->proc_ctx;
end:
	va_end(arg_cpy);
	if(proc_name_str!= NULL)
		free(proc_name_str);
	if(cjson_rest_arg!= NULL)
		cJSON_Delete(cjson_rest_arg);
	if(cjson_rest_curr!= NULL)
		cJSON_Delete(cjson_rest_curr);
	if(settings_str_curr!= NULL)
		free(settings_str_curr);
	if(proc_ctx_new!= NULL) {
		proc_close(&proc_ctx_new);
		ASSERT(proc_ctx_new== NULL);
	}
	return proc_ctx_ret;
}
