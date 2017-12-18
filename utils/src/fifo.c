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
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>

#include "check_utils.h"
#include "log.h"
#include "stat_codes.h"

/* **** Definitions **** */

typedef struct fifo_elem_ctx_s {
	void *elem;
	ssize_t size;
} fifo_elem_ctx_t;

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
	 * Maximum number of element-slots (namely, maximum number of possible
	 * chunks) of the FIFO buffer.
	 */
	size_t buf_slots_max;
	/**
	 * Maximum permitted size of chunks [bytes].
	 * In the case of shared memory, this value must be set greater than zero.
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
	fifo_elem_ctx_t *buf;
	/**
	 * Preallocated chunk-buffer pool.
	 * This is only used with shared memory.
	 */
	void *shm_buf_pool;
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
	 * Module API mutex.
	 */
	pthread_mutex_t *api_mutex_p;
	/**
	 * Signals each time a new chunk enters the FIFO buffer.
	 */
	pthread_cond_t *buf_put_signal_p;
	/**
	 * Signals each time a new chunk is consumed from the FIFO buffer.
	 */
	pthread_cond_t *buf_get_signal_p;
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
} fifo_ctx_t;

/* **** Prototypes **** */

static inline int fifo_input(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t elem_size, int dup_flag);
static inline int fifo_output(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t *ref_elem_size, int flush_flag, int64_t tout_usecs);

static void* fifo_malloc(int flag_use_shm, size_t size);
static void fifo_free(int flag_use_shm, void **ref_p, size_t size);

static pthread_mutex_t* fifo_mutex_create(int flag_use_shm);
static void fifo_mutex_destroy(int flag_use_shm,
		pthread_mutex_t **ref_pthread_mutex);

static pthread_cond_t* fifo_cond_create(int flag_use_shm);
static void fifo_cond_destroy(int flag_use_shm,
		pthread_cond_t **ref_pthread_cond);

/* **** Implementations **** */

fifo_ctx_t* fifo_open(size_t buf_slots_max, size_t chunk_size_max,
		uint32_t flags, const fifo_elem_alloc_fxn_t *fifo_elem_alloc_fxn)
{
	fifo_ctx_t *fifo_ctx= NULL;
	int flag_use_shm, end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(buf_slots_max> 0, return NULL);

	flag_use_shm= flags& FIFO_PROCESS_SHARED;

	/* Allocate FIFO context structure */
	fifo_ctx= (fifo_ctx_t*)fifo_malloc(flag_use_shm, sizeof(fifo_ctx_t));
	CHECK_DO(fifo_ctx!= NULL, goto end);

	/* **** Initialize context structure **** */

	fifo_ctx->flags= flags;

	fifo_ctx->flag_exit= 0;

	fifo_ctx->buf_slots_max= buf_slots_max;

	if(flag_use_shm && chunk_size_max== 0) {
		LOGE("A valid maximum chunk size must be provided when opening a "
				"shared-memory FIFO.\n");
		goto end;
	}
	fifo_ctx->chunk_size_max= chunk_size_max;

	/* Allocate circular pointer-buffer */
	fifo_ctx->buf= (fifo_elem_ctx_t*)fifo_malloc(flag_use_shm, buf_slots_max*
			sizeof(fifo_elem_ctx_t));
	CHECK_DO(fifo_ctx->buf!= NULL, goto end);

	/* Allocate shared memory pool if applicable */
	if(flag_use_shm) {
		fifo_ctx->shm_buf_pool= fifo_malloc(flag_use_shm, buf_slots_max*
				chunk_size_max);
		CHECK_DO(fifo_ctx->shm_buf_pool!= NULL, goto end);
	}

	/* API MUTEX */
	fifo_ctx->api_mutex_p= fifo_mutex_create(flag_use_shm);
	CHECK_DO(fifo_ctx->api_mutex_p!= NULL, goto end);

	/* Put into buffer conditional */
	fifo_ctx->buf_put_signal_p= fifo_cond_create(flag_use_shm);
	CHECK_DO(fifo_ctx->buf_put_signal_p!= NULL, goto end);

	/* Get from buffer conditional */
	fifo_ctx->buf_get_signal_p= fifo_cond_create(flag_use_shm);
	CHECK_DO(fifo_ctx->buf_get_signal_p!= NULL, goto end);

	if(fifo_elem_alloc_fxn!= NULL) {
		if(flag_use_shm) {
			LOGE("Cannot use external duplication callback when using "
					"shared-memory FIFO.\n");
			goto end;
		}
		fifo_ctx->elem_ctx_dup= fifo_elem_alloc_fxn->elem_ctx_dup;
		fifo_ctx->elem_ctx_release= fifo_elem_alloc_fxn->elem_ctx_release;
	}

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fifo_close(&fifo_ctx);
	return fifo_ctx;
}

