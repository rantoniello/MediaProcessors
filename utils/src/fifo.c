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
 * @file fifo.c
 * @author Rafael Antoniello
 */

#include "fifo.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>

#include "check_utils.h"
#include "log.h"
#include "stat_codes.h"

/* **** Definitions **** */

/**
 * Size of structure type 'fifo_elem_ctx_t'.
 * In the case FLAG_USE_SHM flag is set, fifo_elem_ctx_t::shm_elem_pool
 * will be actually a predefined buffer with a maximum fixed size of
 * 'CHUNK_SIZE_MAX' octets. Otherwise, member fifo_elem_ctx_t::shm_elem_pool
 * will not be used (giving a minimum legacy size).
 */
#define SIZEOF_FIFO_ELEM_CTX_T(FLAG_USE_SHM, CHUNK_SIZE_MAX) \
	(size_t)(sizeof(fifo_elem_ctx_t)+ (FLAG_USE_SHM== 0? 1: CHUNK_SIZE_MAX))

/**
 * FIFO element context structure.
 */
typedef struct fifo_elem_ctx_s {
	/**
	 * Element size in bytes.
	 */
	ssize_t size;
	/**
	 * Element pointer.
	 */
	void *elem;
	/**
	 * Only used in the case of shared memory; fifo_elem_ctx_t::elem will
	 * point to this memory pool of a fixed maximum size
	 * (fifo_elem_ctx_t::size <= pool maximum size)
	 */
	uint8_t shm_elem_pool[]; //flexible array member must be last
} fifo_elem_ctx_t;

/**
 * FIFO context structure.
 */
typedef struct fifo_ctx_s {
	/**
	 * Module flags:
	 * - FIFO_O_NONBLOCK
	 * - FIFO_PROCESS_SHARED
	 */
	volatile uint32_t flags;
	/**
	 * Exit flag: if set to non-zero value, FIFO module should finish/unblock
	 * transactions as fast as possible
	 */
	volatile int flag_exit;
	/**
	 * Used only if FIFO_PROCESS_SHARED is signaled.
	 * In this case, this file-name is assigned to the shared memory object
	 * to facilitate opening FIFO from an fork-exec setting.
	 */
#define FIFO_FILE_NAME_MAX_SIZE 1024
	char fifo_file_name[FIFO_FILE_NAME_MAX_SIZE];
	/**
	 * Externally defined duplication function.
	 * Not applicable when using shared memory.
	 */
	fifo_elem_ctx_dup_fxn_t *elem_ctx_dup;
	/**
	 * Externally defined releasing functions.
	 * Not applicable when using shared memory.
	 */
	fifo_elem_ctx_release_fxn_t *elem_ctx_release;
	/**
	 * Module API mutex.
	 */
	pthread_mutex_t api_mutex;
	int flag_api_mutex_initialized;
	/**
	 * Signals each time a new chunk enters the FIFO buffer.
	 */
	pthread_cond_t buf_put_signal;
	int flag_buf_put_signal_initialized;
	/**
	 * Signals each time a new chunk is consumed from the FIFO buffer.
	 */
	pthread_cond_t buf_get_signal;
	int flag_buf_get_signal_initialized;
	/**
	 * Number of slots currently used.
	 */
	volatile ssize_t slots_used_cnt;
	/**
	 * Summation of all the size values of the chunk-buffers that compose the
	 * input buffer. Namely, is the overall input buffer level.
	 */
	volatile ssize_t buf_level;
	/**
	 * Receiver chunk-buffer index.
	 * Each time a chunk-buffer is filled with new data, we increment this
	 * index to point to the next empty receiving buffer.
	 */
	volatile int input_idx;
	/**
	 * Consumer chunk-buffer index.
	 * Each time a chunk-buffer is consumed (emptied) by a consuming process,
	 * we increment this index to point to the next full buffer ready to be
	 * processed.
	 */
	volatile int output_idx;
	/**
	 * Maximum number of element-slots (namely, maximum number of possible
	 * chunks) of the FIFO buffer.
	 */
	size_t buf_slots_max;
	/**
	 * Maximum permitted size of chunks [bytes].
	 * In the case of shared memory, this value *must* be set greater than
	 * zero.
	 * Otherwise, setting this value to zero means no limit to input chunk
	 * size.
	 */
	size_t chunk_size_max;
	/**
	 * This is a circular buffer of pointers to chunks of data.
	 * Instead of managing a single pool of data, the buffer stores pointers to
	 * a fixed number of chunk objects, each one holding the reference and the
	 * size of each chunk.
	 */
	fifo_elem_ctx_t buf[]; //flexible array member must be last
} fifo_ctx_t;

/* **** Prototypes **** */

