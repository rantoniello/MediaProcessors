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
 * @file fifo.c
 * @author Rafael Antoniello
 */

#include "fifo.h"

#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h> /* For NAME_MAX constant */
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "check_utils.h"
#include "log.h"
#include "stat_codes.h"

/* **** Definitions **** */

/**
 * Structure of the elements to be inserted in the FIFO.
 */
typedef struct fifo_elem_ctx_s {
    /**
     * Element size in bytes.
     */
    ssize_t size;
    /**
     * Element memory pool of "fifo_elem_ctx_s::size" bytes
     * (note that fifo_elem_ctx_s::size <= fifo_ctx_s::pool_size)
     */
    uint8_t elem_pool[]; // flexible array member must be last
} fifo_elem_ctx_t;

/**
 * Structure capable to be used as shared-memory in inter-process
 * communications. This structure holds the FIFO memory pool and all the
 * necessary tools for managing queuing.
 */
typedef struct shm_fifo_ctx_s {
    /**
     * Module flags:
     * - FIFO_O_NONBLOCK
     */
    volatile uint32_t flags;
    /**
     * FIFO buffer/pool size.
     */
    size_t pool_size;
    /**
     * FIFO instance API mutex.
     */
    pthread_mutex_t api_mutex;
    uint8_t flag_api_mutex_initialized;
    /**
     * Signals each time a new chunk enters the FIFO buffer.
     */
    pthread_cond_t buf_put_signal;
    uint8_t flag_buf_put_signal_initialized;
    /**
     * Signals each time a new chunk is consumed from the FIFO buffer.
     */
    pthread_cond_t buf_get_signal;
    uint8_t flag_buf_get_signal_initialized;
    /**
     * Number of slots currently used.
     * @deprecated
     */
    volatile ssize_t slots_used_cnt;
    /**
     * Addition of all the sizes of the elements currently enqueued in
     * the FIFO pool. Namely, is the overall FIFO buffer level in bytes.
     */
    volatile ssize_t buf_level;
    /**
     * This index is the buffer byte position corresponding to the next slot
     * available for input. Each time an element is pushed into the FIFO, it
     * is copied starting at this byte position; then the index is incremented
     * to point to the next byte position available in the FIFO buffer.
     */
    volatile int input_byte_idx;
    /**
     * This index is the buffer byte position corresponding to the start of
     * the next element available for output. Each time an element is pulled
     * out of the FIFO, we increment this index to point to the first byte of
     * the next element to be pulled.
     */
    volatile int output_byte_idx;
    /**
     * Buffer byte position of stuffing fragmentation.
     * Instead of fragmenting elements when pushing at the end of the circular
     * buffer, a stuffing position is marked when pushing and skipped while
     * pulling.
     * Note that: output_byte_idx <= stuffing_byte_idx &&
     * input_byte_idx <= stuffing_byte_idx.
     */
    volatile int stuffing_byte_idx;
    /**
     * File name associated to the shared memory.
     * Only used in the case SHM flag is set.
     */
    char shm_fifo_name[NAME_MAX + 1];
    /**
     * This is a circular buffer of elements/chunks of data.
     * @see fifo_elem_ctx_s
     */
    fifo_elem_ctx_t buf[]; // flexible array member must be last
} shm_fifo_ctx_t;

/**
 * FIFO module instance context structure.
 */
typedef struct fifo_ctx_s {
    /**
     * @see shm_fifo_ctx_s
     */
    shm_fifo_ctx_t *shm_fifo_ctx;
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
} fifo_ctx_s;

/* **** Prototypes **** */

static fifo_ctx_t* fifo_ctx_allocate(const size_t pool_size,
        const uint32_t flags,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *log_ctx);
static void fifo_ctx_deallocate(fifo_ctx_t **ref_fifo_ctx, log_ctx_t *log_ctx);

static shm_fifo_ctx_t* shm_fifo_ctx_allocate(const size_t pool_size,
        const uint32_t flags,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *log_ctx);
static void shm_fifo_ctx_deallocate(shm_fifo_ctx_t **ref_shm_fifo_ctx,
        log_ctx_t *log_ctx);

static inline int fifo_ctx_init(fifo_ctx_t *const fifo_ctx,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *__log_ctx);
static inline void fifo_ctx_deinit(fifo_ctx_t *const fifo_ctx,
        log_ctx_t *__log_ctx);

static inline int shm_fifo_ctx_init(shm_fifo_ctx_t *const shm_fifo_ctx,
        const size_t pool_size, const uint32_t flags, log_ctx_t *__log_ctx);
static inline void shm_fifo_ctx_deinit(shm_fifo_ctx_t *const shm_fifo_ctx,
        log_ctx_t *__log_ctx);

static int fifo_push_internal(fifo_ctx_t *fifo_ctx, void **ref_elem,
        const size_t elem_size, int *ref_flag_call_again, log_ctx_t *log_ctx);
static int fifo_pull_internal(fifo_ctx_t *fifo_ctx, void **ref_elem,
        size_t *ref_elem_size, const int64_t tout_usecs,
        const int flag_do_flush, int *ref_flag_call_again, log_ctx_t *log_ctx);

