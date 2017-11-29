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
 * @file proc.c
 * @author Rafael Antoniello
 */

#include "proc.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include <libcjson/cJSON.h>

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

/* **** Prototypes **** */

static int procs_id_get(proc_ctx_t *proc_ctx, log_ctx_t *log_ctx,
		proc_if_rest_fmt_t rest_fmt, void **ref_reponse);

static void* proc_stats_thr(void *t);
static void* proc_thr(void *t);

static void proc_stats_register_frame_pts(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx, const proc_io_t proc_io);
static void proc_stats_register_accumulated_io_bits(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx, const proc_io_t proc_io);

/* **** Implementations **** */

proc_ctx_t* proc_open(const proc_if_t *proc_if, const char *settings_str,
		int proc_instance_index, uint32_t fifo_ctx_maxsize[PROC_IO_NUM],
		log_ctx_t *log_ctx, va_list arg)
{
	uint64_t flag_proc_features;
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

	proc_ctx->fifo_ctx_array[PROC_IPUT]= fifo_open(fifo_ctx_maxsize[PROC_IPUT],
			0/*unlimited chunk size*/, 0, &fifo_elem_alloc_fxn);
	CHECK_DO(proc_ctx->fifo_ctx_array[PROC_IPUT]!= NULL, goto end);

	/* Initialize output FIFO buffer */
	fifo_elem_alloc_fxn.elem_ctx_dup=
			proc_if->oput_fifo_elem_opaque_dup!= NULL?
			(fifo_elem_ctx_dup_fxn_t*)proc_if->oput_fifo_elem_opaque_dup:
			(fifo_elem_ctx_dup_fxn_t*)proc_frame_ctx_dup;
	fifo_elem_alloc_fxn.elem_ctx_release=
			(fifo_elem_ctx_release_fxn_t*)proc_frame_ctx_release;
	proc_ctx->fifo_ctx_array[PROC_OPUT]= fifo_open(fifo_ctx_maxsize[PROC_OPUT],
			0/*unlimited chunk size*/, 0, &fifo_elem_alloc_fxn);
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

	/* Initialize array registering input presentation time-stamps (PTS's) */
	proc_ctx->iput_pts_array_idx= 0;
	memset(proc_ctx->iput_pts_array, -1, sizeof(proc_ctx->iput_pts_array));

	/* Initialize latency measurement related variables */
	proc_ctx->acc_latency_nsec= 0;
	proc_ctx->acc_latency_cnt= 0;
	ret_code= pthread_mutex_init(&proc_ctx->latency_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	/* Launch statistics thread if applicable */
	flag_proc_features= proc_if->flag_proc_features;
	if(flag_proc_features& (PROC_FEATURE_BITRATE|PROC_FEATURE_REGISTER_PTS|
			PROC_FEATURE_LATENCY)) {
		/* Launch periodical statistics computing thread
		 * (e.g. for computing processor's input/output bitrate statistics):
		 * - Instantiate (open) an interruptible usleep module instance;
		 * - Launch statistic thread passing corresponding argument structure.
		 */
		proc_ctx->interr_usleep_ctx= interr_usleep_open();
		CHECK_DO(proc_ctx->interr_usleep_ctx!= NULL, goto end);
		ret_code= pthread_create(&proc_ctx->stats_thread, NULL, proc_stats_thr,
				(void*)proc_ctx);
		CHECK_DO(ret_code== 0, goto end);
	}

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
		int (*unblock)(proc_ctx_t *proc_ctx)= NULL;
		const proc_if_t *proc_if= proc_ctx->proc_if;
		void *thread_end_code= NULL;
		LOG_CTX_SET(proc_ctx->log_ctx);

		/* Join processing thread first
		 * - set flag to notify we are exiting processing;
		 * - unlock input/output FIFO's and unblock processor;
		 * - join thread.
		 */
		proc_ctx->flag_exit= 1;
		fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 0);
		fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 0);
		if(proc_if!= NULL && (unblock= proc_if->unblock)!= NULL) {
			ASSERT(unblock(proc_ctx));
		}
		LOGD("Waiting processor thread to join... "); // comment-me
		pthread_join(proc_ctx->proc_thread, &thread_end_code);
		if(thread_end_code!= NULL) {
			ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
			free(thread_end_code);
			thread_end_code= NULL;
		}
		LOGD("joined O.K; "
				"Waiting statistics thread to join... "); // comment-me
		/* Join periodical statistics thread:
		 * - Unlock interruptible usleep module instance;
		 * - Join the statistics thread;
		 * - Release (close) the interruptible usleep module instance.
		 */
		interr_usleep_unblock(proc_ctx->interr_usleep_ctx);
		pthread_join(proc_ctx->stats_thread, &thread_end_code);
		if(thread_end_code!= NULL) {
			ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
			free(thread_end_code);
			thread_end_code= NULL;
		}
		interr_usleep_close(&proc_ctx->interr_usleep_ctx);
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

		/* Release latency measurement related variables */
		ASSERT(pthread_mutex_destroy(&proc_ctx->latency_mutex)== 0);

		/* Close the specific PROC instance */
		CHECK_DO(proc_if!= NULL, return); // sanity check
		CHECK_DO(proc_if->close!= NULL, return); // sanity check
		proc_if->close(ref_proc_ctx);
	}
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