static void fifo_close_internal(fifo_ctx_t **ref_fifo_ctx, int flag_deinit);
static int fifo_init(fifo_ctx_t *fifo_ctx, size_t slots_max,
		size_t chunk_size_max, uint32_t flags, const char *fifo_file_name,
		const fifo_elem_alloc_fxn_t *fifo_elem_alloc_fxn);
static void fifo_deinit(fifo_ctx_t *fifo_ctx);

static inline int fifo_input(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t elem_size, int dup_flag);
static inline int fifo_output(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t *ref_elem_size, int flush_flag, int64_t tout_usecs);

static int fifo_mutex_init(pthread_mutex_t * const pthread_mutex_p,
		int flag_use_shm, log_ctx_t *log_ctx);
static int fifo_cond_init(pthread_cond_t * const pthread_cond_p,
		int flag_use_shm, log_ctx_t *log_ctx);

/* **** Implementations **** */

fifo_ctx_t* fifo_open(size_t slots_max, size_t chunk_size_max,
		uint32_t flags, const fifo_elem_alloc_fxn_t *fifo_elem_alloc_fxn)
{
	size_t fifo_ctx_size;
	fifo_ctx_t *fifo_ctx= NULL;
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(slots_max> 0, return NULL);
	// 'chunk_size_max' may be zero (means no limitation in chunk size)
	CHECK_DO((flags& FIFO_PROCESS_SHARED)== 0, return NULL);
	// 'fifo_elem_alloc_fxn' may be NULL

	/* Allocate FIFO context structure */
	fifo_ctx_size= sizeof(fifo_ctx_t)+ slots_max* (sizeof(fifo_elem_ctx_t)+ 1);
	fifo_ctx= calloc(1, fifo_ctx_size);
	CHECK_DO(fifo_ctx!= NULL, goto end);

	/* Initialize rest of FIFO context structure.
	 * Note that FIFO file-name do *not* apply when *not* using shared-memory
	 * (thus we pass NULL pointer).
	 */
	ret_code= fifo_init(fifo_ctx, slots_max, chunk_size_max, flags, NULL,
			fifo_elem_alloc_fxn);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fifo_close(&fifo_ctx);
	return fifo_ctx;
}

fifo_ctx_t* fifo_shm_open(size_t slots_max, size_t chunk_size_max,
		uint32_t flags, const char *fifo_file_name)
{
	size_t fifo_ctx_size;
	fifo_ctx_t *fifo_ctx= NULL;
	int ret_code, end_code= STAT_ERROR, shm_fd= -1;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(slots_max> 0, return NULL);
	CHECK_DO(chunk_size_max> 0, return NULL);
	// 'flags' may take any value
	CHECK_DO(fifo_file_name!= NULL, return NULL);

	/* Add shared memory flag */
	flags|= FIFO_PROCESS_SHARED;

	/* Compute the size of the FIFO context structure to be allocated */
	fifo_ctx_size= sizeof(fifo_ctx_t)+ slots_max*
			(sizeof(fifo_elem_ctx_t)+ chunk_size_max);

	/* Create the shared memory segment */
	shm_fd= shm_open(fifo_file_name, O_CREAT| O_RDWR, S_IRUSR | S_IWUSR);
	CHECK_DO(shm_fd>= 0,LOGE("errno: %d\n", errno); goto end);

	/* Configure size of the shared memory segment */
	CHECK_DO(ftruncate(shm_fd, fifo_ctx_size)== 0, goto end);

	/* Map the shared memory segment in the address space of the process */
	fifo_ctx= mmap(NULL, fifo_ctx_size, PROT_READ| PROT_WRITE, MAP_SHARED,
			shm_fd, 0);
	if(fifo_ctx== MAP_FAILED)
		fifo_ctx= NULL;
	CHECK_DO(fifo_ctx!= NULL, goto end);
	memset(fifo_ctx, 0, fifo_ctx_size);

	/* Initialize rest of FIFO context structure.
	 * Note that FIFO element allocation/release external callback functions
	 * do not apply when using shared-memory (thus we pass NULL pointer).
	 */
	ret_code= fifo_init(fifo_ctx, slots_max, chunk_size_max, flags,
			fifo_file_name, NULL);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
	/* We will not need file descriptor any more */
	if(shm_fd>= 0) {
		ASSERT(close(shm_fd)== 0);
	}
	if(end_code!= STAT_SUCCESS)
		fifo_close(&fifo_ctx);
	return fifo_ctx;
}