void fifo_close(fifo_ctx_t **ref_fifo_ctx)
{
	int flag_use_shm;
	fifo_ctx_t *fifo_ctx;
	//LOG_CTX_INIT(NULL);

	if(ref_fifo_ctx== NULL || (fifo_ctx= *ref_fifo_ctx)== NULL)
		return;

	flag_use_shm= fifo_ctx->flags& FIFO_PROCESS_SHARED;

	/* Set exit flag and send signals to eventually unlock MUTEX */
	fifo_ctx->flag_exit= 1;
	if(fifo_ctx->api_mutex_p!= NULL && fifo_ctx->buf_put_signal_p!= NULL &&
			fifo_ctx->buf_get_signal_p!= NULL) {
		pthread_mutex_lock(fifo_ctx->api_mutex_p);
		pthread_cond_broadcast(fifo_ctx->buf_put_signal_p);
		pthread_cond_broadcast(fifo_ctx->buf_get_signal_p);
		pthread_mutex_unlock(fifo_ctx->api_mutex_p);
	}

	/* Release FIFO buffer */
	if(fifo_ctx->buf!= NULL) {
		int i;

		/* Check and release each element of buffer */
		for(i= 0; i< fifo_ctx->buf_slots_max; i++) {
			fifo_elem_ctx_t *fifo_elem_ctx= &fifo_ctx->buf[i];
			if(fifo_elem_ctx->elem!= NULL) {
				if(fifo_ctx->elem_ctx_release!= NULL) {
					fifo_ctx->elem_ctx_release(&fifo_elem_ctx->elem);
				} else {
					/* This is the only case shared memory may be being used.
					 * If it is the case, it is not applicable to free memory as
					 * we use a preallocated pool (just set element pointer to
					 * NULL).
					 */
					if(!flag_use_shm)
						free(fifo_elem_ctx->elem);
				}
				fifo_elem_ctx->elem= NULL;
				fifo_elem_ctx->size= 0;
			}
		}
		fifo_free(flag_use_shm, (void**)&fifo_ctx->buf,
				(size_t)fifo_ctx->buf_slots_max* sizeof(fifo_elem_ctx_t));
		//fifo_ctx->buf= NULL; // redundant
	}

	/* Release shared memory pool if applicable */
	if(flag_use_shm) {
		fifo_free(flag_use_shm, &fifo_ctx->shm_buf_pool,
				fifo_ctx->buf_slots_max* fifo_ctx->chunk_size_max);
	}

	/* Release API MUTEX */
	fifo_mutex_destroy(flag_use_shm, &fifo_ctx->api_mutex_p);

	/* Release conditionals */
	fifo_cond_destroy(flag_use_shm, &fifo_ctx->buf_put_signal_p);
	fifo_cond_destroy(flag_use_shm, &fifo_ctx->buf_get_signal_p);

	/* Release module instance context structure */
	fifo_free(flag_use_shm, (void**)ref_fifo_ctx, sizeof(fifo_ctx_t));
	//*ref_fifo_ctx= NULL; // redundant
}

void fifo_set_blocking_mode(fifo_ctx_t *fifo_ctx, int do_block)
{
	LOG_CTX_INIT(NULL);

	CHECK_DO(fifo_ctx!= NULL, return);

	pthread_mutex_lock(fifo_ctx->api_mutex_p);

	/* Set the 'non-blocking' bit-flag */
	if(do_block!= 0) {
		fifo_ctx->flags&= ~((uint32_t)FIFO_O_NONBLOCK);
	} else {
		fifo_ctx->flags|= (uint32_t)FIFO_O_NONBLOCK;
	}

	/* Announce to unblock conditional waits */
	pthread_cond_broadcast(fifo_ctx->buf_put_signal_p);
	pthread_cond_broadcast(fifo_ctx->buf_get_signal_p);

	pthread_mutex_unlock(fifo_ctx->api_mutex_p);
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

	pthread_mutex_lock(fifo_ctx->api_mutex_p);
	buf_level= fifo_ctx->buf_level;
	pthread_mutex_unlock(fifo_ctx->api_mutex_p);

	return buf_level;
}

