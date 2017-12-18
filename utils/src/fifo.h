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
 * @file fifo.h
 * @brief Simple pointer queue (FIFO) implementation
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_FIFO_H_
#define UTILS_SRC_FIFO_H_

#include <sys/types.h>
#include <inttypes.h>

/* **** Definitions **** */

/**
 * FLag to indicate this FIFO is non-blocking. FIFO is blocking by default
 * (namely, if this flag is not set), which means a writing operation (put)
 * will block until an empty slot is available and reading (get) will block
 * if FIFO is empty.
 */
#define FIFO_O_NONBLOCK 	1
/**
 * FLag to indicate this FIFO is to be defined in shared memory.
 * If it is the case, the FIFO can be shared by parent and son processes
 * (this is thought to be used as an interprocess communication mechanism).
 */
#define FIFO_PROCESS_SHARED 2

/* Forward definitions */
typedef struct fifo_ctx_s fifo_ctx_t;

typedef void*(fifo_elem_ctx_dup_fxn_t)(const void*);
typedef void(fifo_elem_ctx_release_fxn_t)(void**);

typedef struct fifo_elem_alloc_fxn_s {
	fifo_elem_ctx_dup_fxn_t *elem_ctx_dup;
	fifo_elem_ctx_release_fxn_t *elem_ctx_release;
} fifo_elem_alloc_fxn_t;

/* **** Prototypes **** */

/**
 * //TODO
 */
fifo_ctx_t* fifo_open(size_t buf_slots_max, size_t chunk_size_max,
		uint32_t flags, const fifo_elem_alloc_fxn_t *fifo_elem_alloc_fxn);

/**
 * //TODO
 */
void fifo_close(fifo_ctx_t **ref_fifo_ctx);

/**
 * //TODO
 */
void fifo_set_blocking_mode(fifo_ctx_t *fifo_ctx, int do_block);

/**
 * //TODO
 */
int fifo_put_dup(fifo_ctx_t *fifo_ctx, const void *elem, size_t elem_size);

/**
 * //TODO
 */
int fifo_put(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t elem_size);

/**
 * //TODO
 */
int fifo_get(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size);

/**
 * //TODO
 */
int fifo_timedget(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size,
		int64_t tout_usecs);

/**
 * //TODO
 */
int fifo_show(fifo_ctx_t *fifo_ctx, void **elem, size_t *elem_size);

/**
 * //TODO
 */
ssize_t fifo_get_buffer_level(fifo_ctx_t *fifo_ctx);

/**
 * //TODO
 */
int fifo_traverse(fifo_ctx_t *fifo_ctx, int elem_cnt,
		void (*it_fxn)(void *elem, ssize_t elem_size, int idx, void *it_arg,
				int *ref_flag_break),
		void *it_arg);

/**
 * //TODO
 */
void fifo_empty(fifo_ctx_t *fifo_ctx);

#endif /* UTILS_SRC_FIFO_H_ */
