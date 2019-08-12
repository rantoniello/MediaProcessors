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
 * @file utests_procs.cpp
 * @brief Processors (PROCS) module unit testing.
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
#include <libmediaprocsutils/uri_parser.h>

#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/procs.h>
#include <libmediaprocs/proc.h>
}

/* **** Define a very simple bypass processor **** */

static void bypass_proc_close(proc_ctx_t **ref_proc_ctx);
static int bypass_proc_rest_put(proc_ctx_t *proc_ctx, const char *str);

typedef struct bypass_proc_ctx_s {
	/* Generic processor context structure, defined always as the first member
	 * to be able to cast 'bypass_proc_ctx_t' to 'proc_ctx_t'.
	 */
	proc_ctx_s proc_ctx;
	/**
	 * Just a variable extending proc_ctx_s
	 */
	int setting1;
} bypass_proc_ctx_t;

static proc_ctx_t* bypass_proc_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	bypass_proc_ctx_t *bypass_proc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* CHeck arguments */
	if(proc_if== NULL || settings_str== NULL)
		return NULL;

	/* Allocate processor context structure */
	bypass_proc_ctx= (bypass_proc_ctx_t*)calloc(1, sizeof(bypass_proc_ctx_t));
	if(bypass_proc_ctx== NULL)
		goto end;

	/* Copy initial settings */
	ret_code= bypass_proc_rest_put((proc_ctx_t*)bypass_proc_ctx, settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		bypass_proc_close((proc_ctx_t**)&bypass_proc_ctx);
	return (proc_ctx_t*)bypass_proc_ctx;
}

static void bypass_proc_close(proc_ctx_t **ref_proc_ctx)
{
	proc_ctx_t *proc_ctx= NULL;

	if(ref_proc_ctx== NULL)
		return;

	if((proc_ctx= *ref_proc_ctx)!= NULL) {
		free(proc_ctx);
		*ref_proc_ctx= NULL;
	}
}

static int bypass_proc_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	char *setting1_str= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_EINVAL);

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** Parse RESTful string to get settings parameters **** */

	if(flag_is_query== 1) {

		/* 'setting1' */
		setting1_str= uri_parser_query_str_get_value("setting1", str);
		if(setting1_str!= NULL)
			((bypass_proc_ctx_t*)proc_ctx)->setting1= atoll(setting1_str);

	} else {

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		/* 'setting1' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "setting1");
		if(cjson_aux!= NULL)
			((bypass_proc_ctx_t*)proc_ctx)->setting1= cjson_aux->valuedouble;
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(setting1_str!= NULL)
		free(setting1_str);
	return end_code;
}

static int bypass_proc_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL, *cjson_settings= NULL, *cjson_aux= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	if(proc_ctx== NULL || ref_reponse== NULL)
		return STAT_ERROR;

	*ref_reponse= NULL;

	/* JSON string to be returned:
	 * {
	 *     "settings":
	 *     {
	 *         "setting1":string
	 *     }
	 * }
	 */

	/* Create cJSON tree-root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* 'settings' */
	cjson_settings= cJSON_CreateObject();
	CHECK_DO(cjson_settings!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);

	/* 'setting1' */
	cjson_aux= cJSON_CreateNumber((double)
			((bypass_proc_ctx_t*)proc_ctx)->setting1);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_settings, "setting1", cjson_aux);

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

static int bypass_proc_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *fifo_ctx_iput, fifo_ctx_t *fifo_ctx_oput)
{
	int ret_code, end_code= STAT_ERROR;
	size_t fifo_elem_size= 0;
	proc_frame_ctx_t *proc_frame_ctx= NULL;

	/* Just "bypass" frame from input to output */
	ret_code= fifo_get(proc_ctx->fifo_ctx_array[PROC_IPUT],
			(void**)&proc_frame_ctx, &fifo_elem_size);
	CHECK(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN);
	if(ret_code!= STAT_SUCCESS) {
		end_code= ret_code;
		goto end;
	}

	ret_code= fifo_put_dup(proc_ctx->fifo_ctx_array[PROC_OPUT],
			proc_frame_ctx, sizeof(void*));
	CHECK(ret_code== STAT_SUCCESS || ret_code== STAT_ENOMEM);

	end_code= STAT_SUCCESS;
end:
	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return end_code;
}

