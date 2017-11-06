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
 * @file proc.c
 * @author Rafael Antoniello
 */

#include "proc.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/schedule.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocsutils/fair_lock.h>
#include <libmediaprocsutils/interr_usleep.h>

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
 * Processor's statistic thread period (1 second).
 */
#define PROC_STATS_THR_MEASURE_PERIOD_USECS (1000000)

/**
 * Processor's statistic thread argument.
 */
typedef struct proc_stats_thr_arg_s {
	proc_ctx_t *proc_ctx;
	interr_usleep_ctx_t *interr_usleep_ctx;
} proc_stats_thr_arg_t;

/* **** Prototypes **** */

static void* proc_stats_thr(void *t);
static void* proc_thr(void *t);

/* **** Implementations **** */

proc_ctx_t* proc_open(const proc_if_t *proc_if, const char *settings_str,
		int proc_instance_index, uint32_t fifo_ctx_maxsize[PROC_IO_NUM],
		log_ctx_t *log_ctx, va_list arg)
{
	int i, ret_code, end_code= STAT_ERROR;
	proc_ctx_t *proc_ctx= NULL;
	fifo_elem_alloc_fxn_t fifo_elem_alloc_fxn= {0};
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	CHECK_DO(fifo_ctx_maxsize!= NULL , return NULL);
		// Note: 'log_ctx' is allowed to be NULL

	/* Check mandatory call-backs existence */
	CHECK_DO(proc_if->open!= NULL, goto end);
	CHECK_DO(proc_if->close!= NULL, goto end);
	CHECK_DO(proc_if->process_frame!= NULL, goto end);

	/* Open (allocate) the specific processor (PROC) instance */
	proc_ctx= proc_if->open(proc_if, settings_str, LOG_CTX_GET(), arg);
	CHECK_DO(proc_ctx!= NULL, goto end);

	/* Set processor (PROC) interface context structure */
	proc_ctx->proc_if= proc_if;

	/* Set PROC register index. */
	proc_ctx->proc_instance_index= proc_instance_index;

	/* API mutual exclusion lock */
	ret_code= pthread_mutex_init(&proc_ctx->api_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	/* Set external LOG module */
	proc_ctx->log_ctx= LOG_CTX_GET();

	/* Initialize input FIFO buffer */
	fifo_elem_alloc_fxn.elem_ctx_dup=
			proc_if->iput_fifo_elem_opaque_dup!= NULL?
			(fifo_elem_ctx_dup_fxn_t*)proc_if->iput_fifo_elem_opaque_dup:
			(fifo_elem_ctx_dup_fxn_t*)proc_frame_ctx_dup;
	fifo_elem_alloc_fxn.elem_ctx_release=
			proc_if->iput_fifo_elem_opaque_release!= NULL?
			(fifo_elem_ctx_release_fxn_t*)
			proc_if->iput_fifo_elem_opaque_release:
			(fifo_elem_ctx_release_fxn_t*)proc_frame_ctx_release;

	proc_ctx->fifo_ctx_array[PROC_IPUT]= fifo_open(
			fifo_ctx_maxsize[PROC_IPUT], 0, &fifo_elem_alloc_fxn);
	CHECK_DO(proc_ctx->fifo_ctx_array[PROC_IPUT]!= NULL, goto end);

	/* Initialize output FIFO buffer */
	fifo_elem_alloc_fxn.elem_ctx_dup=
			proc_if->oput_fifo_elem_opaque_dup!= NULL?
			(fifo_elem_ctx_dup_fxn_t*)proc_if->oput_fifo_elem_opaque_dup:
			(fifo_elem_ctx_dup_fxn_t*)proc_frame_ctx_dup;
	fifo_elem_alloc_fxn.elem_ctx_release=
			(fifo_elem_ctx_release_fxn_t*)proc_frame_ctx_release;
	proc_ctx->fifo_ctx_array[PROC_OPUT]= fifo_open(
			fifo_ctx_maxsize[PROC_OPUT], 0, &fifo_elem_alloc_fxn);
	CHECK_DO(proc_ctx->fifo_ctx_array[PROC_OPUT]!= NULL, goto end);

	/* Initialize input/output fair-locks */
	for(i= 0; i< PROC_IO_NUM; i++) {
		fair_lock_t *fair_lock= fair_lock_open();
		CHECK_DO(fair_lock!= NULL, goto end);
		proc_ctx->fair_lock_io_array[i]= fair_lock;
	}

	/* Initialize input/output MUTEX for bitrate statistics related */
	ret_code= pthread_mutex_init(&proc_ctx->acc_io_bits_mutex[PROC_IPUT], NULL);
	CHECK_DO(ret_code== 0, goto end);
	ret_code= pthread_mutex_init(&proc_ctx->acc_io_bits_mutex[PROC_OPUT], NULL);
	CHECK_DO(ret_code== 0, goto end);

	/* At last, launch PROC thread */
	proc_ctx->flag_exit= 0;
	proc_ctx->start_routine= (const void*(*)(void*))proc_thr;
	ret_code= pthread_create(&proc_ctx->proc_thread, NULL, proc_thr, proc_ctx);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		proc_if->close(&proc_ctx);
	return proc_ctx;
}

void proc_close(proc_ctx_t **ref_proc_ctx)
{
	proc_ctx_t *proc_ctx= NULL;
	LOG_CTX_INIT(NULL);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	if(ref_proc_ctx== NULL)
		return;

	if((proc_ctx= *ref_proc_ctx)!= NULL) {
		const proc_if_t *proc_if;
		void *thread_end_code= NULL;
		LOG_CTX_SET(proc_ctx->log_ctx);

		/* Join processing thread first
		 * - set flag to notify we are exiting processing;
		 * - unlock input and output FIFO's;
		 * - join thread.
		 */
		proc_ctx->flag_exit= 1;
		fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 0);
		fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 0);
		LOGD("Waiting thread to join... "); // comment-me
		pthread_join(proc_ctx->proc_thread, &thread_end_code);
		if(thread_end_code!= NULL) {
			ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
			free(thread_end_code);
			thread_end_code= NULL;
		}
		LOGD("joined O.K.\n"); // comment-me

		/* Release API mutual exclusion lock */
		ASSERT(pthread_mutex_destroy(&proc_ctx->api_mutex)== 0);

		/* Release input and output FIFO's */
		fifo_close(&proc_ctx->fifo_ctx_array[PROC_IPUT]);
		fifo_close(&proc_ctx->fifo_ctx_array[PROC_OPUT]);

		/* Release input/output fair locks */
		fair_lock_close(&proc_ctx->fair_lock_io_array[PROC_IPUT]);
		fair_lock_close(&proc_ctx->fair_lock_io_array[PROC_OPUT]);

		/* Release input/output MUTEX for bitrate statistics related */
		ASSERT(pthread_mutex_destroy(&proc_ctx->acc_io_bits_mutex[PROC_IPUT])
				== 0);
		ASSERT(pthread_mutex_destroy(&proc_ctx->acc_io_bits_mutex[PROC_OPUT])
				== 0);

		/* Close the specific PROC instance */
		CHECK_DO((proc_if= proc_ctx->proc_if)!= NULL, return); // sanity check
		CHECK_DO(proc_if->close!= NULL, return); // sanity check
		proc_if->close(ref_proc_ctx);
	}
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