fifo_ctx_t* fifo_shm_exec_open(size_t slots_max, size_t chunk_size_max,
		uint32_t flags, const char *fifo_file_name)
{
	size_t fifo_ctx_size;
	fifo_ctx_t *fifo_ctx= NULL;
	int end_code= STAT_ERROR, shm_fd= -1;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(slots_max> 0, return NULL);
	CHECK_DO(chunk_size_max> 0, return NULL);
	// 'flags' may take any value
	CHECK_DO(fifo_file_name!= NULL, return NULL);

	/* Compute the size of the FIFO context structure to be allocated */
	fifo_ctx_size= sizeof(fifo_ctx_t)+ slots_max*
			(sizeof(fifo_elem_ctx_t)+ chunk_size_max);

	/* Create the shared memory segment */
	shm_fd= shm_open(fifo_file_name, O_RDWR, S_IRUSR | S_IWUSR);
	CHECK_DO(shm_fd>= 0,LOGE("errno: %d\n", errno); goto end);

	/* Map the shared memory segment in the address space of the process */
	fifo_ctx= mmap(NULL, fifo_ctx_size, PROT_READ| PROT_WRITE, MAP_SHARED,
			shm_fd, 0);
	if(fifo_ctx== MAP_FAILED)
		fifo_ctx= NULL;
	CHECK_DO(fifo_ctx!= NULL, goto end);

	//LOGV("FIFO flags are: '0x%0x\n", fifo_ctx->flags); //comment-me

	end_code= STAT_SUCCESS;
end:
	/* We will not need file descriptor any more */
	if(shm_fd>= 0) {
		ASSERT(close(shm_fd)== 0);
	}
	if(end_code!= STAT_SUCCESS)
		fifo_shm_exec_close(&fifo_ctx);
	return fifo_ctx;
}

void fifo_close(fifo_ctx_t **ref_fifo_ctx)
{
	fifo_close_internal(ref_fifo_ctx, 1);
}

void fifo_shm_exec_close(fifo_ctx_t **ref_fifo_ctx)
{
	fifo_close_internal(ref_fifo_ctx, 0);
}

void fifo_set_blocking_mode(fifo_ctx_t *fifo_ctx, int do_block)
{
	LOG_CTX_INIT(NULL);

	CHECK_DO(fifo_ctx!= NULL, return);

	pthread_mutex_lock(&fifo_ctx->api_mutex);

	/* Set the 'non-blocking' bit-flag */
	if(do_block!= 0) {
		fifo_ctx->flags&= ~((uint32_t)FIFO_O_NONBLOCK);
	} else {
		fifo_ctx->flags|= (uint32_t)FIFO_O_NONBLOCK;
	}

	/* Announce to unblock conditional waits */
	pthread_cond_broadcast(&fifo_ctx->buf_put_signal);
	pthread_cond_broadcast(&fifo_ctx->buf_get_signal);

	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	return;
}

int fifo_put_dup(fifo_ctx_t *fifo_ctx, const void *elem, size_t elem_size)
{
	void *p= (void*)elem;
	return fifo_input(fifo_ctx, &p, elem_size, 1/*duplicate*/);
}

int fifo_put(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t elem_size)
{
	return fifo_input(fifo_ctx, ref_elem, elem_size, 0/*do not duplicate*/);
}

int fifo_get(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size)
{
	return fifo_output(fifo_ctx, ref_elem, ref_elem_size, 1/*flush FIFO*/,
			-1/*no time-out*/);
}

int fifo_timedget(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size,
		int64_t tout_usecs)
{
	return fifo_output(fifo_ctx, ref_elem, ref_elem_size, 1/*flush FIFO*/,
			tout_usecs/*user specified time-out*/);
}

int fifo_show(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size)
{
	return fifo_output(fifo_ctx, ref_elem, ref_elem_size,
			0/*do NOT flush FIFO*/, -1/*no time-out*/);
}

ssize_t fifo_get_buffer_level(fifo_ctx_t *fifo_ctx)
{
	ssize_t buf_level= -1; // invalid value to indicate STAT_ERROR
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return -1);

	pthread_mutex_lock(&fifo_ctx->api_mutex);
	buf_level= fifo_ctx->buf_level;
	pthread_mutex_unlock(&fifo_ctx->api_mutex);

	return buf_level;
}