SUITE(UTESTS_PROCS)
{
	TEST(REGISTER_UNREGISTER_PROC_IF)
	{
		int ret_code;
		const proc_if_t proc_if_bypass_proc= {
			"bypass_processor", "encoder", "application/octet-stream",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};

		ret_code= procs_module_open(NULL);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_bypass_proc);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_UNREGISTER_TYPE", "bypass_processor");
		CHECK(ret_code== STAT_SUCCESS);

		procs_module_close();
	}

	TEST(REGISTER_GET_COPY_PROC_IF)
	{
		int ret_code;
		const proc_if_t proc_if_bypass_proc= {
			"bypass_processor", "encoder", "application/octet-stream",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};
		proc_if_t *proc_if_cpy= NULL;

		ret_code= procs_module_open(NULL);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_bypass_proc);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_GET_TYPE", "bypass_processor",
				&proc_if_cpy);
		CHECK(ret_code== STAT_SUCCESS);

		/* Check duplication */
		CHECK(proc_if_cmp(&proc_if_bypass_proc, proc_if_cpy)== 0);

		ret_code= procs_module_opt("PROCS_UNREGISTER_TYPE", "bypass_processor");
		CHECK(ret_code== STAT_SUCCESS);

		if(proc_if_cpy!= NULL)
			proc_if_release(&proc_if_cpy);
		procs_module_close();
	}

	TEST(POST_DELETE_PROCS)
	{
		int ret_code, proc_id= -1;
		procs_ctx_t *procs_ctx= NULL;
		char *rest_str= NULL;
		cJSON *cjson_rest= NULL, *cjson_aux= NULL;
		const proc_if_t proc_if_bypass_proc= {
			"bypass_processor", "encoder", "application/octet-stream",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};
		LOG_CTX_INIT(NULL);

		ret_code= log_module_open();
		CHECK_DO(ret_code== STAT_SUCCESS, CHECK(false); goto end);

		ret_code= procs_module_open(NULL);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_bypass_proc);
		CHECK(ret_code== STAT_SUCCESS);

		/* Get PROCS module's instance */
		procs_ctx= procs_open(NULL);
		CHECK_DO(procs_ctx!= NULL, CHECK(false); goto end);

		ret_code= procs_opt(procs_ctx, "PROCS_POST", "bypass_processor",
				"setting1=100", &rest_str);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL,
				CHECK(false); goto end);
		//printf("PROCS_POST: '%s'\n", rest_str); fflush(stdout); // comment-me
		cjson_rest= cJSON_Parse(rest_str);
		CHECK_DO(cjson_rest!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		CHECK((proc_id= cjson_aux->valuedouble)>= 0);
		free(rest_str);
		rest_str= NULL;

		ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", proc_id);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_UNREGISTER_TYPE", "bypass_processor");
		CHECK(ret_code== STAT_SUCCESS);