static void* fifo_elem_ctx_memcpy_default(void *opaque, void *dest,
        const void *src, size_t size, log_ctx_t *log_ctx);
static int fifo_elem_ctx_dequeue_default(void *opaque, void **ref_elem,
        size_t *ref_elem_size, const void *src, size_t size,
        log_ctx_t *log_ctx);

/* **** Implementations **** */


fifo_ctx_t* fifo_open(const size_t pool_size, const uint32_t flags,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *log_ctx)
{
    fifo_ctx_t *fifo_ctx;
    register int end_code = STAT_ERROR;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments.
     * Parameter 'flags' may take any value.
     * Parameter 'fifo_extend_param_ctx' is allowed to be NULL.
     */
    CHECK_DO(pool_size > sizeof(fifo_elem_ctx_t), return NULL);

    /* Allocate FIFO context structure */
    fifo_ctx = fifo_ctx_allocate(pool_size, flags, fifo_extend_param_ctx,
            LOG_CTX_GET());
    CHECK_DO(fifo_ctx != NULL, goto end);

    /* Initialize context structure */
    if(fifo_ctx_init(fifo_ctx, fifo_extend_param_ctx, LOG_CTX_GET()) !=
            STAT_SUCCESS || shm_fifo_ctx_init(fifo_ctx->shm_fifo_ctx,
                    pool_size, flags, LOG_CTX_GET()) != STAT_SUCCESS)
        goto end;

    end_code = STAT_SUCCESS;
end:
    if(end_code != STAT_SUCCESS)
        fifo_close(&fifo_ctx, LOG_CTX_GET());
    return fifo_ctx;
}