int fifo_traverse(fifo_ctx_t *fifo_ctx, int elem_cnt,
		void (*it_fxn)(void *elem, ssize_t elem_size, int idx, void *it_arg,
				int *ref_flag_break),
		void *it_arg)
{
	int flag_use_shm;
	size_t buf_slots_max, chunk_size_max;
	ssize_t slots_used_cnt;
	int i, cnt, cnt_max, flag_break;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(elem_cnt> 0 || elem_cnt== -1, return STAT_ERROR);
	CHECK_DO(it_fxn!= NULL, return STAT_ERROR);

	/* Lock API MUTEX */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	flag_use_shm= fifo_ctx->flags& FIFO_PROCESS_SHARED;
	buf_slots_max= fifo_ctx->buf_slots_max;
	chunk_size_max= fifo_ctx->chunk_size_max;

	/* Iterate: we do it beginning with the input index (namely, we go from
	 * the newest queued element to the oldest).
	 */
	slots_used_cnt= fifo_ctx->slots_used_cnt;
	if(elem_cnt== -1)
		elem_cnt= slots_used_cnt; // '-1' means "traverse all the FIFO"
	cnt_max= (elem_cnt< slots_used_cnt)? elem_cnt: slots_used_cnt;
	flag_break= 0;
	for(i= fifo_ctx->input_idx- 1, cnt= 0; cnt< cnt_max; cnt++) {
		fifo_elem_ctx_t *fifo_elem_ctx= (fifo_elem_ctx_t*)(
				(uint8_t*)fifo_ctx->buf+
				i* SIZEOF_FIFO_ELEM_CTX_T(flag_use_shm, chunk_size_max));

		/* Execute iterator callback function */
		it_fxn(fifo_elem_ctx->elem, fifo_elem_ctx->size, i, it_arg,
				&flag_break);
		if(flag_break!= 0)
			break;

		/* Update for next iteration; note that 'buf_slots_max' is > 0 in
		 * modulo operation:
		 * integer r = a % b;
		 * r= r < 0 ? r + b : r; <- Only works if B> 0
		 */
		i= (i- 1)% buf_slots_max;
		if(i< 0)
			i= i+ buf_slots_max;
	}

	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	return STAT_SUCCESS;
}

void fifo_empty(fifo_ctx_t *fifo_ctx)
{
	int i, flag_use_shm;
	size_t buf_slots_max, chunk_size_max;
	LOG_CTX_INIT(NULL);

	CHECK_DO(fifo_ctx!= NULL, return);

	/* Lock API mutex */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	flag_use_shm= fifo_ctx->flags& FIFO_PROCESS_SHARED;
	buf_slots_max= fifo_ctx->buf_slots_max;
	chunk_size_max= fifo_ctx->chunk_size_max;

	/* Release all the elements available in FIFO buffer */
	for(i= 0; i< buf_slots_max; i++) {
		fifo_elem_ctx_t *fifo_elem_ctx= (fifo_elem_ctx_t*)(
				(uint8_t*)fifo_ctx->buf+
				i* SIZEOF_FIFO_ELEM_CTX_T(flag_use_shm, chunk_size_max));
		if(fifo_elem_ctx->elem!= NULL) {
			if(fifo_ctx->elem_ctx_release!= NULL) {
				fifo_ctx->elem_ctx_release(&fifo_elem_ctx->elem);
			} else {
				/* This is the only case shared memory may be being used.
				 * If it is the case, it is not applicable to free memory as
				 * we use a preallocated pool (just set element pointer to
				 * NULL).
				 */
				if(!(fifo_ctx->flags& FIFO_PROCESS_SHARED))
					free(fifo_elem_ctx->elem);
			}
			fifo_elem_ctx->elem= NULL;
			fifo_elem_ctx->size= 0;
		}
	}

	/* Reset FIFO level and indexes */
	fifo_ctx->slots_used_cnt= 0;
	fifo_ctx->buf_level= 0;
	fifo_ctx->input_idx= 0;
	fifo_ctx->output_idx= 0;

	pthread_mutex_unlock(&fifo_ctx->api_mutex);
}

/**
 * Internal FIFO closing function. Only parent process (not child) should
 * de-initialize FIFO while closing.
 * @param ref_fifo_ctx
 * @param flag_deinit
 */
static void fifo_close_internal(fifo_ctx_t **ref_fifo_ctx, int flag_deinit)
{
	fifo_ctx_t *fifo_ctx;
	LOG_CTX_INIT(NULL);

	if(ref_fifo_ctx== NULL || (fifo_ctx= *ref_fifo_ctx)== NULL)
		return;

	/* De-initialize FIFO context structure if applicable */
	if(flag_deinit)
		fifo_deinit(fifo_ctx);

	/* Release module instance context structure */
	if((fifo_ctx->flags& FIFO_PROCESS_SHARED)!= 0) {
		size_t fifo_ctx_size= sizeof(fifo_ctx_t)+ fifo_ctx->buf_slots_max*
				(sizeof(fifo_elem_ctx_t)+ fifo_ctx->chunk_size_max);

		/* remove the mapped shared memory segment from the address space */
		ASSERT(munmap(fifo_ctx, fifo_ctx_size)== 0);
	} else {
		free(fifo_ctx);
	}
	*ref_fifo_ctx= NULL;
}

/**
 * Initialize FIFO context structure.
 * @param fifo_ctx
 * @return Status code (refer to 'stat_codes_ctx_t' type).
 */
