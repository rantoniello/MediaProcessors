/*
 * Copyright (c) 2017, 2018, 2019, 2020 Rafael Antoniello
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
 * @file fifo.h
 * @brief Simple queue (FIFO) implementation
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_FIFO_H_
#define UTILS_SRC_FIFO_H_

#include <sys/types.h>
#include <inttypes.h>

/* **** Definitions **** */

/**
 * FLag to indicate this FIFO is non-blocking. FIFO is blocking by default
 * (flag not set), which means that a 'fifo_put' operation will block until an
 * empty slot is available and a 'fifo_get' will block while FIFO is empty.
 */
#define FIFO_O_NONBLOCK 	1
/**
 * FLag to indicate this FIFO is to be defined in shared memory.
 * In this case, the FIFO is thought to be used as an interprocess
 * communication mechanism.
 */
#define FIFO_PROCESS_SHARED 2

/* Forward definitions */
typedef struct fifo_ctx_s fifo_ctx_t;
typedef struct log_ctx_s log_ctx_t;

typedef void* (fifo_elem_ctx_memcpy_fxn_t)(void *opaque, void *dest,
		const void *src, size_t size, log_ctx_t *log_ctx);
typedef int (fifo_elem_ctx_dequeue_fxn_t)(void *opaque, void **ref_elem,
		size_t *ref_elem_size, const void *src, size_t size,
		log_ctx_t *log_ctx);

/**
 * Extended parameters of function 'fifo_open()'.
 */
typedef struct fifo_extend_param_ctx_s {
	/**
	 * Opaque externally defined function to be used to copy data when queuing
	 * a new element in the FIFO.
	 */
	fifo_elem_ctx_memcpy_fxn_t *elem_ctx_memcpy;
	/**
	 * Opaque externally defined function to be used to dequeue data out of
	 * the FIFO.
	 */
	fifo_elem_ctx_dequeue_fxn_t *elem_ctx_dequeue;
	/**
	 * Opaque user-defined data passed to 'fifo_elem_ctx_memcpy_fxn_t' and
	 * 'fifo_elem_ctx_dequeue_fxn_t'.
	 */
	void *opaque;
	/**
	 * File name associated to the shared memory.
	 * Only used in the case SHM flag is set.
	 */
	const char *shm_fifo_name;
} fifo_extend_param_ctx_t;

/* **** Prototypes **** */

/**
 * //TODO
 */
fifo_ctx_t* fifo_open(const size_t pool_size, const uint32_t flags,
		const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
		log_ctx_t *log_ctx);

/**
 * //TODO
 */
fifo_ctx_t* fifo_open_shm(const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
		log_ctx_t *log_ctx);

/**
 * //TODO
 */
void fifo_close(fifo_ctx_t **ref_fifo_ctx, log_ctx_t *log_ctx);

/**
 * //TODO
 */
void fifo_close_shm(fifo_ctx_t **ref_fifo_ctx, log_ctx_t *log_ctx);

/**
 * //TODO
 */
void fifo_set_blocking_mode(fifo_ctx_t *fifo_ctx, const int do_block,
		log_ctx_t *log_ctx);

/**
 * //TODO
 */
int fifo_push(fifo_ctx_t *fifo_ctx, void **ref_elem, const size_t elem_size,
		log_ctx_t *log_ctx);

/**
 * //TODO
 */
int fifo_pull(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size,
		const int64_t tout_usecs, log_ctx_t *log_ctx);

/**
 * //TODO
 */
int fifo_show(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size,
		const int64_t tout_usecs, log_ctx_t *log_ctx);

/**
 * //TODO
 */
ssize_t fifo_get_buffer_level(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx);

/**
 * //TODO
 */
int fifo_traverse(fifo_ctx_t *fifo_ctx, int elem_cnt,
		void (*it_fxn)(void *elem, ssize_t elem_size, int idx, void *it_arg,
				int *ref_flag_break), void *it_arg, log_ctx_t *log_ctx);

/**
 * //TODO
 */
void fifo_empty(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx);

#endif /* UTILS_SRC_FIFO_H_ */
