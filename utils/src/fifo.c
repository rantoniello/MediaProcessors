/*
 * Copyright (c) 2017 Rafael Antoniello
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

#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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
	 */
	volatile uint32_t flags;
	/**
	 * Exit flag: if set to non-zero value, FIFO module should finish/unblock
	 * transactions as fast as possible
	 */
	volatile int flag_exit;
	/**
	 * FIFO buffer maximum size.
	 */
	uint32_t buf_max_size;
	/**
	 * FIFO buffer.
	 * This is a circular buffer of chunks of data.
	 * Instead of managing a single pool of data, the buffer stores pointers to
	 * a fixed number of chunk objects, each one holding the reference and the
	 * size of each chunk.
	 */
	fifo_elem_ctx_t *buf;
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
	pthread_mutex_t api_mutex;
	/**
	 * Signals each time a new chunk enters the FIFO buffer.
	 */
	pthread_cond_t buf_put_signal;
	/**
	 * Signals each time a new chunk is consumed from the FIFO buffer.
	 */
	pthread_cond_t buf_get_signal;
	/**
	 * Externally defined duplication and releasing functions.
	 */
	FIFO_ELEM_CTX_DUP;
	FIFO_ELEM_CTX_RELEASE;
} fifo_ctx_t;

/* **** Prototypes **** */

static inline int fifo_input(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t elem_size, int dup_flag);
static inline int fifo_output(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t *ref_elem_size, int flush_flag);

/* **** Implementations **** */

fifo_ctx_t* fifo_open(uint32_t buf_max_size, uint32_t flags,
		const fifo_elem_alloc_fxn_t *fifo_elem_alloc_fxn)
{
	fifo_ctx_t *fifo_ctx= NULL;
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(buf_max_size> 0, return NULL);

	/* Allocate FIFO context structure */
	fifo_ctx= (fifo_ctx_t*)calloc(1, sizeof(fifo_ctx_t));
	CHECK_DO(fifo_ctx!= NULL, goto end);

	/* Initialize context structure */
	fifo_ctx->flags= flags;
	fifo_ctx->flag_exit= 0;
	fifo_ctx->buf_max_size= buf_max_size;
	fifo_ctx->buf= (fifo_elem_ctx_t*)calloc(buf_max_size, sizeof(
			fifo_elem_ctx_t));
	CHECK_DO(fifo_ctx->buf!= NULL, goto end);
	ret_code= pthread_mutex_init(&fifo_ctx->api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);
	ret_code= pthread_cond_init(&fifo_ctx->buf_put_signal, NULL);
	CHECK_DO(ret_code== 0, goto end);
	ret_code= pthread_cond_init(&fifo_ctx->buf_get_signal, NULL);
	CHECK_DO(ret_code== 0, goto end);
	if(fifo_elem_alloc_fxn!= NULL) {
		fifo_ctx->elem_ctx_dup= fifo_elem_alloc_fxn->elem_ctx_dup;
		fifo_ctx->elem_ctx_release= fifo_elem_alloc_fxn->elem_ctx_release;
	}

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fifo_close(&fifo_ctx);
	return fifo_ctx;
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
}

void fifo_close(fifo_ctx_t **pfifo_ctx)
{
	fifo_ctx_t *fifo_ctx;

	if(pfifo_ctx== NULL)
		return;

	if((fifo_ctx= *pfifo_ctx)!= NULL) {

		/* Set exit flag and send signals to eventually unlock mutex */
		fifo_ctx->flag_exit= 1;
		pthread_mutex_lock(&fifo_ctx->api_mutex);
    	pthread_cond_broadcast(&fifo_ctx->buf_put_signal);
    	pthread_cond_broadcast(&fifo_ctx->buf_get_signal);
    	pthread_mutex_unlock(&fifo_ctx->api_mutex);

		/* Release FIFO buffer */
		if(fifo_ctx->buf!= NULL) {
			/* Check and release each element of buffer */
			int i;
			for(i= 0; i< fifo_ctx->buf_max_size; i++) {
				fifo_elem_ctx_t *fifo_elem_ctx= &fifo_ctx->buf[i];
				if(fifo_elem_ctx->elem!= NULL) {
					if(fifo_ctx->elem_ctx_release!= NULL) {
						fifo_ctx->elem_ctx_release(&fifo_elem_ctx->elem);
					} else {
						free(fifo_elem_ctx->elem);
					}
					fifo_elem_ctx->elem= NULL;
					fifo_elem_ctx->size= 0;
				}
			}
			free(fifo_ctx->buf);
			fifo_ctx->buf= NULL;
		}

		/* Release mutex and conditionals */
		pthread_mutex_destroy(&fifo_ctx->api_mutex);
		pthread_cond_destroy(&fifo_ctx->buf_put_signal);
		pthread_cond_destroy(&fifo_ctx->buf_get_signal);

		/* Release module instance context structure */
		free(fifo_ctx);
		*pfifo_ctx= NULL;
	}
}