end:
		if(procs_ctx!= NULL)
			procs_close(&procs_ctx);
		procs_module_close();
		if(rest_str!= NULL)
			free(rest_str);
		if(cjson_rest!= NULL)
			cJSON_Delete(cjson_rest);
		log_module_close();
	}

	TEST(GET_PUT_PROCS)
	{
		int ret_code, proc_id= -1;
		procs_ctx_t *procs_ctx= NULL;
		char *rest_str= NULL;
		cJSON *cjson_rest= NULL, *cjson_aux= NULL;
		const proc_if_t proc_if_bypass_proc= {
			"bypass_processor", "encoder", "application/encoder",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};
		const proc_if_t proc_if_bypass_proc2= {
			"bypass_processor2", "encoder2", "application/encoder2",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};
		LOG_CTX_INIT(NULL);

		log_module_open();

		ret_code= procs_module_open(NULL);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_REGISTER_TYPE",
				&proc_if_bypass_proc);
		CHECK(ret_code== STAT_SUCCESS);
		ret_code= procs_module_opt("PROCS_REGISTER_TYPE",
				&proc_if_bypass_proc2);
		CHECK(ret_code== STAT_SUCCESS);

		/* Get PROCS module's instance */
		procs_ctx= procs_open(NULL);
		CHECK_DO(procs_ctx!= NULL, CHECK(false); goto end);

		ret_code= procs_opt(procs_ctx, "PROCS_POST", "bypass_processor",
				"setting1=100", &rest_str);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL,
				CHECK(false); goto end);
		//printf("PROCS_POST: '%s'\n", rest_str); fflush(stdout); // comment-me
		cjson_rest= cJSON_Parse(rest_str);
		CHECK_DO(cjson_rest!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		CHECK((proc_id= cjson_aux->valuedouble)>= 0);
		free(rest_str);
		rest_str= NULL;
		cJSON_Delete(cjson_rest);
		cjson_rest= NULL;

		/* Get setting */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id, &rest_str);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL,
				CHECK(false); goto end);
		//LOGV("GET returns: '%s'\n", rest_str); fflush(stdout); //comment-me
		cjson_rest= cJSON_Parse(rest_str);
		CHECK_DO(cjson_rest!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "settings");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_aux, "setting1");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		CHECK(cjson_aux->valuedouble== 100);
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

		/* Put new setting */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id, "setting1=200");
		CHECK(ret_code== STAT_SUCCESS);

		/* Get setting again */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id, &rest_str);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL,
				CHECK(false); goto end);
		//printf("GET returns: '%s'\n", rest_str); fflush(stdout); //comment-me
		cjson_rest= cJSON_Parse(rest_str);
		CHECK_DO(cjson_rest!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "settings");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_aux, "setting1");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		CHECK(cjson_aux->valuedouble== 200);
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

		/* Test putting same processor name setting! */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id,
				"proc_name=bypass_processor");
		CHECK(ret_code== STAT_SUCCESS);

		/* Test putting new processor name setting! */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id,
				"proc_name=bypass_processor2");
		CHECK(ret_code== STAT_SUCCESS);

		/* Get setting again and check that 'setting1' is preserved */
		ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id, &rest_str);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL,
				CHECK(false); goto end);
		//printf("GET returns: '%s'\n", rest_str); //comment-me
		cjson_rest= cJSON_Parse(rest_str);
		CHECK_DO(cjson_rest!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "settings");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_aux, "setting1");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		CHECK(cjson_aux->valuedouble== 200);
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

		ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", proc_id);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_UNREGISTER_TYPE", "bypass_processor");
		CHECK(ret_code== STAT_SUCCESS);

