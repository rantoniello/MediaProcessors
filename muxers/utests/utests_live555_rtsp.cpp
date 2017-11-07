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
 * @file utests_live555_rtsp.cpp
 * @brief
 * @author Rafael Antoniello
 */

#include <UnitTest++/UnitTest++.h>

extern "C" {
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/schedule.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/procs.h>
#include <libmediaprocsmuxers/live555_rtsp.h>
}

SUITE(UTESTS_LIVE555_RTSP)
{
#define FRAME_SIZE 2048

	typedef struct thr_ctx_s {
		volatile int flag_exit;
		int dmux_proc_id;
		procs_ctx_t *procs_ctx;
		proc_frame_ctx_t *proc_frame_ctx_template1;
		proc_frame_ctx_t *proc_frame_ctx_template2;
	} thr_ctx_t;

	static void* consumer_thr(void *t)
	{
		int i, ret_code, elem_strem_id_step= -1, elem_strem_id_alt= -1;
		int elementary_streams_cnt= 0;
		proc_frame_ctx_t *proc_frame_ctx= NULL;
		thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
		char *rest_str= NULL;
		cJSON *cjson_rest= NULL, *cjson_es_array= NULL, *cjson_aux= NULL;

		if(thr_ctx== NULL) {
			CHECK(false);
			exit(-1);
		}

		/* Receive first frame from de-multiplexer -PRELUDE-.
		 * The first time we receive data we have to check the elementary stream
		 * Id's. The idea is to use the elementary stream Id's to send each
		 * de-multiplexed frame to the correct decoding sink.
		 * We do this once, the first time we are receiving any frame,
		 * by consulting the de-multiplexer API.
		 */
		ret_code= STAT_EAGAIN;
		while(ret_code!= STAT_SUCCESS && thr_ctx->flag_exit== 0) {
			schedule(); // Avoid closed loops
			ret_code= procs_recv_frame(thr_ctx->procs_ctx,
					thr_ctx->dmux_proc_id, &proc_frame_ctx);
		}
		if(thr_ctx->flag_exit!= 0) {
			proc_frame_ctx_release(&proc_frame_ctx);
			return NULL;
		}
		if(ret_code!= STAT_SUCCESS || proc_frame_ctx== NULL) {
			CHECK(false);
			exit(-1);
		}

		/* Parse elementary streams Id's */
		ret_code= procs_opt(thr_ctx->procs_ctx, "PROCS_ID_GET",
				thr_ctx->dmux_proc_id, &rest_str);
		if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
			CHECK(false);
			exit(-1);
		}
		if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
			CHECK(false);
			exit(-1);
		}
		// Elementary streams objects array
		if((cjson_es_array= cJSON_GetObjectItem(cjson_rest,
				"elementary_streams"))== NULL) {
			CHECK(false);
			exit(-1);
		}
		// Iterate elementary stream objects and find the corresponding Id.
		elementary_streams_cnt= cJSON_GetArraySize(cjson_es_array);
		for(i= 0; i< elementary_streams_cnt; i++) {
			cJSON *cjson_es= cJSON_GetArrayItem(cjson_es_array, i);
			if(cjson_es!= NULL) {
				int elementary_stream_id;
				char *mime;

				/* Get stream Id. */
				cjson_aux= cJSON_GetObjectItem(cjson_es,
						"elementary_stream_id");
				if(cjson_aux== NULL) {
					CHECK(false);
					exit(-1);
				}
				elementary_stream_id= cjson_aux->valueint;

				/* Check MIME type and assign Id. */
				cjson_aux= cJSON_GetObjectItem(cjson_es, "sdp_mimetype");
				if(cjson_aux== NULL) {
					CHECK(false);
					exit(-1);
				}
				mime= cjson_aux->valuestring;
				if(mime!= NULL && strcasecmp("application/step-data",
						mime)== 0)
					elem_strem_id_step= elementary_stream_id;
				else if(mime!= NULL && strcasecmp("application/alternate-data",
						mime)== 0)
					elem_strem_id_alt= elementary_stream_id;
			}
		}
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;
		if(elem_strem_id_alt< 0 || elem_strem_id_alt< 0) {
			CHECK(false);
			exit(-1);
		}

		while(thr_ctx->flag_exit== 0) {

			/* Get frame from de-multiplexer */
			proc_frame_ctx_release(&proc_frame_ctx);
			ret_code= procs_recv_frame(thr_ctx->procs_ctx,
					thr_ctx->dmux_proc_id, &proc_frame_ctx);
			CHECK(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN ||
					ret_code== STAT_ENOTFOUND);

			/* Parse frame if applicable */
			if(proc_frame_ctx!= NULL) {
				int i, val_32b;
				const uint8_t *data_buf= proc_frame_ctx->p_data[0];
				int frame_size= (int)proc_frame_ctx->width[0];
				printf("Got frame!\n"); fflush(stdout); //comment-me

				/* Verify size */
				CHECK(frame_size== FRAME_SIZE);

				// { //comment-me
				//for(i= 0; i< frame_size; i++)
				//	printf("%d ", data_buf[i]);
				//printf("\n"); fflush(stdout);
				// }

				/* Verify frame data content */
				if(proc_frame_ctx->es_id== elem_strem_id_step) {
					/* We should have a "step" function */
					for(i= 0, val_32b= 0; i< frame_size; i+= 4, val_32b++) {
						CHECK(data_buf[i+0]== (((uint32_t)val_32b>>24)& 0xFF));
						CHECK(data_buf[i+1]== (((uint32_t)val_32b>>16)& 0xFF));
						CHECK(data_buf[i+2]== (((uint32_t)val_32b>> 8)& 0xFF));
						CHECK(data_buf[i+3]== (((uint32_t)val_32b>> 0)& 0xFF));
					}
				} else if(proc_frame_ctx->es_id== elem_strem_id_alt) {
					/* We should have an alternate 0x00-0xFF function */
					for(i= 0; i< frame_size; i++)
						CHECK(data_buf[i]== (i&1? 0xFF: 0));
				}
			}
		}
		proc_frame_ctx_release(&proc_frame_ctx);
		return NULL;
	}

	/**
	 * Register (open) a processor instance:
	 * 1.- Register processor with given initial settings if desired,
	 * 2.- Parse JSON-REST response to get processor Id.
	 */
	static void procs_post(procs_ctx_t *procs_ctx, const char *proc_name,
			const char *proc_settings, int *ref_proc_id)
	{
		int ret_code;
		char *rest_str= NULL;
		cJSON *cjson_rest= NULL, *cjson_aux= NULL;

		ret_code= procs_opt(procs_ctx, "PROCS_POST", proc_name, proc_settings,
				&rest_str);
		if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
			CHECK(false);
			exit(-1);
		}
		if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
			CHECK(false);
			exit(-1);
		}
		if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id"))== NULL) {
			CHECK(false);
			exit(-1);
		}
		if((*ref_proc_id= cjson_aux->valuedouble)< 0) {
			CHECK(false);
			exit(-1);
		}
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;
	}

	TEST(UTESTS_LIVE555_RTSP_SERVER)
	{
		pthread_t consumer_thread;
		int i, ret_code, mux_proc_id= -1, dmux_proc_id= -1,
				elem_strem_id_step= -1, elem_strem_id_alt= -1;
		procs_ctx_t *procs_ctx= NULL;
		char *rest_str= NULL;
		cJSON *cjson_rest= NULL, *cjson_aux= NULL, *cjson_aux2= NULL;
		proc_frame_ctx_t proc_frame_ctx_template1= {0};
		proc_frame_ctx_t proc_frame_ctx_template2= {0};
		uint8_t data_buf_template1[FRAME_SIZE];
		uint8_t data_buf_template2[FRAME_SIZE];
		thr_ctx_t thr_ctx= {0};

	    /* Open LOG module */
	    log_module_open();

		/* Open processors (PROCS) module */
		ret_code= procs_module_open(NULL);
		if(ret_code!= STAT_SUCCESS) {
			CHECK(false);
			goto end;
		}

		/* Register multiplexer and de-multiplexer processor types */
		ret_code= procs_module_opt("PROCS_REGISTER_TYPE",
				&proc_if_live555_rtsp_mux);
		if(ret_code!= STAT_SUCCESS) {
			CHECK(false);
			goto end;
		}
		ret_code= procs_module_opt("PROCS_REGISTER_TYPE",
				&proc_if_live555_rtsp_dmux);
		if(ret_code!= STAT_SUCCESS) {
			CHECK(false);
			goto end;
		}

		/* Get PROCS module's instance */
		procs_ctx= procs_open(NULL);
		if(procs_ctx== NULL) {
			CHECK(false);
			goto end;
		}

	    /* Register (open) a multiplexer instance */
		procs_post(procs_ctx, "live555_rtsp_mux", "rtsp_port=8554",
				&mux_proc_id);

	    /* Register an elementary stream for the multiplexer: STEP f() */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_ES_MUX_REGISTER", mux_proc_id,
				"sdp_mimetype=application/step-data", &rest_str);
		if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_aux= cJSON_GetObjectItem(cjson_rest,
				"elementary_stream_id"))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((elem_strem_id_step= cjson_aux->valuedouble)< 0) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

		/* Register an elementary stream for the multiplexer: ALT f() */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_ES_MUX_REGISTER", mux_proc_id,
				"sdp_mimetype=application/alternate-data", &rest_str);
		if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_aux= cJSON_GetObjectItem(cjson_rest,
				"elementary_stream_id"))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((elem_strem_id_alt= cjson_aux->valuedouble)< 0) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

	    /* Register RTSP de-multiplexer instance and get corresponding Id. */
		procs_post(procs_ctx, "live555_rtsp_dmux",
				"rtsp_url=rtsp://127.0.0.1:8554/session", &dmux_proc_id);

		/* Launch consumer thread */
		thr_ctx.flag_exit= 0;
		thr_ctx.procs_ctx= procs_ctx;
		thr_ctx.dmux_proc_id= dmux_proc_id;
		thr_ctx.proc_frame_ctx_template1= &proc_frame_ctx_template1;
		thr_ctx.proc_frame_ctx_template2= &proc_frame_ctx_template2;
		ret_code= pthread_create(&consumer_thread, NULL, consumer_thr,
				&thr_ctx);
		if(ret_code!= 0) {
			CHECK(false);
			goto end;
		}

		/* **** Prepare data frames templates ****
		 * - "template frame 1": step function;
		 * - "template frame 2": alternate 0x00-0xFF function
		 */

		/* "template frame 1": step function */
		proc_frame_ctx_template1.data= data_buf_template1;
		proc_frame_ctx_template1.p_data[0]= data_buf_template1;
		proc_frame_ctx_template1.linesize[0]= FRAME_SIZE;
		proc_frame_ctx_template1.width[0]= FRAME_SIZE;
		proc_frame_ctx_template1.height[0]= 1; // "1D" data
		proc_frame_ctx_template1.pts= 0;
		proc_frame_ctx_template1.es_id= elem_strem_id_step;
		for(int i= 0, val_32b= 0; i< FRAME_SIZE; i+= 4, val_32b++) {
			data_buf_template1[i+ 0]= ((uint32_t)val_32b>> 24)& 0xFF;
			data_buf_template1[i+ 1]= ((uint32_t)val_32b>> 16)& 0xFF;
			data_buf_template1[i+ 2]= ((uint32_t)val_32b>>  8)& 0xFF;
			data_buf_template1[i+ 3]= ((uint32_t)val_32b>>  0)& 0xFF;
		}

		/* "template frame 2": alternate 0x00-0xFF function */
		proc_frame_ctx_template2.data= data_buf_template2;
		proc_frame_ctx_template2.p_data[0]= data_buf_template2;
		proc_frame_ctx_template2.linesize[0]= FRAME_SIZE;
		proc_frame_ctx_template2.width[0]= FRAME_SIZE;
		proc_frame_ctx_template2.height[0]= 1; // "1D" data
		proc_frame_ctx_template2.pts= 0;
		proc_frame_ctx_template2.es_id= elem_strem_id_alt;
		for(int i= 0; i< FRAME_SIZE; i++)
			data_buf_template2[i]= i&1? 0xFF: 0;

		/* Send (multiplex RTSP/RTP) a few frames to the consumer thread */
		for(i= 0; i< 10; i++) {
			/*  the samples */
			CHECK(procs_send_frame(procs_ctx, mux_proc_id,
					&proc_frame_ctx_template1)== STAT_SUCCESS);
			CHECK(procs_send_frame(procs_ctx, mux_proc_id,
					&proc_frame_ctx_template2)== STAT_SUCCESS);
			usleep(1000*10);
		}

	    /* Join the threads */
	    thr_ctx.flag_exit= 1;
	    // Delete processor before joining (to unblock processor).
	    // Close client to avoid O.S. to keep server's ports for 60 secs.
		ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", dmux_proc_id);
		CHECK(ret_code== STAT_SUCCESS);

		/* Send a few frames to the consumer thread  even when previously
		 * deleted (just to test...).
		 */
		for(i= 0; i< 10; i++) {
			/*  the samples */
			CHECK(procs_send_frame(procs_ctx, mux_proc_id,
					&proc_frame_ctx_template1)== STAT_SUCCESS);
			CHECK(procs_send_frame(procs_ctx, mux_proc_id,
					&proc_frame_ctx_template2)== STAT_SUCCESS);
			usleep(1000*10);
		}

		/* Before deleting multiplexer, try to change settings... */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", mux_proc_id,
				"rtsp_streaming_session_name=session2&rtsp_port=1999");
		if(ret_code!= STAT_SUCCESS) {
			CHECK(false);
			goto end;
		}

		/* Check new settings */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", mux_proc_id, &rest_str);
		if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "settings"))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		if((cjson_aux2= cJSON_GetObjectItem(cjson_aux,
				"rtsp_streaming_session_name"))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		CHECK(strcmp(cjson_aux2->valuestring, "session2")== 0);
		if((cjson_aux2= cJSON_GetObjectItem(cjson_aux,
				"rtsp_port"))== NULL) {
			fprintf(stderr, "Error at line: %d\n", __LINE__);
			exit(-1);
		}
		CHECK(cjson_aux2->valueint== 1999);
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

		/* Delete multiplexer */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", mux_proc_id);
		CHECK(ret_code== STAT_SUCCESS);
		pthread_join(consumer_thread, NULL);

end:
		if(procs_ctx!= NULL)
			procs_close(&procs_ctx);
		procs_module_close();
		log_module_close();
		if(rest_str!= NULL)
			free(rest_str);
		return;
	}
}