int fifo_traverse(fifo_ctx_t *fifo_ctx, int elem_cnt,
		void (*it_fxn)(void *elem, ssize_t elem_size, int idx, void *it_arg,
				int *ref_flag_break),
		void *it_arg)
{
	ssize_t slots_used_cnt;
	int i, cnt, cnt_max, flag_break;
	size_t buf_slots_max;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(elem_cnt> 0 || elem_cnt== -1, return STAT_ERROR);
	CHECK_DO(it_fxn!= NULL, return STAT_ERROR);

	/* Lock API MUTEX */
	pthread_mutex_lock(fifo_ctx->api_mutex_p);

	/* Iterate: we do it beginning with the input index (namely, we go from
	 * the newest queued element to the oldest).
	 */
	slots_used_cnt= fifo_ctx->slots_used_cnt;
	if(elem_cnt== -1)
		elem_cnt= slots_used_cnt; // '-1' means "traverse all the FIFO"
	cnt_max= (elem_cnt< slots_used_cnt)? elem_cnt: slots_used_cnt;
	buf_slots_max= fifo_ctx->buf_slots_max;
	flag_break= 0;
	for(i= fifo_ctx->input_idx- 1, cnt= 0; cnt< cnt_max; cnt++) {
		fifo_elem_ctx_t fifo_elem_ctx= fifo_ctx->buf[i];

		/* Execute iterator callback function */
		it_fxn(fifo_elem_ctx.elem, fifo_elem_ctx.size, i, it_arg, &flag_break);
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

	pthread_mutex_unlock(fifo_ctx->api_mutex_p);
	return STAT_SUCCESS;
}

void fifo_empty(fifo_ctx_t *fifo_ctx)
{
	int i;
	LOG_CTX_INIT(NULL);

	CHECK_DO(fifo_ctx!= NULL, return);

	/* Lock API mutex */
	pthread_mutex_lock(fifo_ctx->api_mutex_p);

	/* Release all the elements available in FIFO buffer */
	for(i= 0; i< fifo_ctx->buf_slots_max; i++) {
		fifo_elem_ctx_t *fifo_elem_ctx= &fifo_ctx->buf[i];
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

	pthread_mutex_unlock(fifo_ctx->api_mutex_p);
}

static inline int fifo_input(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t elem_size, int dup_flag)
{
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
	pthread_mutex_lock(fifo_ctx->api_mutex_p);

	buf_slots_max= fifo_ctx->buf_slots_max;

	/* In the case of blocking FIFO, if buffer is full we block until a
	 * element is consumed and a new free slot is available.
	 * In the case of a non-blocking FIFO, if buffer is full we exit
	 * returning 'STAT_ENOMEM' status.
	 */
	while(fifo_ctx->slots_used_cnt>= buf_slots_max &&
			!(fifo_ctx->flags& FIFO_O_NONBLOCK) &&
			fifo_ctx->flag_exit== 0) {
		pthread_cond_broadcast(fifo_ctx->buf_put_signal_p);
		pthread_cond_wait(fifo_ctx->buf_get_signal_p, fifo_ctx->api_mutex_p);
	}
	if(fifo_ctx->slots_used_cnt>= buf_slots_max &&
			(fifo_ctx->flags& FIFO_O_NONBLOCK)) {
		//LOGV("FIFO buffer overflow!\n"); //Comment-me
		end_code= STAT_ENOMEM;
		goto end;
	}

	/* Get FIFO slot where to put new element */
	fifo_elem_ctx= &fifo_ctx->buf[fifo_ctx->input_idx];
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
		if(!(fifo_ctx->flags& FIFO_PROCESS_SHARED)) {
			fifo_elem_ctx->elem= malloc(elem_size);
		} else {
			fifo_elem_ctx->elem= &((uint8_t*)fifo_ctx->shm_buf_pool)
					[fifo_ctx->input_idx* chunk_size_max];
		}
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
	pthread_cond_broadcast(fifo_ctx->buf_put_signal_p);

	end_code= STAT_SUCCESS;
end:
	pthread_mutex_unlock(fifo_ctx->api_mutex_p);
	return end_code;
}

static inline int fifo_output(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t *ref_elem_size, int flush_flag, int64_t tout_usecs)
{
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
	pthread_mutex_lock(fifo_ctx->api_mutex_p);

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
		//LOGV("FIFO buffer underrun!\n"); //comment-me
		pthread_cond_broadcast(fifo_ctx->buf_get_signal_p);
		if(tout_usecs>= 0) {
			ret_code= pthread_cond_timedwait(fifo_ctx->buf_put_signal_p,
					fifo_ctx->api_mutex_p, &ts_tout);
			if(ret_code== ETIMEDOUT) {
				LOGW("Warning: FIFO buffer timed-out!\n");
				end_code= STAT_ETIMEDOUT;
				goto end;
			}
		} else {
			ret_code= pthread_cond_wait(fifo_ctx->buf_put_signal_p,
					fifo_ctx->api_mutex_p);
			CHECK_DO(ret_code== 0, goto end);
		}
	}
	if(fifo_ctx->slots_used_cnt<= 0 && (fifo_ctx->flags& FIFO_O_NONBLOCK)) {
		//LOGV("FIFO buffer underrun!\n"); //comment-me
		end_code= STAT_EAGAIN;
		goto end;
	}

	/* Get the element */
	fifo_elem_ctx= &fifo_ctx->buf[fifo_ctx->output_idx];
	elem= fifo_elem_ctx->elem;
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
		fifo_ctx->output_idx= (fifo_ctx->output_idx+ 1)%
				fifo_ctx->buf_slots_max;
		pthread_cond_broadcast(fifo_ctx->buf_get_signal_p);
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
	pthread_mutex_unlock(fifo_ctx->api_mutex_p);
	if(elem_cpy!= NULL)
		free(elem_cpy);
	return end_code;
}

static void* fifo_malloc(int flag_use_shm, size_t size)
{
	void *p= NULL;

	if(flag_use_shm!= 0) {
		p= mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS,
				0, 0);
		if(p== MAP_FAILED)
			p= NULL;
	} else {
		p= malloc(size);
	}
	if(p!= NULL)
		memset(p, 0, size);
	return p;
}