int proc_send_frame(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx)
{
	int end_code;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_frame_ctx!= NULL, return STAT_ERROR);

	fair_lock(proc_ctx->fair_lock_io_array[PROC_IPUT]);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Write frame to input FIFO */
	end_code= fifo_put_dup(proc_ctx->fifo_ctx_array[PROC_IPUT],
			proc_frame_ctx, sizeof(void*));

	/* Update processor's input bitrate statistics
	 * (note we are "sending frame *into* the processor").
	 * For most processors implementation (specially for encoders and
	 * decoders), measuring traffic at this point would be precise.
	 * Nevertheless, for certain processors, as is the case of demultiplexers,
	 * this function ('proc_send_frame()') is not used thus input bitrate
	 * should be measured at some other point of the specific implementation
	 * (e.g. when receiving data from an INET socket).
	 */
	if(end_code== STAT_SUCCESS && proc_frame_ctx!= NULL) {
		register int i;
		register uint32_t byte_cnt_iput, bit_cnt_iput;
		register size_t width;
		pthread_mutex_t *acc_io_bits_mutex_p=
				&proc_ctx->acc_io_bits_mutex[PROC_IPUT];

		/* Get frame size (we "unroll" for the most common cases) */
		byte_cnt_iput= (proc_frame_ctx->width[0]* proc_frame_ctx->height[0])+
				(proc_frame_ctx->width[1]* proc_frame_ctx->height[1])+
				(proc_frame_ctx->width[2]* proc_frame_ctx->height[2]);
		// We do the rest in a loop...
		for(i= 3; (width= proc_frame_ctx->width[i])> 0 &&
				i< PROC_FRAME_NUM_DATA_POINTERS; i++)
			byte_cnt_iput+= width* proc_frame_ctx->height[i];

		/* Update currently accumulated bit value */
		bit_cnt_iput= (byte_cnt_iput<< 3);
		ASSERT(pthread_mutex_lock(acc_io_bits_mutex_p)== 0);
		proc_ctx->acc_io_bits[PROC_IPUT]+= bit_cnt_iput;
		ASSERT(pthread_mutex_unlock(acc_io_bits_mutex_p)== 0);
	}

	fair_unlock(proc_ctx->fair_lock_io_array[PROC_IPUT]);
	return end_code;
}