fifo_ctx_t* fifo_open_shm(const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *log_ctx)
{
    register const char *fifo_file_name;
    register size_t shm_fifo_ctx_size;
    fifo_ctx_t *fifo_ctx = NULL;
    register shm_fifo_ctx_t *shm_fifo_ctx;
    register int fd = -1, end_code = STAT_ERROR;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments */
    CHECK_DO(fifo_extend_param_ctx != NULL && (fifo_file_name =
            fifo_extend_param_ctx->shm_fifo_name) != NULL, return NULL);

    /* Allocate FIFO context structure
     * (note FIFO context is not shared memory)
     */
    fifo_ctx = (fifo_ctx_t*)calloc(1, sizeof(fifo_ctx_t));
    CHECK_DO(fifo_ctx != NULL, goto end);

    /* Open already existent shared memory segment */
    fd = shm_open(fifo_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if(fd < 0) {
        LOGE("Could not open SHM-FIFO (errno: %d)\n", errno);
        goto end;
    }

    /* Partially map the shared memory segment in the address space of the
     * calling process just to obtain shm size
     */
    shm_fifo_ctx = mmap(NULL, sizeof(shm_fifo_ctx_t), PROT_READ|PROT_WRITE,
            MAP_SHARED, fd, 0);
    CHECK_DO(shm_fifo_ctx != MAP_FAILED, goto end);
    fifo_ctx->shm_fifo_ctx = shm_fifo_ctx; // alias

    shm_fifo_ctx_size = sizeof(shm_fifo_ctx_t) + shm_fifo_ctx->pool_size;
    CHECK_DO(munmap(shm_fifo_ctx, sizeof(shm_fifo_ctx_t)) == 0, goto end);
    fifo_ctx->shm_fifo_ctx = shm_fifo_ctx = NULL; // alias

    /* Re-map shared segment with actual size */
    shm_fifo_ctx = mmap(NULL, shm_fifo_ctx_size, PROT_READ|PROT_WRITE,
            MAP_SHARED, fd, 0);
    CHECK_DO(shm_fifo_ctx != MAP_FAILED, goto end);
    fifo_ctx->shm_fifo_ctx = shm_fifo_ctx;
    shm_fifo_ctx = NULL;

    /* Initialize context structure */
    if(fifo_ctx_init(fifo_ctx, fifo_extend_param_ctx, LOG_CTX_GET()) !=
            STAT_SUCCESS)
        goto end;

    end_code = STAT_SUCCESS;
end:
    /* File descriptor is not necessary to be kept */
    if(fd >= 0)
        ASSERT(close(fd) == 0);

    /* Release FIFO in case error occurred */
    if(end_code != 0)
        fifo_close_shm(&fifo_ctx, LOG_CTX_GET());

    return fifo_ctx;
}


void fifo_close(fifo_ctx_t **ref_fifo_ctx, log_ctx_t *log_ctx)
{
    register fifo_ctx_t *fifo_ctx;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    if(ref_fifo_ctx == NULL || (fifo_ctx = *ref_fifo_ctx) == NULL)
        return;

    fifo_ctx_deinit(fifo_ctx, LOG_CTX_GET());
    shm_fifo_ctx_deinit(fifo_ctx->shm_fifo_ctx, LOG_CTX_GET());

    fifo_ctx_deallocate(ref_fifo_ctx, log_ctx);
    // ref_fifo_ctx = NULL; // performed at 'fifo_ctx_deallocate()'
}


void fifo_close_shm(fifo_ctx_t **ref_fifo_ctx, log_ctx_t *log_ctx)
{
    register fifo_ctx_t *fifo_ctx;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    if(ref_fifo_ctx == NULL || (fifo_ctx = *ref_fifo_ctx) == NULL)
        return;

    fifo_ctx_deinit(fifo_ctx, LOG_CTX_GET());

    /* Remove the mapped shared memory segment from the address space */
    if(fifo_ctx->shm_fifo_ctx != NULL) {
        ASSERT(munmap(fifo_ctx->shm_fifo_ctx, sizeof(shm_fifo_ctx_t) +
                fifo_ctx->shm_fifo_ctx->pool_size) == 0);
    }

    free(fifo_ctx);
    *ref_fifo_ctx = NULL;
}


void fifo_set_blocking_mode(fifo_ctx_t *fifo_ctx, const int do_block,
        log_ctx_t *log_ctx)
{
    shm_fifo_ctx_t *shm_fifo_ctx;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    CHECK_DO(fifo_ctx != NULL &&
            (shm_fifo_ctx = fifo_ctx->shm_fifo_ctx) != NULL, return);

    pthread_mutex_lock(&shm_fifo_ctx->api_mutex);

    /* Set the 'non-blocking' bit-flag */
    if(do_block != 0)
        shm_fifo_ctx->flags &= ~((uint32_t)FIFO_O_NONBLOCK);
    else
        shm_fifo_ctx->flags |= (uint32_t)FIFO_O_NONBLOCK;

    /* Announce to unblock conditional waits */
    pthread_cond_broadcast(&shm_fifo_ctx->buf_put_signal);
    pthread_cond_broadcast(&shm_fifo_ctx->buf_get_signal);

    pthread_mutex_unlock(&shm_fifo_ctx->api_mutex);
}


int fifo_push(fifo_ctx_t *fifo_ctx, void **ref_elem, const size_t elem_size,
        log_ctx_t *log_ctx)
{
    int ret_code, flag_call_again = 0;

    do {
        ret_code = fifo_push_internal(fifo_ctx, ref_elem, elem_size,
                &flag_call_again, log_ctx);
    } while(flag_call_again != 0 && ret_code == STAT_SUCCESS);

    return ret_code;
}


int fifo_pull(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size,
        const int64_t tout_usecs, log_ctx_t *log_ctx)
{
    int ret_code, flag_call_again = 0;

    do {
        ret_code = fifo_pull_internal(fifo_ctx, ref_elem, ref_elem_size,
                    tout_usecs, 1, &flag_call_again, log_ctx);
    } while(flag_call_again != 0 && ret_code == STAT_SUCCESS);

    return ret_code;
}


int fifo_show(fifo_ctx_t *fifo_ctx, void **ref_elem, size_t *ref_elem_size,
        const int64_t tout_usecs, log_ctx_t *log_ctx)
{
    int ret_code, flag_call_again = 0;

    do {
        ret_code = fifo_pull_internal(fifo_ctx, ref_elem, ref_elem_size,
                    tout_usecs, 0, &flag_call_again, log_ctx);
    } while(flag_call_again != 0 && ret_code == STAT_SUCCESS);

    return ret_code;
}


ssize_t fifo_get_buffer_level(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx)
{
    shm_fifo_ctx_t *shm_fifo_ctx;
    ssize_t buf_level = -1; /* invalid value to indicate STAT_ERROR */
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments */
    CHECK_DO(fifo_ctx != NULL &&
            (shm_fifo_ctx = fifo_ctx->shm_fifo_ctx) != NULL, return -1);

    pthread_mutex_lock(&shm_fifo_ctx->api_mutex);
    buf_level = shm_fifo_ctx->buf_level;
    pthread_mutex_unlock(&shm_fifo_ctx->api_mutex);

    return buf_level;
}


int fifo_traverse(fifo_ctx_t *fifo_ctx, int elem_cnt,
        void (*it_fxn)(void *elem, ssize_t elem_size, int idx, void *it_arg,
                int *ref_flag_break), void *it_arg, log_ctx_t *log_ctx)
{
    register ssize_t buf_level;
    register int output_byte_idx;
    shm_fifo_ctx_t *shm_fifo_ctx = NULL;

    shm_fifo_ctx = fifo_ctx->shm_fifo_ctx;
    buf_level = shm_fifo_ctx->buf_level;
    output_byte_idx = shm_fifo_ctx->output_byte_idx;

    register fifo_elem_ctx_t *fifo_elem_ctx = (fifo_elem_ctx_t*)(((uint8_t*)shm_fifo_ctx->buf) + output_byte_idx);




#if 0 //FIXME: TODO
    register ssize_t buf_level;
    register int i, cnt, cnt_max, flag_break;
    register uint32_t pool_size;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments */
    CHECK_DO(fifo_ctx != NULL, return STAT_ERROR);
    CHECK_DO(elem_cnt > 0 || elem_cnt == -1, return STAT_ERROR);
    CHECK_DO(it_fxn != NULL, return STAT_ERROR);
    /* Argument 'it_arg' is allowed to be NULL. */

    /* Lock API MUTEX */
    pthread_mutex_lock(&fifo_ctx->api_mutex);

    /* Iterate: we do it beginning with the input index (namely, we go from
     * the newest queued element to the oldest).
     */
    buf_level = fifo_ctx->buf_level;
    if(elem_cnt == -1)
        elem_cnt = buf_level; // '-1' means "traverse all the FIFO"
    cnt_max = (elem_cnt < buf_level)? elem_cnt: buf_level;
    pool_size = fifo_ctx->pool_size;
    flag_break = 0;
    for(i = fifo_ctx->input_byte_idx - 1, cnt = 0; cnt < cnt_max; cnt++) { //FIXME!!
        fifo_elem_ctx_t fifo_elem_ctx= fifo_ctx->buf[i];

        /* Execute iterator callback function */
        it_fxn(fifo_elem_ctx.elem, fifo_elem_ctx.size, i, it_arg, &flag_break);
        if(flag_break!= 0)
            break;

        /* Update for next iteration; note that 'pool_size' is > 0 in
         * modulo operation:
         * integer r = a % b;
         * r= r < 0 ? r + b : r; <- Only works if B> 0
         */
        i= (i- 1)% pool_size;
        if(i< 0)
            i= i+ pool_size;
    }

    pthread_mutex_unlock(&fifo_ctx->api_mutex);
    return STAT_SUCCESS;
#endif
    return STAT_ERROR;
}


void fifo_empty(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx)
{
    shm_fifo_ctx_t *shm_fifo_ctx;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments */
    CHECK_DO(fifo_ctx != NULL &&
            (shm_fifo_ctx = fifo_ctx->shm_fifo_ctx) != NULL, return);

    pthread_mutex_lock(&shm_fifo_ctx->api_mutex);

    /* Reset FIFO level and indexes */
    shm_fifo_ctx->slots_used_cnt = 0;
    shm_fifo_ctx->buf_level = 0;
    shm_fifo_ctx->input_byte_idx = 0;
    shm_fifo_ctx->output_byte_idx = 0;

    pthread_mutex_unlock(&shm_fifo_ctx->api_mutex);
}


static fifo_ctx_t* fifo_ctx_allocate(const size_t pool_size,
        const uint32_t flags,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *log_ctx)
{
    register fifo_ctx_t *fifo_ctx;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    fifo_ctx = (fifo_ctx_t*)calloc(1, sizeof(fifo_ctx_t));
    CHECK_DO(fifo_ctx != NULL, return NULL);

    if(flags & FIFO_PROCESS_SHARED)
        fifo_ctx->shm_fifo_ctx = shm_fifo_ctx_allocate(pool_size, flags,
                fifo_extend_param_ctx, log_ctx);
    else
        fifo_ctx->shm_fifo_ctx = (shm_fifo_ctx_t*)calloc(1,
                sizeof(shm_fifo_ctx_t) + pool_size);
    CHECK_DO(fifo_ctx->shm_fifo_ctx != NULL, return NULL);

    return fifo_ctx;
}


static void fifo_ctx_deallocate(fifo_ctx_t **ref_fifo_ctx, log_ctx_t *log_ctx)
{
    register fifo_ctx_t *fifo_ctx;
    register shm_fifo_ctx_t *shm_fifo_ctx;

    if(ref_fifo_ctx == NULL || (fifo_ctx = *ref_fifo_ctx) == NULL)
        return;

    shm_fifo_ctx = fifo_ctx->shm_fifo_ctx;
    if(shm_fifo_ctx != NULL) {
        if(shm_fifo_ctx->flags & FIFO_PROCESS_SHARED)
            shm_fifo_ctx_deallocate(&fifo_ctx->shm_fifo_ctx, log_ctx);
        else {
            free(shm_fifo_ctx);
            fifo_ctx->shm_fifo_ctx = NULL;
        }
    }

    free(fifo_ctx);
    *ref_fifo_ctx = NULL;

}


static shm_fifo_ctx_t* shm_fifo_ctx_allocate(const size_t pool_size,
        const uint32_t flags,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *log_ctx)
{
    register const char *fifo_file_name;
    register size_t shm_fifo_ctx_size;
    shm_fifo_ctx_t *shm_fifo_ctx = NULL;
    register int fd = -1, end_code = STAT_ERROR;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments:
     * Note that this function is called from 'fifo_open'; most of the
     * arguments were already checked.
     */
    CHECK_DO(fifo_extend_param_ctx != NULL && (fifo_file_name =
            fifo_extend_param_ctx->shm_fifo_name) != NULL, goto end);
    if(strlen(fifo_file_name) > NAME_MAX) {
        LOGE("Maximum FIFO file-name length exceeded; name has to be "
                "maximum %d characters\n", NAME_MAX);
        goto end;
    }

    // Create the shared memory segment
    // Note on 'O_CREAT|O_EXCL' usage: if both flags specified when opening a
    // shared memory object with the given name that already exists, return
    // error 'EEXIST'. The check for the existence of the object, and its
    // creation if it does not exist, are performed **atomically**.
    fd = shm_open(fifo_file_name, O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR);
    if(fd < 0) {
        LOGE("Could not open SHM-FIFO (errno: %d)\n", errno);
        goto end;
    }

    // Compute the size of the FIFO context structure allocation
    // Note that 'sizeof(shm_fifo_ctx_t)' is intended to reserve the FIFO's
    // context space aligned at the beginning of the shared-memory segment
    shm_fifo_ctx_size = sizeof(shm_fifo_ctx_t) + pool_size;

    // Configure size of the shared memory segment
    // Note: we use the 'ftruncate()' on a brand new FIFO, thus, the new FIFO
    // memory space reads as null bytes ('\0'). Also note that the file offset
    // is not changed (points to the beginning of the shared-memory segment).
    CHECK_DO(ftruncate(fd, shm_fifo_ctx_size) == 0, goto end);

    // Map the shared memory segment in the address space of calling process
    if((shm_fifo_ctx = mmap(NULL, shm_fifo_ctx_size, PROT_READ|PROT_WRITE,
            MAP_SHARED, fd, 0)) == MAP_FAILED)
        shm_fifo_ctx = NULL;
    CHECK_DO(shm_fifo_ctx != NULL, goto end);

    memset(shm_fifo_ctx, 0, shm_fifo_ctx_size);
    snprintf(shm_fifo_ctx->shm_fifo_name, NAME_MAX + 1, "%s", fifo_file_name);

    end_code = STAT_SUCCESS;
end:
    /* File descriptor is not necessary to be kept */
    if(fd >= 0)
        ASSERT(close(fd) == 0);

    /* Deallocate FIFO in case error occurred */
    if(end_code != STAT_SUCCESS)
        shm_fifo_ctx_deallocate(&shm_fifo_ctx, LOG_CTX_GET());

    return shm_fifo_ctx;
}


static void shm_fifo_ctx_deallocate(shm_fifo_ctx_t **ref_shm_fifo_ctx,
        log_ctx_t *log_ctx)
{
    register shm_fifo_ctx_t *shm_fifo_ctx;
    char shm_fifo_name[NAME_MAX + 1];
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    if(ref_shm_fifo_ctx == NULL || (shm_fifo_ctx = *ref_shm_fifo_ctx) == NULL)
        return;

    ASSERT(strcpy(shm_fifo_name, shm_fifo_ctx->shm_fifo_name) ==
            shm_fifo_name);

    // Remove the mapped shared memory segment from the address space
    ASSERT(munmap(shm_fifo_ctx, sizeof(shm_fifo_ctx_t) +
            shm_fifo_ctx->pool_size) == 0);

    // Unlink FIFO
    ASSERT(shm_unlink(shm_fifo_name) == 0);

    *ref_shm_fifo_ctx = NULL;
}


static inline int fifo_ctx_init(fifo_ctx_t *const fifo_ctx,
        const fifo_extend_param_ctx_t *fifo_extend_param_ctx,
        log_ctx_t *__log_ctx)
{
    fifo_ctx->elem_ctx_memcpy = fifo_elem_ctx_memcpy_default;

    fifo_ctx->elem_ctx_dequeue = fifo_elem_ctx_dequeue_default;

    fifo_ctx->opaque = NULL;

    if(fifo_extend_param_ctx != NULL) {

        if(fifo_extend_param_ctx->elem_ctx_memcpy != NULL)
            fifo_ctx->elem_ctx_memcpy = fifo_extend_param_ctx->elem_ctx_memcpy;

        if(fifo_extend_param_ctx->elem_ctx_dequeue != NULL)
            fifo_ctx->elem_ctx_dequeue =
                    fifo_extend_param_ctx->elem_ctx_dequeue;

        if(fifo_extend_param_ctx->opaque != NULL)
            fifo_ctx->opaque = fifo_extend_param_ctx->opaque;
    }

    return STAT_SUCCESS;
}


static inline void fifo_ctx_deinit(fifo_ctx_t *const fifo_ctx,
        log_ctx_t *__log_ctx)
{
    if(fifo_ctx == NULL)
        return;

    // Reserved for future use
}


static inline int shm_fifo_ctx_init(shm_fifo_ctx_t *const shm_fifo_ctx,
        const size_t pool_size, const uint32_t flags, log_ctx_t *__log_ctx)
{
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;
    register pthread_mutexattr_t *p_mutexattr = NULL;
    register int end_code = STAT_ERROR;

    shm_fifo_ctx->flags = flags;

    shm_fifo_ctx->pool_size = pool_size;

    if(flags & FIFO_PROCESS_SHARED) {
        pthread_mutexattr_init(&mutexattr);
        CHECK_DO(pthread_mutexattr_setpshared(&mutexattr,
                PTHREAD_PROCESS_SHARED) == 0, goto end);
        p_mutexattr = &mutexattr;
    }
    CHECK_DO(pthread_mutex_init(&shm_fifo_ctx->api_mutex, p_mutexattr) == 0,
            goto end);
    shm_fifo_ctx->flag_api_mutex_initialized = 1;

    pthread_condattr_init(&condattr);
    CHECK_DO(pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) == 0,
            goto end);
    if(flags & FIFO_PROCESS_SHARED) {
        CHECK_DO(pthread_condattr_setpshared(&condattr,
                PTHREAD_PROCESS_SHARED) == 0, goto end);
    }
    CHECK_DO(pthread_cond_init(&shm_fifo_ctx->buf_put_signal, &condattr) == 0,
            goto end);
    shm_fifo_ctx->flag_buf_put_signal_initialized = 1;

    CHECK_DO(pthread_cond_init(&shm_fifo_ctx->buf_get_signal, &condattr) == 0,
            goto end);
    shm_fifo_ctx->flag_buf_get_signal_initialized = 1;

    //shm_fifo_ctx->slots_used_cnt = 0; // Already performed at allocation

    //shm_fifo_ctx->buf_level = 0; // Already performed at allocation

    //shm_fifo_ctx->input_byte_idx = 0; // Already performed at allocation

    //shm_fifo_ctx->output_byte_idx = 0; // Already performed at allocation

    //shm_fifo_ctx->shm_fifo_name // Already initialized at allocation

    end_code = STAT_SUCCESS;
end:
    return end_code;
}