int fifo_put_dup(fifo_ctx_t *fifo_ctx, const void *elem, size_t elem_size)
{
	void *p= (void*)elem;
	return fifo_input(fifo_ctx, &p, elem_size, 1);
}

int fifo_put(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t elem_size)
{
	return fifo_input(fifo_ctx, ref_elem, elem_size, 0);
}

int fifo_get(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size)
{
	return fifo_output(fifo_ctx, ref_elem, ref_elem_size, 1);
}

int fifo_show(fifo_ctx_t *fifo_ctx, void **elem, size_t *elem_size)
{
	return fifo_output(fifo_ctx, elem, elem_size, 0);
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
	ssize_t buf_level;
	int i, cnt, cnt_max, flag_break;
	uint32_t buf_max_size;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(elem_cnt> 0 || elem_cnt== -1, return STAT_ERROR);
	CHECK_DO(it_fxn!= NULL, return STAT_ERROR);

	/* Lock API MUTEX */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	/* Iterate: we do it beginning with the input index (namely, we go from
	 * the newest queued element to the oldest).
	 */
	buf_level= fifo_ctx->buf_level;
	if(elem_cnt== -1)
		elem_cnt= buf_level; // '-1' means "traverse all the FIFO"
	cnt_max= (elem_cnt< buf_level)? elem_cnt: buf_level;
	buf_max_size= fifo_ctx->buf_max_size;
	flag_break= 0;
	for(i= fifo_ctx->input_idx- 1, cnt= 0; cnt< cnt_max; cnt++) {
		fifo_elem_ctx_t fifo_elem_ctx= fifo_ctx->buf[i];

		/* Execute iterator callback function */
		it_fxn(fifo_elem_ctx.elem, fifo_elem_ctx.size, i, it_arg, &flag_break);
		if(flag_break!= 0)
			break;

		/* Update for next iteration; note that 'buf_max_size' is > 0 in
		 * modulo operation:
		 * integer r = a % b;
		 * r= r < 0 ? r + b : r; <- Only works if B> 0
		 */
		i= (i- 1)% buf_max_size;
		if(i< 0)
			i= i+ buf_max_size;
	}

	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	return STAT_SUCCESS;
}

void fifo_empty(fifo_ctx_t *fifo_ctx)
{
	int i;
	LOG_CTX_INIT(NULL);

	CHECK_DO(fifo_ctx!= NULL, return);

	/* Lock API mutex */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	/* Release all the elements available in FIFO buffer */
	for(i= 0; i< fifo_ctx->buf_max_size; i++) {
		fifo_elem_ctx_t *fifo_elem_ctx= &fifo_ctx->buf[i];
		if(fifo_elem_ctx->elem!= NULL) {
			if(fifo_ctx->elem_ctx_release!= NULL) {
				fifo_ctx->elem_ctx_release(&fifo_elem_ctx->elem);
			} else {
				free(fifo_elem_ctx->elem);
			}
			fifo_elem_ctx->elem= NULL;
			fifo_elem_ctx->size= 0;
		}
	}

	/* Reset FIFO level and indexes */
	fifo_ctx->buf_level= 0;
	fifo_ctx->input_idx= 0;
	fifo_ctx->output_idx= 0;

	pthread_mutex_unlock(&fifo_ctx->api_mutex);
}