static void fifo_free(int flag_use_shm, void **ref_p, size_t size)
{
	void *p= NULL;
	LOG_CTX_INIT(NULL);

	if(ref_p== NULL || (p= *ref_p)== NULL)
		return;

	if(flag_use_shm!= 0) {
		ASSERT(munmap(p, size)== 0);
	} else {
		free(p);
	}
	*ref_p= NULL;
}

static pthread_mutex_t* fifo_mutex_create(int flag_use_shm)
{
	pthread_mutexattr_t attr, *attr_p= NULL;
	int ret_code, end_code= STAT_ERROR;
	pthread_mutex_t *pthread_mutex= NULL;
	LOG_CTX_INIT(NULL);

	pthread_mutex= (pthread_mutex_t*)fifo_malloc(flag_use_shm,
			sizeof(pthread_mutex_t));
	CHECK_DO(pthread_mutex!= NULL, goto end);

	/* Initialize */
	if(flag_use_shm!= 0) {
		pthread_mutexattr_init(&attr);
		ret_code= pthread_mutexattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);
		CHECK_DO(ret_code== 0, goto end);
		attr_p= &attr; // update pointer
	}
	ret_code= pthread_mutex_init(pthread_mutex, attr_p);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fifo_mutex_destroy(flag_use_shm, &pthread_mutex);
	return pthread_mutex;
}

static void fifo_mutex_destroy(int flag_use_shm,
		pthread_mutex_t **ref_pthread_mutex)
{
	pthread_mutex_t *pthread_mutex= NULL;
	LOG_CTX_INIT(NULL);

	if(ref_pthread_mutex== NULL || (pthread_mutex= *ref_pthread_mutex)== NULL)
		return;

	/* Release mutex */
	ASSERT(pthread_mutex_destroy(pthread_mutex)== 0);

	/* Free memory */
	fifo_free(flag_use_shm, (void**)ref_pthread_mutex, sizeof(pthread_mutex_t));
}

static pthread_cond_t* fifo_cond_create(int flag_use_shm)
{
	pthread_condattr_t attr;
	int ret_code, end_code= STAT_ERROR;
	pthread_cond_t *pthread_cond= NULL;
	LOG_CTX_INIT(NULL);

	pthread_cond= (pthread_cond_t*)fifo_malloc(flag_use_shm,
			sizeof(pthread_cond_t));
	CHECK_DO(pthread_cond!= NULL, goto end);

	/* Initialize */
	pthread_condattr_init(&attr);
	ret_code= pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	CHECK_DO(ret_code== 0, goto end);

	if(flag_use_shm!= 0) {
		ret_code= pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		CHECK_DO(ret_code== 0, goto end);
	}

	ret_code= pthread_cond_init(pthread_cond, &attr);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fifo_cond_destroy(flag_use_shm, &pthread_cond);
	return pthread_cond;
}

static void fifo_cond_destroy(int flag_use_shm,
		pthread_cond_t **ref_pthread_cond)
{
	pthread_cond_t *pthread_cond= NULL;
	LOG_CTX_INIT(NULL);

	if(ref_pthread_cond== NULL || (pthread_cond= *ref_pthread_cond)== NULL)
		return;

	/* Release conditional */
	ASSERT(pthread_cond_destroy(pthread_cond)== 0);

	/* Free memory */
	fifo_free(flag_use_shm, (void**)ref_pthread_cond, sizeof(pthread_cond_t));
}