static inline void shm_fifo_ctx_deinit(shm_fifo_ctx_t *const shm_fifo_ctx,
        log_ctx_t *__log_ctx)
{
    register uint8_t flag_api_mutex_deinit, flag_put_signal_deinit,
        flag_get_signal_deinit;

    if(shm_fifo_ctx == NULL)
        return;

    flag_api_mutex_deinit = shm_fifo_ctx->flag_api_mutex_initialized;
    flag_put_signal_deinit = shm_fifo_ctx->flag_buf_put_signal_initialized;
    flag_get_signal_deinit = shm_fifo_ctx->flag_buf_get_signal_initialized;

    /* Eventually unblock FIFO */
    shm_fifo_ctx->flags &= ~((uint32_t)FIFO_O_NONBLOCK);
    if(flag_api_mutex_deinit != 0) {
        pthread_mutex_lock(&shm_fifo_ctx->api_mutex);
        if(flag_put_signal_deinit != 0)
            pthread_cond_broadcast(&shm_fifo_ctx->buf_put_signal);
        if(flag_get_signal_deinit != 0)
            pthread_cond_broadcast(&shm_fifo_ctx->buf_get_signal);
        pthread_mutex_unlock(&shm_fifo_ctx->api_mutex);
    }

    /* Release API MUTEX */
    if(flag_api_mutex_deinit != 0) {
        ASSERT(pthread_mutex_destroy(&shm_fifo_ctx->api_mutex) == 0);
        shm_fifo_ctx->flag_api_mutex_initialized = 0;
    }

    /* Release conditionals */
    if(flag_put_signal_deinit != 0) {
        ASSERT(pthread_cond_destroy(&shm_fifo_ctx->buf_put_signal) == 0);
        shm_fifo_ctx->flag_buf_put_signal_initialized = 0;
    }
    if(flag_get_signal_deinit != 0) {
        ASSERT(pthread_cond_destroy(&shm_fifo_ctx->buf_get_signal) == 0);
        shm_fifo_ctx->flag_buf_get_signal_initialized = 0;
    }
}