static inline int fifo_input(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t elem_size, int dup_flag)
{
	uint32_t buf_max_size;
	fifo_elem_ctx_t *fifo_elem_ctx= NULL;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_elem!= NULL && *ref_elem!= NULL, return STAT_ERROR);
	CHECK_DO(elem_size> 0, return STAT_ERROR);

	/* Lock API mutex */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	buf_max_size= fifo_ctx->buf_max_size;

	/* Block until we have space in buffer to put new element */
	while(fifo_ctx->buf_level>= buf_max_size &&
			!(fifo_ctx->flags& FIFO_O_NONBLOCK) &&
			fifo_ctx->flag_exit== 0) {
		pthread_cond_broadcast(&fifo_ctx->buf_put_signal);
		pthread_cond_wait(&fifo_ctx->buf_get_signal, &fifo_ctx->api_mutex);
	}
	/* If 'FIFO_O_NONBLOCK' flag is set, check if buffer has available space
	 * before proceeding.
	 */
	if(fifo_ctx->buf_level>= buf_max_size &&
			(fifo_ctx->flags& FIFO_O_NONBLOCK)) {
		//LOGV("FIFO buffer overflow!\n"); //Comment-me
		end_code= STAT_ENOMEM;
		goto end;
	}

	/* Allocate, copy and insert element */


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
		fifo_elem_ctx->elem= malloc(elem_size);
		CHECK_DO(fifo_elem_ctx->elem!= NULL, goto end);
		memcpy(fifo_elem_ctx->elem, *ref_elem, elem_size);
	}
	if(dup_flag== 0) {
		fifo_elem_ctx->elem= *ref_elem;
		*ref_elem= NULL; // we now own the element
	}
	fifo_elem_ctx->size= elem_size;

	/* Update circular buffer management variables */
	fifo_ctx->buf_level+= 1;
	//CHECK_DO(fifo_ctx->buf_level<= buf_max_size,
	//		fifo_ctx->buf_level= buf_max_size); //comment-me
	fifo_ctx->input_idx= (fifo_ctx->input_idx+ 1)% buf_max_size;
	pthread_cond_broadcast(&fifo_ctx->buf_put_signal);

	end_code= STAT_SUCCESS;
end:
	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	return end_code;
}

static inline int fifo_output(fifo_ctx_t *fifo_ctx, void **ref_elem,
		size_t *ref_elem_size, int flush_flag)
{
	fifo_elem_ctx_t *fifo_elem_ctx= NULL;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_elem!= NULL, return STAT_ERROR);
	CHECK_DO(ref_elem_size!= NULL, return STAT_ERROR);

	/* Lock API mutex */
	pthread_mutex_lock(&fifo_ctx->api_mutex);

	/* Reset arguments to be returned by value */
	*ref_elem= NULL;
	*ref_elem_size= 0;

	//CHECK_DO(fifo_ctx->buf_level<= fifo_ctx->buf_max_size,
	//		goto end); //comment-me

	/* If buffer is empty, block until a new element is inserted */
	while(fifo_ctx->buf_level<= 0 && !(fifo_ctx->flags & FIFO_O_NONBLOCK)
			&& fifo_ctx->flag_exit== 0) {
		//LOGV("FIFO buffer underun!\n"); //comment-me
		pthread_cond_broadcast(&fifo_ctx->buf_get_signal);
		pthread_cond_wait(&fifo_ctx->buf_put_signal, &fifo_ctx->api_mutex);
	}
	/* If 'FIFO_O_NONBLOCK' flag is set, check if buffer is empty before
	 * proceeding.
	 */
	if(fifo_ctx->buf_level<= 0 && (fifo_ctx->flags & FIFO_O_NONBLOCK)) {
		//LOGV("FIFO buffer underun!\n"); //comment-me
		end_code= STAT_EAGAIN;
		goto end;
	}

	/* Get element */
	fifo_elem_ctx= &fifo_ctx->buf[fifo_ctx->output_idx];
	*ref_elem= fifo_elem_ctx->elem;
	*ref_elem_size= fifo_elem_ctx->size;

	/* If applicable, flush (release) element from FIFO */
	if(flush_flag) {
		fifo_elem_ctx->elem= NULL;
		fifo_elem_ctx->size= 0;

		/* Update circular buffer management variables */
		fifo_ctx->buf_level-= 1;
		//CHECK_DO(fifo_ctx->buf_level>= 0,
		//	fifo_ctx->buf_level= 0); //comment-me
		fifo_ctx->output_idx= (fifo_ctx->output_idx+ 1) %fifo_ctx->buf_max_size;
		pthread_cond_broadcast(&fifo_ctx->buf_get_signal);
	}

	end_code= STAT_SUCCESS;
end:
	pthread_mutex_unlock(&fifo_ctx->api_mutex);
	return end_code;
}