static int fifo_init(fifo_ctx_t *fifo_ctx, size_t slots_max,
		size_t chunk_size_max, uint32_t flags, const char *fifo_file_name,
		const fifo_elem_alloc_fxn_t *fifo_elem_alloc_fxn)
{
	int ret_code, flag_use_shm, end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(slots_max> 0, return STAT_ERROR);
	// 'chunk_size_max' may be zero
	// 'flags' may take any value
	// 'fifo_file_name' may be NULL
	// 'fifo_elem_alloc_fxn' may be NULL

	flag_use_shm= flags& FIFO_PROCESS_SHARED;

	/* Module flags */
	fifo_ctx->flags= flags;

	/* Exit flag */
	fifo_ctx->flag_exit= 0;

	/* Shared FIFO name */
	if(fifo_file_name!= NULL) {
		size_t file_name_len;
		int printed_size;

		CHECK_DO(flag_use_shm!= 0, goto end);
		CHECK_DO(fifo_elem_alloc_fxn== NULL, goto end);

		file_name_len= strlen(fifo_file_name);
		CHECK_DO(file_name_len> 0 && file_name_len< FIFO_FILE_NAME_MAX_SIZE,
				goto end);

		printed_size= snprintf(fifo_ctx->fifo_file_name,
				FIFO_FILE_NAME_MAX_SIZE, "%s", fifo_file_name);
		CHECK_DO(printed_size==file_name_len, goto end);
	}

	/* FIFO element allocation/release external callback functions */
	if(fifo_elem_alloc_fxn!= NULL) {
		fifo_elem_ctx_dup_fxn_t *elem_ctx_dup=
				fifo_elem_alloc_fxn->elem_ctx_dup;
		fifo_elem_ctx_release_fxn_t *elem_ctx_release=
				fifo_elem_alloc_fxn->elem_ctx_release;

		CHECK_DO(flag_use_shm== 0, goto end);

		if(elem_ctx_dup!= NULL)
			fifo_ctx->elem_ctx_dup= elem_ctx_dup;
		if(elem_ctx_release!= NULL)
			fifo_ctx->elem_ctx_release= elem_ctx_release;
	}

	/* API MUTEX */
	fifo_ctx->flag_api_mutex_initialized= 0; // To close safely on error
	ret_code= fifo_mutex_init(&fifo_ctx->api_mutex, flag_use_shm,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);
	fifo_ctx->flag_api_mutex_initialized= 1;

	/* "Put into buffer" conditional */
	fifo_ctx->flag_buf_put_signal_initialized= 0; // To close safely on error
	ret_code= fifo_cond_init(&fifo_ctx->buf_put_signal, flag_use_shm,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);
	fifo_ctx->flag_buf_put_signal_initialized= 1;

	/* "Get from buffer" conditional */
	fifo_ctx->flag_buf_get_signal_initialized= 0; // To close safely on error
	ret_code= fifo_cond_init(&fifo_ctx->buf_get_signal, flag_use_shm,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);
	fifo_ctx->flag_buf_get_signal_initialized= 1;

	/* Maximum number of element-slots */
	fifo_ctx->buf_slots_max= slots_max;

	/* Maximum permitted size of chunks [bytes] */
	if(flag_use_shm && chunk_size_max== 0) {
		LOGE("A valid maximum chunk size must be provided when opening a "
				"shared-memory FIFO.\n");
		goto end;
	}
	fifo_ctx->chunk_size_max= chunk_size_max;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fifo_deinit(fifo_ctx);
	return end_code;
}

/**
 * De-initialize FIFO context structure.
 * @param fifo_ctx
 */