end:
		if(procs_ctx!= NULL)
			procs_close(&procs_ctx);
		procs_module_close();
		log_module_close();
		if(rest_str!= NULL)
			free(rest_str);
		if(cjson_rest!= NULL)
			cJSON_Delete(cjson_rest);
	}

	TEST(SEND_RECV_PROCS)
	{
#define FIFO_SIZE 2
		int frame_idx, i, x, y, ret_code, proc_id= -1;
		procs_ctx_t *procs_ctx= NULL;
		char *rest_str= NULL;
		cJSON *cjson_rest= NULL, *cjson_aux= NULL;
		const proc_if_t proc_if_bypass_proc= {
			"bypass_processor", "encoder", "application/octet-stream",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};
		proc_frame_ctx_t *proc_frame_ctx= NULL;
		uint8_t yuv_frame[48]= { // YUV4:2:0 simple data example
			0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Y
			0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, // Y
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, // Y
			0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, // Y
			0x00, 0x01, 0x02, 0x03, // U
			0x04, 0x05, 0x06, 0x07, // U
			0x00, 0x01, 0x02, 0x03, // V
			0x04, 0x05, 0x06, 0x07  // V
		};
		proc_frame_ctx_t proc_frame_ctx_yuv= {0};
		LOG_CTX_INIT(NULL);

		log_module_open();

		/* Initialize YUV frame structure */
		proc_frame_ctx_yuv.data= yuv_frame;
		proc_frame_ctx_yuv.p_data[0]= &yuv_frame[0];  // Y
		proc_frame_ctx_yuv.p_data[1]= &yuv_frame[32]; // U
		proc_frame_ctx_yuv.p_data[2]= &yuv_frame[40]; // V
		proc_frame_ctx_yuv.linesize[0]= proc_frame_ctx_yuv.width[0]= 8;
		proc_frame_ctx_yuv.linesize[1]= proc_frame_ctx_yuv.width[1]= 4;
		proc_frame_ctx_yuv.linesize[2]= proc_frame_ctx_yuv.width[2]= 4;
		proc_frame_ctx_yuv.height[0]= 4;
		proc_frame_ctx_yuv.height[1]= proc_frame_ctx_yuv.height[2]= 2;
		proc_frame_ctx_yuv.proc_sample_fmt= PROC_IF_FMT_UNDEF;
		proc_frame_ctx_yuv.pts= -1;
		proc_frame_ctx_yuv.dts= -1;

		ret_code= procs_module_open(NULL);
		CHECK(ret_code== STAT_SUCCESS);

		ret_code= procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_bypass_proc);
		CHECK(ret_code== STAT_SUCCESS);

		/* Get PROCS module's instance */
		procs_ctx= procs_open(NULL);
		CHECK_DO(procs_ctx!= NULL, CHECK(false); goto end);

		ret_code= procs_opt(procs_ctx, "PROCS_POST", "bypass_processor",
				"setting1=100", &rest_str);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL,
				CHECK(false); goto end);
		//printf("PROCS_POST: '%s'\n", rest_str); fflush(stdout); // comment-me
		cjson_rest= cJSON_Parse(rest_str);
		CHECK_DO(cjson_rest!= NULL, CHECK(false); goto end);
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id");
		CHECK_DO(cjson_aux!= NULL, CHECK(false); goto end);
		CHECK((proc_id= cjson_aux->valuedouble)>= 0);
		free(rest_str);
		rest_str= NULL;

		/* Fill processor input FIFO with two equal YUV frames */
		for(frame_idx= 0; frame_idx< FIFO_SIZE; frame_idx++) {
			ret_code= procs_send_frame(procs_ctx, proc_id, &proc_frame_ctx_yuv);
			CHECK(ret_code== STAT_SUCCESS);
		}

		/* Read previously pushed frames from processor (in this simple test
		 * the processor is just a "bypass").
		 */
		for(frame_idx= 0; frame_idx< FIFO_SIZE; frame_idx++) {
			if(proc_frame_ctx!= NULL)
				proc_frame_ctx_release(&proc_frame_ctx);
			ret_code= procs_recv_frame(procs_ctx, proc_id, &proc_frame_ctx);
			CHECK(ret_code== STAT_SUCCESS);
			CHECK(proc_frame_ctx!= NULL);
			if(proc_frame_ctx== NULL)
				goto end;

			CHECK(proc_frame_ctx->proc_sample_fmt== PROC_IF_FMT_UNDEF);
			CHECK(proc_frame_ctx->pts== -1);
			CHECK(proc_frame_ctx->dts== -1);
			for(i= 0; i< 3/*Num. of data planes*/; i++) {
				for(y= 0; y< (int)proc_frame_ctx->height[i]; y++) {
					for(x= 0; x< (int)proc_frame_ctx->width[i]; x++) {
						int data_coord= x+ y* proc_frame_ctx->linesize[i];
						uint8_t data_val= proc_frame_ctx->p_data[i][data_coord];
						int expected_val= x+ y* proc_frame_ctx_yuv.width[i];
						CHECK(data_val== expected_val);
						//printf("0x%02x ", data_val); // comment-me
					}
					//printf("\n"); // comment-me
				}
			}
		}

end:
		//CHECK(procs_opt(procs_ctx, PROCS_ID_DELETE, proc_id)==
		//		STAT_SUCCESS); // Let function 'procs_close()' do this

		//CHECK(procs_module_opt(PROCS_UNREGISTER_TYPE, "bypass_processor")==
		//		STAT_SUCCESS); // Let function 'procs_module_close()' do this
		if(procs_ctx!= NULL)
			procs_close(&procs_ctx);
		procs_module_close();
		log_module_close();
		if(rest_str!= NULL)
			free(rest_str);
		if(cjson_rest!= NULL)
			cJSON_Delete(cjson_rest);
		proc_frame_ctx_release(&proc_frame_ctx);
#undef FIFO_SIZE
	}
}