int proc_send_frame(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx)
{
	const proc_if_t *proc_if;
	int end_code;
	int (*send_frame)(proc_ctx_t*, const proc_frame_ctx_t*)= NULL;
	fair_lock_t *fair_lock_p= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//CHECK_DO(proc_frame_ctx!= NULL,
	//		return STAT_ERROR); // Bypassed by this function

	/* Get required variables from PROC interface structure */
	LOG_CTX_SET(proc_ctx->log_ctx);

	fair_lock_p= proc_ctx->fair_lock_io_array[PROC_IPUT];
	CHECK_DO(fair_lock_p!= NULL, return STAT_ERROR);

	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, return STAT_ERROR);

	/* Send frame to processor
	 * (perform within input interface critical section).
	 */
	end_code= STAT_ENOTFOUND;
	if((send_frame= proc_if->send_frame)!= NULL) {
		fair_lock(fair_lock_p);
		end_code= send_frame(proc_ctx, proc_frame_ctx);
		fair_unlock(fair_lock_p);
	}

	return end_code;
}

int proc_recv_frame(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx)
{
	const proc_if_t *proc_if;
	int ret_code, end_code= STAT_ERROR;
	int (*recv_frame)(proc_ctx_t*, proc_frame_ctx_t**)= NULL;
	fair_lock_t *fair_lock_p= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_proc_frame_ctx!= NULL, return STAT_ERROR);

	*ref_proc_frame_ctx= NULL;

	/* Get required variables from PROC interface structure */
	LOG_CTX_SET(proc_ctx->log_ctx);

	fair_lock_p= proc_ctx->fair_lock_io_array[PROC_OPUT];
	CHECK_DO(fair_lock_p!= NULL, goto end);

	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);

	/* Receive frame from processor
	 * (perform within output interface critical section).
	 */
	ret_code= STAT_ENOTFOUND;
	if((recv_frame= proc_if->recv_frame)!= NULL) {
		fair_lock(fair_lock_p);
		ret_code= recv_frame(proc_ctx, ref_proc_frame_ctx);
		fair_unlock(fair_lock_p);
	}
	if(ret_code!= STAT_SUCCESS) {
		end_code= ret_code;
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
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
	int (*unblock)(proc_ctx_t *proc_ctx)= NULL;
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
		if(proc_if!= NULL && (unblock= proc_if->unblock)!= NULL)
			end_code= unblock(proc_ctx);
	} else if(TAG_IS("PROC_GET")) {
		proc_if_rest_fmt_t rest_fmt= va_arg(arg, proc_if_rest_fmt_t);
		void **ref_reponse= va_arg(arg, void**);
		end_code= procs_id_get(proc_ctx, LOG_CTX_GET(), rest_fmt, ref_reponse);
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

int proc_send_frame_default1(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx)
{
	register uint64_t flag_proc_features;
	const proc_if_t *proc_if;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//CHECK_DO(proc_frame_ctx!= NULL, return STAT_ERROR); // bypassed

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, return STAT_ERROR);
	flag_proc_features= proc_if->flag_proc_features;

	/* Check if input PTS statistics are used by this processor */
	if((flag_proc_features& PROC_FEATURE_REGISTER_PTS) &&
			(flag_proc_features&PROC_FEATURE_LATENCY))
		proc_stats_register_frame_pts(proc_ctx, proc_frame_ctx, PROC_IPUT);

	/* Treat bitrate statistics if applicable.
	 * For most processors implementation (specially for encoders and
	 * decoders), measuring traffic at this point would be precise.
	 * Nevertheless, for certain processors, as is the case of demultiplexers,
	 * this function ('proc_send_frame()') is not used thus input bitrate
	 * should be measured at some other point of the specific implementation
	 * (e.g. when receiving data from an INET socket).
	 */
	if(flag_proc_features& PROC_FEATURE_BITRATE)
		proc_stats_register_accumulated_io_bits(proc_ctx, proc_frame_ctx,
				PROC_IPUT);

	/* Write frame to input FIFO */
	return fifo_put_dup(proc_ctx->fifo_ctx_array[PROC_IPUT], proc_frame_ctx,
			sizeof(void*));
}