static int fifo_push_internal(fifo_ctx_t *fifo_ctx, void **ref_elem,
        const size_t elem_size, int *ref_flag_call_again, log_ctx_t *log_ctx)
{
    shm_fifo_ctx_t *shm_fifo_ctx;
    register size_t pool_size, elem_mem_size;
    register int end_code = STAT_ERROR;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments */
    CHECK_DO(fifo_ctx != NULL &&
            (shm_fifo_ctx = fifo_ctx->shm_fifo_ctx) != NULL &&
            ref_elem != NULL && *ref_elem != NULL && elem_size > 0,
            return STAT_ERROR);
    if((elem_mem_size = elem_size + sizeof(fifo_elem_ctx_t)) > (pool_size =
            shm_fifo_ctx->pool_size)) {
        LOGE("Input element size can not exceed FIFO overall pool size (%d "
                "bytes)\n", pool_size);
        return STAT_ENOMEM;
    }

    /* Reset arguments to be returned by value */
    *ref_flag_call_again = 0;

    /* Lock API MUTEX */
    pthread_mutex_lock(&shm_fifo_ctx->api_mutex);

    /* In the case of blocking FIFO, if buffer is full we block until an
     * element is consumed and a new free slot is available.
     * In the case of a non-blocking FIFO, if buffer is full we exit returning
     * 'ENOMEM' status.
     */
    while((shm_fifo_ctx->buf_level + elem_mem_size > pool_size) &&
            !(shm_fifo_ctx->flags & FIFO_O_NONBLOCK)) {
        pthread_cond_broadcast(&shm_fifo_ctx->buf_put_signal);
        pthread_cond_wait(&shm_fifo_ctx->buf_get_signal,
                &shm_fifo_ctx->api_mutex);
    }
    if((shm_fifo_ctx->buf_level + elem_mem_size > pool_size) &&
            (shm_fifo_ctx->flags & FIFO_O_NONBLOCK)) {
        LOGW("SHM-FIFO buffer overflow!\n");
        end_code = STAT_ENOMEM;
        goto end;
    }

    /* Manage fragmentation stuff at the end of the memory pool */
    if(shm_fifo_ctx->stuffing_byte_idx != 0) {
        CHECK_DO(shm_fifo_ctx->input_byte_idx <=
                shm_fifo_ctx->stuffing_byte_idx, goto end);
    } else {
        if(shm_fifo_ctx->input_byte_idx + elem_mem_size > pool_size) {
            /* Set fragmentation index and update circular buffer management
             * variables
             */
            shm_fifo_ctx->stuffing_byte_idx = shm_fifo_ctx->input_byte_idx;
            shm_fifo_ctx->buf_level += pool_size - shm_fifo_ctx->input_byte_idx;
            shm_fifo_ctx->input_byte_idx = 0;

            /* Signal we have a new element in the FIFO */
            pthread_cond_broadcast(&shm_fifo_ctx->buf_put_signal);
            *ref_flag_call_again = 1;
            end_code = STAT_SUCCESS;
            goto end;
        }
    }

    /* *** Push new element ****
     * (input_byte_idx + elem_size + sizeof(fifo_elem_ctx_t) <= pool_size)
     */

    /* WARNING: after 'pthread_cond_wait' any member of 'shm_fifo_ctx' may have
     * have changed concurrently (refresh local references to these below)
     */

    /* Get FIFO slot where to put new element */
    register fifo_elem_ctx_t *fifo_elem_ctx = (fifo_elem_ctx_t*)
            (((uint8_t*)shm_fifo_ctx->buf) + shm_fifo_ctx->input_byte_idx);

    fifo_ctx->elem_ctx_memcpy(fifo_ctx->opaque, fifo_elem_ctx->elem_pool,
            *ref_elem, elem_size, LOG_CTX_GET());
    fifo_elem_ctx->size = elem_size;

    /* Update circular buffer management variables */
    shm_fifo_ctx->slots_used_cnt += 1;
    shm_fifo_ctx->buf_level += elem_mem_size;
    shm_fifo_ctx->input_byte_idx =
            (shm_fifo_ctx->input_byte_idx + elem_mem_size) % pool_size;

    /* Signal we have a new element in the FIFO */
    pthread_cond_broadcast(&shm_fifo_ctx->buf_put_signal);

    end_code = STAT_SUCCESS;
end:
    pthread_mutex_unlock(&shm_fifo_ctx->api_mutex);
    return end_code;
}