static void fifo_deinit(fifo_ctx_t *fifo_ctx)
{
	register int i, flag_use_shm, flag_api_mutex_initialized,
		flag_buf_put_signal_initialized, flag_buf_get_signal_initialized;
	size_t buf_slots_max, chunk_size_max;
	LOG_CTX_INIT(NULL);

	if(fifo_ctx== NULL)
		return;

	flag_use_shm= fifo_ctx->flags& FIFO_PROCESS_SHARED;
	buf_slots_max= fifo_ctx->buf_slots_max;
	chunk_size_max= fifo_ctx->chunk_size_max;
	flag_api_mutex_initialized= fifo_ctx->flag_api_mutex_initialized;
	flag_buf_put_signal_initialized= fifo_ctx->flag_buf_put_signal_initialized;
	flag_buf_get_signal_initialized= fifo_ctx->flag_buf_get_signal_initialized;

	/* Set exit flag and send signals to eventually unlock MUTEX */
	fifo_ctx->flag_exit= 1;
	if(flag_api_mutex_initialized!= 0) {
		pthread_mutex_lock(&fifo_ctx->api_mutex);
		if(flag_buf_put_signal_initialized!= 0)
			pthread_cond_broadcast(&fifo_ctx->buf_put_signal);
		if(flag_buf_get_signal_initialized!= 0)
			pthread_cond_broadcast(&fifo_ctx->buf_get_signal);
		pthread_mutex_unlock(&fifo_ctx->api_mutex);
	}

	/* Release FIFO buffer elements if applicable */
	for(i= 0; i< buf_slots_max; i++) {
		fifo_elem_ctx_t *fifo_elem_ctx= (fifo_elem_ctx_t*)(
				(uint8_t*)fifo_ctx->buf+
				i* SIZEOF_FIFO_ELEM_CTX_T(flag_use_shm, chunk_size_max));
		if(fifo_elem_ctx->elem!= NULL) {
			if(fifo_ctx->elem_ctx_release!= NULL) {
				fifo_ctx->elem_ctx_release(&fifo_elem_ctx->elem);
			} else {
				/* This is the only case shared memory may be being used.
				 * If it is the case, it is not applicable to free memory
				 * as we use a preallocated pool (just set element pointer
				 * to NULL).
				 */
				if(!flag_use_shm)
					free(fifo_elem_ctx->elem);
			}
			fifo_elem_ctx->elem= NULL;
			fifo_elem_ctx->size= 0;
		}
	}

	/* Release API MUTEX */
	if(flag_api_mutex_initialized!= 0) {
		ASSERT(pthread_mutex_destroy(&fifo_ctx->api_mutex)== 0);
		fifo_ctx->flag_api_mutex_initialized= 0;
	}

	/* Release conditionals */
	if(flag_buf_put_signal_initialized!= 0) {
		ASSERT(pthread_cond_destroy(&fifo_ctx->buf_put_signal)== 0);
		fifo_ctx->flag_buf_put_signal_initialized= 0;
	}
	if(flag_buf_get_signal_initialized!= 0) {
		ASSERT(pthread_cond_destroy(&fifo_ctx->buf_get_signal)== 0);
		fifo_ctx->flag_buf_get_signal_initialized= 0;
	}

	/* Unlink FIFO file-name if applicable */
	if(flag_use_shm!= 0 && strlen(fifo_ctx->fifo_file_name)> 0) {
		ASSERT(shm_unlink(fifo_ctx->fifo_file_name)== 0);
		memset(fifo_ctx->fifo_file_name, 0, FIFO_FILE_NAME_MAX_SIZE);
	}
}

static inline int fifo_input(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t elem_size, int dup_flag)
{
	int flag_use_shm;
	size_t buf_slots_max, chunk_size_max;
	fifo_elem_ctx_t *fifo_elem_ctx= NULL;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_elem!= NULL && *ref_elem!= NULL, return STAT_ERROR);
	CHECK_DO(elem_size> 0, return STAT_ERROR);
	if((chunk_size_max= fifo_ctx->chunk_size_max)!= 0 &&
			(elem_size> chunk_size_max)) {
		LOGE("Size of element exceed configured maximum chunk size for this "
				"FIFO\n");
		return STAT_ERROR;
	}
	if((dup_flag== 0) && (fifo_ctx->flags& FIFO_PROCESS_SHARED)) {
		// Duplication is mandatory when using shared memory
		LOGE("Cannot put element into shared-memory FIFO without duplication. "
				"Please consider using 'fifo_put_dup()' instead.\n");
		return STAT_ERROR;
	}

	/* Lock API MUTEX */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	flag_use_shm= fifo_ctx->flags& FIFO_PROCESS_SHARED;
	buf_slots_max= fifo_ctx->buf_slots_max;

	/* In the case of blocking FIFO, if buffer is full we block until a
	 * element is consumed and a new free slot is available.
	 * In the case of a non-blocking FIFO, if buffer is full we exit
	 * returning 'STAT_ENOMEM' status.
	 */
	while(fifo_ctx->slots_used_cnt>= buf_slots_max &&
			!(fifo_ctx->flags& FIFO_O_NONBLOCK) &&
			fifo_ctx->flag_exit== 0) {
		pthread_cond_broadcast(&fifo_ctx->buf_put_signal);
		pthread_cond_wait(&fifo_ctx->buf_get_signal, &fifo_ctx->api_mutex);
	}
	if(fifo_ctx->slots_used_cnt>= buf_slots_max &&
			(fifo_ctx->flags& FIFO_O_NONBLOCK)) {
		LOGD("FIFO buffer overflow!\n");
		end_code= STAT_ENOMEM;
		goto end;
	}

	/* Get FIFO slot where to put new element */
	fifo_elem_ctx= (fifo_elem_ctx_t*)((uint8_t*)fifo_ctx->buf+
			fifo_ctx->input_idx*
			SIZEOF_FIFO_ELEM_CTX_T(flag_use_shm, chunk_size_max));
	CHECK_DO(fifo_elem_ctx->elem== NULL, goto end);
	CHECK_DO(fifo_elem_ctx->size== 0, goto end);

	/* Get or copy (if applicable) the new element */
	if(dup_flag!= 0 && fifo_ctx->elem_ctx_dup!= NULL) {
		fifo_elem_ctx->elem= fifo_ctx->elem_ctx_dup(*ref_elem);
		CHECK_DO(fifo_elem_ctx->elem!= NULL, goto end);
	}
	if(dup_flag!= 0 && fifo_ctx->elem_ctx_dup== NULL) {
		/* This is the only case shared memory may be being used.
		 * If it is the case, it is not need to allocate memory as we use a
		 * preallocated pool.
		 */
		fifo_elem_ctx->elem= !(fifo_ctx->flags& FIFO_PROCESS_SHARED)?
				malloc(elem_size): &fifo_elem_ctx->shm_elem_pool[0];
		CHECK_DO(fifo_elem_ctx->elem!= NULL, goto end);
		memcpy(fifo_elem_ctx->elem, *ref_elem, elem_size);
	}
	if(dup_flag== 0) {
		fifo_elem_ctx->elem= *ref_elem;
		*ref_elem= NULL; // we now own the element
	}
	fifo_elem_ctx->size= elem_size;

	/* Update circular buffer management variables */
	fifo_ctx->slots_used_cnt+= 1;
	fifo_ctx->buf_level+= elem_size;
	//CHECK_DO(fifo_ctx->slots_used_cnt<= buf_slots_max,
	//		fifo_ctx->slots_used_cnt= buf_slots_max); //comment-me
	fifo_ctx->input_idx= (fifo_ctx->input_idx+ 1)% buf_slots_max;
	pthread_cond_broadcast(&fifo_ctx->buf_put_signal);

	end_code= STAT_SUCCESS;