int proc_recv_frame_default1(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx)
{
	register uint64_t flag_proc_features;
	const proc_if_t *proc_if;
	int ret_code, end_code= STAT_ERROR;
	size_t fifo_elem_size= 0;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(ref_proc_frame_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	*ref_proc_frame_ctx= NULL;

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	flag_proc_features= proc_if->flag_proc_features;

	/* Read a frame from the output FIFO */
	ret_code= fifo_get(proc_ctx->fifo_ctx_array[PROC_OPUT],
			(void**)ref_proc_frame_ctx, &fifo_elem_size);
	if(ret_code!= STAT_SUCCESS) {
		end_code= ret_code;
		goto end;
	}

	/* Treat bitrate statistics if applicable.
	 * For most processors implementation (specially for encoders and
	 * decoders), measuring traffic at this point would be precise.
	 * Nevertheless, for certain processors, as is the case of multiplexers,
	 * this function ('proc_recv_frame()') is not used thus input bitrate
	 * should be measured at some other point of the specific implementation
	 * (e.g. when sending data to an INET socket).
	 */
	if(flag_proc_features& PROC_FEATURE_BITRATE)
		proc_stats_register_accumulated_io_bits(proc_ctx, *ref_proc_frame_ctx,
				PROC_OPUT);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		proc_frame_ctx_release(ref_proc_frame_ctx);
	return end_code;
}

void proc_stats_register_accumulated_latency(proc_ctx_t *proc_ctx,
		const int64_t oput_frame_pts)
{
	register int i, idx;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return);
	if(oput_frame_pts<= 0)
		return;

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Parse input circular PTS register.
	 * Note that we do not care if this register is modified
	 * concurrently in function 'proc_stats_register_frame_pts()'...
	 * this measurement is still valid.
	 */
	for(i= 0, idx= proc_ctx->iput_pts_array_idx; i< IPUT_PTS_ARRAY_SIZE;
			i++, idx= (idx+ 1)% IPUT_PTS_ARRAY_SIZE) {
		int64_t pts_iput= proc_ctx->iput_pts_array[IPUT_PTS_VAL][idx];

		if(pts_iput== oput_frame_pts) {
			register int ret_code;
			register int64_t curr_nsec, iput_nsec;
			struct timespec monotime_curr= {0};

			ret_code= clock_gettime(CLOCK_MONOTONIC, &monotime_curr);
			CHECK_DO(ret_code== 0, return);
			curr_nsec= (int64_t)monotime_curr.tv_sec*1000000000+
					(int64_t)monotime_curr.tv_nsec;
			iput_nsec= proc_ctx->iput_pts_array[IPUT_PTS_STC_VAL][idx];
			if(curr_nsec> iput_nsec) {
				pthread_mutex_t *latency_mutex_p= &proc_ctx->latency_mutex;
				ASSERT((pthread_mutex_lock(latency_mutex_p))== 0);
				proc_ctx->acc_latency_nsec+= curr_nsec- iput_nsec;
				proc_ctx->acc_latency_cnt++;
				//LOGV("acc_latency_nsec= %"PRId64" (count: %d)\n",
				//		proc_ctx->acc_latency_nsec,
				//		proc_ctx->acc_latency_cnt); //comment-me
				ASSERT((pthread_mutex_unlock(latency_mutex_p))== 0);
			}
			return;
		}
	}
	return;
}