static int fifo_pull_internal(fifo_ctx_t *fifo_ctx, void **ref_elem,
        size_t *ref_elem_size, const int64_t tout_usecs,
        const int flag_do_flush, int *ref_flag_call_again, log_ctx_t *log_ctx)
{
    shm_fifo_ctx_t *shm_fifo_ctx;
    register size_t elem_size;
    register int end_code = STAT_ERROR;
    struct timespec ts_tout = {0};
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments.
     * Parameter 'tout_usecs' may take any value.
     */
    CHECK_DO(fifo_ctx != NULL &&
            (shm_fifo_ctx = fifo_ctx->shm_fifo_ctx) != NULL &&
            ref_elem != NULL && ref_elem_size != NULL, return STAT_ERROR);

    /* Lock API MUTEX */
    pthread_mutex_lock(&shm_fifo_ctx->api_mutex);

    /* Reset arguments to be returned by value */
    if(*ref_flag_call_again == 0) {
        *ref_elem = NULL;
        *ref_elem_size = 0;
    }
    *ref_flag_call_again = 0;

    /* Get current time and compute time-out if applicable.
     * Note that a negative time-out mens 'wait indefinitely'.
     */
    if(tout_usecs >= 0) {
        struct timespec ts_curr = {0, 0};
        register int64_t curr_nsec;
        CHECK_DO(clock_gettime(CLOCK_MONOTONIC, &ts_curr) == 0, goto end);

        curr_nsec = (int64_t)ts_curr.tv_sec * 1000000000 +
                (int64_t)ts_curr.tv_nsec;
        curr_nsec += (tout_usecs * 1000);
        ts_tout.tv_sec = curr_nsec / 1000000000;
        ts_tout.tv_nsec = curr_nsec % 1000000000;
    }

    /* In the case of blocking FIFO, if buffer is empty we block until a new
     * element is inserted, or if it is the case, until time-out occur.
     * In the case of a non-blocking FIFO, if buffer is empty we exit
     * returning 'EAGAIN' status.
     */
    while(shm_fifo_ctx->buf_level <= 0 &&
            !(shm_fifo_ctx->flags & FIFO_O_NONBLOCK)) {
        pthread_cond_broadcast(&shm_fifo_ctx->buf_get_signal);
        if(tout_usecs >= 0) {
            if(pthread_cond_timedwait(&shm_fifo_ctx->buf_put_signal,
                    &shm_fifo_ctx->api_mutex, &ts_tout) == ETIMEDOUT) {
                LOGW("FIFO pulling timed-out on empty buffer!\n");
                end_code = STAT_ETIMEDOUT;
                goto end;
            }
        } else {
            CHECK_DO(pthread_cond_wait(&shm_fifo_ctx->buf_put_signal,
                    &shm_fifo_ctx->api_mutex) == 0, goto end);
        }
    }
    if(shm_fifo_ctx->buf_level <= 0 &&
            (shm_fifo_ctx->flags & FIFO_O_NONBLOCK)) {
        end_code = STAT_EAGAIN;
printf("######## %s:fifo_pull_internal %d proc_frame_ctx dequeue = %p; size = %zu\n", __FILE__, __LINE__, *ref_elem, *ref_elem_size); fflush(stdout); //FIXME!!
        goto end;
    }

    /* If present, skip fragmentation at the end of the memory pool
     * (always in any case, even when 'flag_do_flush == 0'
     */
    if(shm_fifo_ctx->stuffing_byte_idx != 0) {
        CHECK_DO(shm_fifo_ctx->output_byte_idx <=
                shm_fifo_ctx->stuffing_byte_idx, goto end);
        if(shm_fifo_ctx->output_byte_idx == shm_fifo_ctx->stuffing_byte_idx) {
            shm_fifo_ctx->buf_level -= (shm_fifo_ctx->pool_size -
                    shm_fifo_ctx->output_byte_idx);
            shm_fifo_ctx->output_byte_idx = 0;
            shm_fifo_ctx->stuffing_byte_idx = 0; // remove mark

            /* Signal we have a new free slot in the FIFO */
            pthread_cond_broadcast(&shm_fifo_ctx->buf_get_signal);
            *ref_flag_call_again = 1; // mark to call pull again
            end_code = STAT_SUCCESS;
printf("######## %s:fifo_pull_internal %d proc_frame_ctx dequeue = %p; size = %zu\n", __FILE__, __LINE__, *ref_elem, *ref_elem_size); fflush(stdout); //FIXME!!
            goto end;
        }
    }

    /* *** Pull new element ****
     * (output_byte_idx + sizeof(fifo_elem_ctx_t) + elem_size <= pool_size)
     */

    /* WARNING: after 'pthread_cond_wait' any member of 'shm_fifo_ctx' may have
     * have changed concurrently (refresh local references to these below)
     */

    /* Get FIFO slot from where to read the element */
    register fifo_elem_ctx_t *fifo_elem_ctx = (fifo_elem_ctx_t*)
            (((uint8_t*)shm_fifo_ctx->buf) + shm_fifo_ctx->output_byte_idx);
    CHECK_DO((elem_size = fifo_elem_ctx->size) > 0, goto end);
printf("######## %s:fifo_pull_internal %d proc_frame_ctx dequeue = %p; size = %zu\n", __FILE__, __LINE__, *ref_elem, *ref_elem_size); fflush(stdout); //FIXME!!
    /* Allocate, copy the element and set the element references to return. */
    if(fifo_ctx->elem_ctx_dequeue(fifo_ctx->opaque, ref_elem, ref_elem_size,
            &fifo_elem_ctx->elem_pool[0], elem_size, LOG_CTX_GET()) !=
                    STAT_SUCCESS) {
printf("######## %s:fifo_pull_internal %d proc_frame_ctx dequeue = %p; size = %zu\n", __FILE__, __LINE__, *ref_elem, *ref_elem_size); fflush(stdout); //FIXME!!
        goto end;
    }
printf("######## %s:fifo_pull_internal %d proc_frame_ctx dequeue = %p; size = %zu\n", __FILE__, __LINE__, *ref_elem, *ref_elem_size); fflush(stdout); //FIXME!!
    /* Update circular buffer management variables if applicable. */
    if(flag_do_flush != 0) {
        shm_fifo_ctx->slots_used_cnt -= 1;
        shm_fifo_ctx->buf_level -= sizeof(fifo_elem_ctx_t) + elem_size;
        shm_fifo_ctx->output_byte_idx = (shm_fifo_ctx->output_byte_idx +
                sizeof(fifo_elem_ctx_t) + elem_size) % shm_fifo_ctx->pool_size;

        /* Signal we have a new free slot in the FIFO */
        pthread_cond_broadcast(&shm_fifo_ctx->buf_get_signal);
    }

    end_code = STAT_SUCCESS;
end:
    pthread_mutex_unlock(&shm_fifo_ctx->api_mutex);
printf("######## %s:fifo_pull_internal %d proc_frame_ctx dequeue = %p; size = %zu\n", __FILE__, __LINE__, *ref_elem, *ref_elem_size); fflush(stdout); //FIXME!!
    return end_code;
}


static void* fifo_elem_ctx_memcpy_default(void *opaque, void *dest,
        const void *src, size_t size, log_ctx_t *log_ctx)
{
    return memcpy(dest, src, size);
}


static int fifo_elem_ctx_dequeue_default(void *opaque, void **ref_elem,
        size_t *ref_elem_size, const void *src, size_t size,
        log_ctx_t *log_ctx)
{
    void *elem_cpy;
    LOG_CTX_INIT(log_ctx); /* Argument 'log_ctx' is allowed to be NULL */

    /* Check arguments */
    CHECK_DO(ref_elem != NULL && ref_elem_size != NULL && src != NULL &&
            size > 0, return STAT_ERROR);

    /* Allocate and copy the element */
    elem_cpy = malloc(size);
    CHECK_DO(elem_cpy != NULL, return STAT_ERROR);
    memcpy(elem_cpy, src, size);

    /* Set the element references to return. */
    *ref_elem = elem_cpy;
    *ref_elem_size = size;

    return STAT_SUCCESS;
}