end:
	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	return end_code;
}

static inline int fifo_output(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t *ref_elem_size, int flush_flag, int64_t tout_usecs)
{
	int flag_use_shm;
	size_t buf_slots_max, chunk_size_max;
	fifo_elem_ctx_t *fifo_elem_ctx= NULL;
	int ret_code, end_code= STAT_ERROR;
	void *elem= NULL; // Do not release
	ssize_t elem_size= 0;  // Do not release
	void *elem_cpy= NULL;
	struct timespec ts_tout= {0};
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_elem!= NULL, return STAT_ERROR);
	CHECK_DO(ref_elem_size!= NULL, return STAT_ERROR);

	/* Reset arguments to be returned by value */
	*ref_elem= NULL;
	*ref_elem_size= 0;

	/* Lock API MUTEX */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	flag_use_shm= fifo_ctx->flags& FIFO_PROCESS_SHARED;
	buf_slots_max= fifo_ctx->buf_slots_max;
	chunk_size_max= fifo_ctx->chunk_size_max;

	/* Get current time and compute time-out if applicable.
	 * Note that a negative time-out mens 'wait indefinitely'.
	 */
	if(tout_usecs>= 0) {
		struct timespec ts_curr;
		register int64_t curr_nsec;
		/* Get current time */
		CHECK_DO(clock_gettime(CLOCK_MONOTONIC, &ts_curr)== 0, goto end);
	    curr_nsec= (int64_t)ts_curr.tv_sec*1000000000+ (int64_t)ts_curr.tv_nsec;
	    //LOGV("curr_nsec: %"PRId64"\n", curr_nsec); //comment-me
	    //LOGV("secs: %"PRId64"\n", (int64_t)ts_curr.tv_sec); //comment-me
	    //LOGV("nsecs: %"PRId64"\n", (int64_t)ts_curr.tv_nsec); //comment-me
	    /* Compute time-out */
	    curr_nsec+= (tout_usecs* 1000);
	    ts_tout.tv_sec= curr_nsec/ 1000000000;
	    ts_tout.tv_nsec= curr_nsec% 1000000000;
	    curr_nsec= (int64_t)ts_tout.tv_sec*1000000000+
	    		(int64_t)ts_tout.tv_nsec; //comment-me
	    //LOGV("tout_nsec: %"PRId64"\n", curr_nsec); //comment-me
	    //LOGV("secs: %"PRId64"\n", (int64_t)ts_tout.tv_sec); //comment-me
	    //LOGV("nsecs: %"PRId64"\n", (int64_t)ts_tout.tv_nsec); //comment-me
	}

	/* In the case of blocking FIFO, if buffer is empty we block until a
	 * new element is inserted, or if it is the case, time-out occur.
	 * In the case of a non-blocking FIFO, if buffer is empty we exit
	 * returning 'STAT_EAGAIN' status.
	 */
	while(fifo_ctx->slots_used_cnt<= 0 && !(fifo_ctx->flags& FIFO_O_NONBLOCK)
			&& fifo_ctx->flag_exit== 0) {
		LOGD("FIFO buffer underrun!\n");
		pthread_cond_broadcast(&fifo_ctx->buf_get_signal);
		if(tout_usecs>= 0) {
			ret_code= pthread_cond_timedwait(&fifo_ctx->buf_put_signal,
					&fifo_ctx->api_mutex, &ts_tout);
			if(ret_code== ETIMEDOUT) {
				LOGW("Warning: FIFO buffer timed-out!\n");
				end_code= STAT_ETIMEDOUT;
				goto end;
			}
		} else {
			ret_code= pthread_cond_wait(&fifo_ctx->buf_put_signal,
					&fifo_ctx->api_mutex);
			CHECK_DO(ret_code== 0, goto end);
		}
	}
	if(fifo_ctx->slots_used_cnt<= 0 && (fifo_ctx->flags& FIFO_O_NONBLOCK)) {
		LOGD("FIFO buffer underrun!\n");
		end_code= STAT_EAGAIN;
		goto end;
	}

	/* Get the element.
	 * It is important to note that in a for-exec setting, value of
	 * 'fifo_elem_ctx->elem' can not be used as same shared memory will have
	 * different pointers values (because of different virtual memory maps).
	 */
	fifo_elem_ctx= (fifo_elem_ctx_t*)((uint8_t*)fifo_ctx->buf+
			fifo_ctx->output_idx*
			SIZEOF_FIFO_ELEM_CTX_T(flag_use_shm, chunk_size_max));
	elem= !(fifo_ctx->flags& FIFO_PROCESS_SHARED)? fifo_elem_ctx->elem:
			&fifo_elem_ctx->shm_elem_pool[0];
	elem_size= fifo_elem_ctx->size;
	CHECK_DO(elem!= NULL && elem_size> 0, goto end);

	/* Flush element from FIFO if required
	 * (Update circular buffer management variables).
	 */
	if(flush_flag) {
		fifo_elem_ctx->elem= NULL;
		fifo_elem_ctx->size= 0;
		fifo_ctx->slots_used_cnt-= 1;
		fifo_ctx->buf_level-= elem_size;
		fifo_ctx->output_idx= (fifo_ctx->output_idx+ 1)% buf_slots_max;
		pthread_cond_broadcast(&fifo_ctx->buf_get_signal);
	}

	/* Set the element references to return.
	 * In the special case we work with shared memory, we must return a copy.
	 */
	if(!(fifo_ctx->flags& FIFO_PROCESS_SHARED)) {
		*ref_elem= elem; // directly return the pointer
	} else {
		elem_cpy= malloc(elem_size);
		CHECK_DO(elem_cpy!= NULL, goto end);
		memcpy(elem_cpy, elem, elem_size);
		*ref_elem= elem_cpy; // return a copy
		elem_cpy= NULL; // Avoid double referencing
	}
	*ref_elem_size= (size_t)elem_size;

	end_code= STAT_SUCCESS;