static int procs_id_get(proc_ctx_t *proc_ctx, log_ctx_t *log_ctx,
		proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	const proc_if_t *proc_if;
	uint64_t flag_proc_features;
	int ret_code, end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	int (*rest_get)(proc_ctx_t *proc_ctx, proc_if_rest_fmt_t rest_fmt,
			void **ref_reponse)= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//log_ctx allowed to be NULL
	CHECK_DO(rest_fmt>= 0 && rest_fmt< PROC_IF_REST_FMT_ENUM_MAX,
			return STAT_ERROR);
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	*ref_reponse= NULL;

	/* Check that processor API critical section is locked */
	ret_code= pthread_mutex_trylock(&proc_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, goto end);

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	rest_get= proc_if->rest_get;
	flag_proc_features= proc_if->flag_proc_features;

	/* Check if GET function callback is implemented by specific processor */
	if(rest_get== NULL) {
		/* Nothing to do */
		end_code= STAT_ENOTFOUND;
		goto end;
	}

	/* GET processor's REST response */
	ret_code= rest_get(proc_ctx, PROC_IF_REST_FMT_CJSON, (void**)&cjson_rest);
	if(ret_code!= STAT_SUCCESS) {
		end_code= ret_code;
		goto end;
	}
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* **** Add some REST elements at top ****
	 * We do a little HACK to insert elements at top as cJSON library does not
	 * support it natively -it always insert at the bottom-
	 * We do this at the risk of braking in a future library version, as we
	 * base current solution on the internal implementation of function
	 * 'cJSON_AddItemToObject()' -may change in future-.
	 */

	if(flag_proc_features&PROC_FEATURE_LATENCY) {
		/* 'latency_avg_usec' */
		cjson_aux= cJSON_CreateNumber((double)proc_ctx->latency_avg_usec);
		CHECK_DO(cjson_aux!= NULL, goto end);
		// Hack of 'cJSON_AddItemToObject(cjson_rest, "latency_avg_usec",
		// 		cjson_aux);':
		cjson_aux->string= (char*)strdup("latency_avg_usec");
		cjson_aux->type|= cJSON_StringIsConst;
		//cJSON_AddItemToArray(cjson_rest, cjson_aux);
		cJSON_InsertItemInArray(cjson_rest, 0, cjson_aux); // Insert at top
		cjson_aux->type&= ~cJSON_StringIsConst;
	}

	/* Format response to be returned */
	switch(rest_fmt) {
	case PROC_IF_REST_FMT_CHAR:
		/* Print cJSON structure data to char string */
		*ref_reponse= (void*)CJSON_PRINT(cjson_rest);
		CHECK_DO(*ref_reponse!= NULL && strlen((char*)*ref_reponse)> 0,
				goto end);
		break;
	case PROC_IF_REST_FMT_CJSON:
		*ref_reponse= (void*)cjson_rest;
		cjson_rest= NULL; // Avoid double referencing
		break;
	default:
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

static void* proc_stats_thr(void *t)
{
	const proc_if_t *proc_if;
	uint64_t flag_proc_features;
	proc_ctx_t *proc_ctx= (proc_ctx_t*)t;
	int *ref_end_code= NULL;
	interr_usleep_ctx_t *interr_usleep_ctx= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Allocate return context; initialize to a default 'ERROR' value */
	ref_end_code= (int*)malloc(sizeof(int));
	CHECK_DO(ref_end_code!= NULL, return NULL);
	*ref_end_code= STAT_ERROR;

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, goto end);

	interr_usleep_ctx= proc_ctx->interr_usleep_ctx;
	CHECK_DO(interr_usleep_ctx!= NULL, goto end);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	flag_proc_features= proc_if->flag_proc_features;

	while(proc_ctx->flag_exit== 0) {
		register int ret_code;

		/* Bitrate statistics */
		if(flag_proc_features& PROC_FEATURE_BITRATE) {
			register uint32_t bit_counter_iput, bit_counter_oput;
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
		}

		/* Check if latency statistics are used by this processor */
		if(flag_proc_features&PROC_FEATURE_LATENCY) {
			register int acc_latency_cnt;
			register int64_t acc_latency_usec= 0;
			pthread_mutex_t *latency_mutex_p= &proc_ctx->latency_mutex;

			ASSERT((pthread_mutex_lock(latency_mutex_p))== 0);
			acc_latency_cnt= proc_ctx->acc_latency_cnt;
			proc_ctx->acc_latency_cnt= 0;
			if(acc_latency_cnt> 0)
				acc_latency_usec= proc_ctx->acc_latency_nsec/ acc_latency_cnt;
			proc_ctx->acc_latency_nsec= 0;
			ASSERT((pthread_mutex_unlock(latency_mutex_p))== 0);
			acc_latency_usec/= 1000; // convert nsec-> usec
			proc_ctx->latency_avg_usec= acc_latency_usec;
			//LOGV("----> Average latency (usec/sec)= %"PRId64"\n",
			//		acc_latency_usec); //comment-me
			if(acc_latency_usec> proc_ctx->latency_max_usec)
				proc_ctx->latency_max_usec= acc_latency_usec;
			if(proc_ctx->latency_min_usec<= 0)
				proc_ctx->latency_min_usec= acc_latency_usec;
			if(acc_latency_usec> 0 &&
					acc_latency_usec< proc_ctx->latency_min_usec)
				proc_ctx->latency_min_usec= acc_latency_usec;
		}

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
	proc_ctx_t* proc_ctx= (proc_ctx_t*)t;
	int ret_code, *ref_end_code= NULL;
	int (*process_frame)(proc_ctx_t*, fifo_ctx_t*, fifo_ctx_t*)= NULL;
	fifo_ctx_t *iput_fifo_ctx= NULL, *oput_fifo_ctx= NULL;
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
	return (void*)ref_end_code;
}

/**
 * Register frame presentation time stamp (PTS).
 */
static void proc_stats_register_frame_pts(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx, const proc_io_t proc_io)
{
	register int64_t curr_nsec;
	struct timespec monotime_curr= {0};
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return);
	CHECK_DO(proc_frame_ctx!= NULL, return);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Register input PTS */
	proc_ctx->iput_pts_array[IPUT_PTS_VAL][proc_ctx->iput_pts_array_idx]=
			proc_frame_ctx->pts;

	/* Register STC value corresponding to the input PTS */
    CHECK_DO(clock_gettime(CLOCK_MONOTONIC, &monotime_curr)== 0, return);
    curr_nsec= (int64_t)monotime_curr.tv_sec*1000000000+
    		(int64_t)monotime_curr.tv_nsec;
    proc_ctx->iput_pts_array[IPUT_PTS_STC_VAL][proc_ctx->iput_pts_array_idx]=
    		curr_nsec;

	/* Update array index */
	proc_ctx->iput_pts_array_idx= (proc_ctx->iput_pts_array_idx+ 1)%
			IPUT_PTS_ARRAY_SIZE;

	return;
}

/**
 * Register accumulated input/output bits.
 * This is used to compute bitrate statistics.
 */
static void proc_stats_register_accumulated_io_bits(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx, const proc_io_t proc_io)
{
	register uint32_t byte_cnt, bit_cnt;
	register size_t width;
	register int i;
	pthread_mutex_t *acc_io_bits_mutex_p= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return);
	CHECK_DO(proc_frame_ctx!= NULL, return);

	LOG_CTX_SET(proc_ctx->log_ctx);

	acc_io_bits_mutex_p= &proc_ctx->acc_io_bits_mutex[proc_io];

	/* Get frame size (we "unroll" for the most common cases) */
	byte_cnt= (proc_frame_ctx->width[0]* proc_frame_ctx->height[0])+
			(proc_frame_ctx->width[1]* proc_frame_ctx->height[1])+
			(proc_frame_ctx->width[2]* proc_frame_ctx->height[2]);
	// We do the rest in a loop...
	for(i= 3; (width= proc_frame_ctx->width[i])> 0 &&
			i< PROC_FRAME_NUM_DATA_POINTERS; i++)
		byte_cnt+= width* proc_frame_ctx->height[i];

	/* Update currently accumulated bit value */
	bit_cnt= (byte_cnt<< 3);
	ASSERT(pthread_mutex_lock(acc_io_bits_mutex_p)== 0);
	proc_ctx->acc_io_bits[proc_io]+= bit_cnt;
	ASSERT(pthread_mutex_unlock(acc_io_bits_mutex_p)== 0);

	return;
}