int proc_recv_frame(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	size_t fifo_elem_size= 0;
	proc_frame_ctx_t *proc_frame_ctx= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_proc_frame_ctx!= NULL, return STAT_ERROR);

	fair_lock(proc_ctx->fair_lock_io_array[PROC_OPUT]);

	LOG_CTX_SET(proc_ctx->log_ctx);

	*ref_proc_frame_ctx= NULL;

	/* Read a frame from the output FIFO */
	ret_code= fifo_get(proc_ctx->fifo_ctx_array[PROC_OPUT],
			(void**)ref_proc_frame_ctx, &fifo_elem_size);
	if(ret_code!= STAT_SUCCESS) {
		end_code= ret_code;
		goto end;
	}

	/* Update processor's output bitrate statistics
	 * (note we are "receiving frame *from* the processor").
	 * For most processors implementation (specially for encoders and
	 * decoders), measuring traffic at this point would be precise.
	 * Nevertheless, for certain processors, as is the case of multiplexers,
	 * this function ('proc_recv_frame()') is not used thus input bitrate
	 * should be measured at some other point of the specific implementation
	 * (e.g. when sending data to an INET socket).
	 */
	if((proc_frame_ctx= *ref_proc_frame_ctx)!= NULL) {
		register int i;
		register uint32_t byte_cnt_oput, bit_cnt_oput;
		register size_t width;
		pthread_mutex_t *acc_io_bits_mutex_p=
				&proc_ctx->acc_io_bits_mutex[PROC_OPUT];

		/* Get frame size (we "unroll" for the most common cases) */
		byte_cnt_oput= (proc_frame_ctx->width[0]* proc_frame_ctx->height[0])+
				(proc_frame_ctx->width[1]* proc_frame_ctx->height[1])+
				(proc_frame_ctx->width[2]* proc_frame_ctx->height[2]);
		// We do the rest in a loop...
		for(i= 3; (width= proc_frame_ctx->width[i])> 0 &&
				i< PROC_FRAME_NUM_DATA_POINTERS; i++)
			byte_cnt_oput+= width* proc_frame_ctx->height[i];

		/* Update currently accumulated bit value */
		bit_cnt_oput= (byte_cnt_oput<< 3);
		ASSERT(pthread_mutex_lock(acc_io_bits_mutex_p)== 0);
		proc_ctx->acc_io_bits[PROC_OPUT]+= bit_cnt_oput;
		ASSERT(pthread_mutex_unlock(acc_io_bits_mutex_p)== 0);
	}

	end_code= STAT_SUCCESS;
end:
	fair_unlock(proc_ctx->fair_lock_io_array[PROC_OPUT]);
	if(end_code!= STAT_SUCCESS)
		proc_frame_ctx_release(ref_proc_frame_ctx);
	return end_code;
}