end:
	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	if(elem_cpy!= NULL)
		free(elem_cpy);
	return end_code;
}

static int fifo_mutex_init(pthread_mutex_t * const pthread_mutex_p,
		int flag_use_shm, log_ctx_t *log_ctx)
{
	pthread_mutexattr_t attr, *attr_p= NULL;
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(log_ctx);

	/* Initialize */
	if(flag_use_shm!= 0) {
		pthread_mutexattr_init(&attr);
		ret_code= pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		CHECK_DO(ret_code== 0, goto end);
		attr_p= &attr; // update pointer
	}
	ret_code= pthread_mutex_init(pthread_mutex_p, attr_p);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS) {
		ASSERT(pthread_mutex_destroy(pthread_mutex_p)== 0);
	}
	return end_code;
}

static int fifo_cond_init(pthread_cond_t * const pthread_cond_p,
		int flag_use_shm, log_ctx_t *log_ctx)
{
	pthread_condattr_t attr;
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Initialize */
	pthread_condattr_init(&attr);
	ret_code= pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	CHECK_DO(ret_code== 0, goto end);

	if(flag_use_shm!= 0) {
		ret_code= pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		CHECK_DO(ret_code== 0, goto end);
	}

	ret_code= pthread_cond_init(pthread_cond_p, &attr);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS) {
		ASSERT(pthread_cond_destroy(pthread_cond_p)== 0);
	}
	return end_code;
}