int proc_opt(proc_ctx_t *proc_ctx, const char *tag, ...)
{
	va_list arg;
	int end_code;

	va_start(arg, tag);

	end_code= proc_vopt(proc_ctx, tag, arg);

	va_end(arg);

	return end_code;
}

int proc_vopt(proc_ctx_t *proc_ctx, const char *tag, va_list arg)
{
	int end_code= STAT_ERROR;
	const proc_if_t *proc_if= NULL;
	int (*rest_get)(proc_ctx_t *proc_ctx, proc_if_rest_fmt_t rest_fmt,
			void **ref_reponse)= NULL;
	int (*rest_put)(proc_ctx_t *proc_ctx, const char *str)= NULL;
	int (*opt)(proc_ctx_t *proc_ctx, const char *tag, va_list arg)= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	ASSERT(pthread_mutex_lock(&proc_ctx->api_mutex)== 0);

	LOG_CTX_SET(proc_ctx->log_ctx);

	proc_if= proc_ctx->proc_if;

	if(TAG_IS("PROC_UNBLOCK")) {
		fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 0);
		fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 0);
		end_code= STAT_SUCCESS;
	} else if(TAG_IS("PROC_GET")) {
		end_code= STAT_ENOTFOUND;
		if(proc_if!= NULL && (rest_get= proc_if->rest_get)!= NULL) {
			proc_if_rest_fmt_t rest_fmt= va_arg(arg, proc_if_rest_fmt_t);
			void **ref_reponse= va_arg(arg, void**);
			end_code= rest_get(proc_ctx, rest_fmt, ref_reponse);
		}
	} else if(TAG_IS("PROC_PUT")) {
		end_code= STAT_ENOTFOUND;
		if(proc_if!= NULL && (rest_put= proc_if->rest_put)!= NULL)
			end_code= rest_put(proc_ctx, va_arg(arg, const char*));
	} else {
		if(proc_if!= NULL && (opt= proc_if->opt)!= NULL)
			end_code= opt(proc_ctx, tag, arg);
		else {
			LOGE("Unknown option\n");
			end_code= STAT_ENOTFOUND;
		}
	}

	ASSERT(pthread_mutex_unlock(&proc_ctx->api_mutex)== 0);
	return end_code;
}

static void* proc_stats_thr(void *t)
{
	proc_stats_thr_arg_t* proc_stats_thr_arg= (proc_stats_thr_arg_t*)t;
	int *ref_end_code= NULL;
	proc_ctx_t *proc_ctx= NULL; // Do not release
	interr_usleep_ctx_t *interr_usleep_ctx= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Allocate return context; initialize to a default 'ERROR' value */
	ref_end_code= (int*)malloc(sizeof(int));
	CHECK_DO(ref_end_code!= NULL, return NULL);
	*ref_end_code= STAT_ERROR;

	/* Check arguments */
	CHECK_DO(proc_stats_thr_arg!= NULL, goto end);

	proc_ctx= proc_stats_thr_arg->proc_ctx;
	CHECK_DO(proc_ctx!= NULL, goto end);

	interr_usleep_ctx= proc_stats_thr_arg->interr_usleep_ctx;
	CHECK_DO(interr_usleep_ctx!= NULL, goto end);

	LOG_CTX_SET(proc_ctx->log_ctx);

	while(proc_ctx->flag_exit== 0) {
		register uint32_t bit_counter_iput, bit_counter_oput;
		register int ret_code;
		pthread_mutex_t *acc_io_bits_mutex_iput_p=
				&proc_ctx->acc_io_bits_mutex[PROC_IPUT];
		pthread_mutex_t *acc_io_bits_mutex_oput_p=
				&proc_ctx->acc_io_bits_mutex[PROC_OPUT];

		/* Note that this is a periodic loop executed once per second
		 * (Thus, 'bitrate' is given in bits per second)
		 */
		ASSERT(pthread_mutex_lock(acc_io_bits_mutex_iput_p)== 0);
		bit_counter_iput= proc_ctx->acc_io_bits[PROC_IPUT];
		proc_ctx->acc_io_bits[PROC_IPUT]= 0;
		ASSERT(pthread_mutex_unlock(acc_io_bits_mutex_iput_p)== 0);
		proc_ctx->bitrate[PROC_IPUT]= bit_counter_iput;

		ASSERT(pthread_mutex_lock(acc_io_bits_mutex_oput_p)== 0);
		bit_counter_oput= proc_ctx->acc_io_bits[PROC_OPUT];
		proc_ctx->acc_io_bits[PROC_OPUT]= 0;
		ASSERT(pthread_mutex_unlock(acc_io_bits_mutex_oput_p)== 0);
		proc_ctx->bitrate[PROC_OPUT]= bit_counter_oput;

		/* Sleep given time (interruptible by external thread) */
		ret_code= interr_usleep(interr_usleep_ctx,
				PROC_STATS_THR_MEASURE_PERIOD_USECS);
		ASSERT(ret_code== STAT_SUCCESS || ret_code== STAT_EINTR);
	}

	*ref_end_code= STAT_SUCCESS;
end:
	return (void*)ref_end_code;
}

static void* proc_thr(void *t)
{
	const proc_if_t *proc_if;
	pthread_t stats_thread;
	proc_ctx_t* proc_ctx= (proc_ctx_t*)t;
	int ret_code, *ref_end_code= NULL;
	int (*process_frame)(proc_ctx_t*, fifo_ctx_t*, fifo_ctx_t*)= NULL;
	fifo_ctx_t *iput_fifo_ctx= NULL, *oput_fifo_ctx= NULL;
	void *thread_end_code= NULL;
	interr_usleep_ctx_t *interr_usleep_ctx= NULL;
	proc_stats_thr_arg_t proc_stats_thr_arg= {0};
	LOG_CTX_INIT(NULL);

	/* Allocate return context; initialize to a default 'ERROR' value */
	ref_end_code= (int*)malloc(sizeof(int));
	CHECK_DO(ref_end_code!= NULL, return NULL);
	*ref_end_code= STAT_ERROR;

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, goto end);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get PROC processing callback */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	process_frame= proc_if->process_frame;
	CHECK_DO(process_frame!= NULL, goto end);

	/* Get input/output FIFO buffers */
	iput_fifo_ctx= proc_ctx->fifo_ctx_array[PROC_IPUT];
	oput_fifo_ctx= proc_ctx->fifo_ctx_array[PROC_OPUT];
	CHECK_DO(iput_fifo_ctx!= NULL && oput_fifo_ctx!= NULL, goto end);

	/* Launch periodical statistics computing thread
	 * (e.g. for computing processor's input/output bitrate statistics):
	 * - Instantiate (open) an interruptible usleep module instance;
	 * - Launch statistic thread passing corresponding argument structure.
	 */
	interr_usleep_ctx= interr_usleep_open();
	CHECK_DO(interr_usleep_ctx!= NULL, goto end);
	proc_stats_thr_arg.proc_ctx= proc_ctx;
	proc_stats_thr_arg.interr_usleep_ctx= interr_usleep_ctx;
	ret_code= pthread_create(&stats_thread, NULL, proc_stats_thr,
			(void*)&proc_stats_thr_arg);
	CHECK_DO(ret_code== 0, goto end);

	/* Run processing thread */
	while(proc_ctx->flag_exit== 0) {
		ret_code= process_frame(proc_ctx, iput_fifo_ctx, oput_fifo_ctx);
		if(ret_code== STAT_EOF)
			proc_ctx->flag_exit= 1;
		else if(ret_code!= STAT_SUCCESS)
			schedule(); // Avoid CPU-consuming closed loops
	}

	*ref_end_code= STAT_SUCCESS;
end:
	/* Join periodical statistics thread:
	 * - Unlock interruptible usleep module instance;
	 * - Join the statistics thread;
	 * - Release (close) the interruptible usleep module instance.
	 */
	interr_usleep_unblock(interr_usleep_ctx);
	pthread_join(stats_thread, &thread_end_code);
	if(thread_end_code!= NULL) {
		ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
		free(thread_end_code);
		thread_end_code= NULL;
	}
	interr_usleep_close(&interr_usleep_ctx);
	return (void*)ref_end_code;
}
