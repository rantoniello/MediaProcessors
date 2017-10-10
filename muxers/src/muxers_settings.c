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
 * @file muxers_settings.c
 * @author Rafael Antoniello
 */

#include "muxers_settings.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/uri_parser.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocs/proc_if.h>

muxers_settings_mux_ctx_t* muxers_settings_mux_ctx_allocate()
{
	return (muxers_settings_mux_ctx_t*)calloc(1, sizeof(
			muxers_settings_mux_ctx_t));
}

void muxers_settings_mux_ctx_release(
		muxers_settings_mux_ctx_t **ref_muxers_settings_mux_ctx)
{
	muxers_settings_mux_ctx_t *muxers_settings_mux_ctx= NULL;

	if(ref_muxers_settings_mux_ctx== NULL)
		return;

	if((muxers_settings_mux_ctx= *ref_muxers_settings_mux_ctx)!= NULL) {

		free(muxers_settings_mux_ctx);
		*ref_muxers_settings_mux_ctx= NULL;
	}
}

int muxers_settings_mux_ctx_init(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(muxers_settings_mux_ctx!= NULL, return STAT_ERROR);

	muxers_settings_mux_ctx->rtsp_port= 8554;
	muxers_settings_mux_ctx->time_stamp_freq= 9000; // [Hz]
	muxers_settings_mux_ctx->rtsp_streaming_session_name= strdup("session");
	CHECK_DO(muxers_settings_mux_ctx->rtsp_streaming_session_name!= NULL,
			return STAT_ERROR);

	return STAT_SUCCESS;
}

void muxers_settings_mux_ctx_deinit(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx)
{
	if(muxers_settings_mux_ctx== NULL)
		return;

	if(muxers_settings_mux_ctx->rtsp_streaming_session_name!= NULL) {
		free(muxers_settings_mux_ctx->rtsp_streaming_session_name);
		muxers_settings_mux_ctx->rtsp_streaming_session_name= NULL;
	}

	// Reserved for future use
	// Release here heap-allocated members of the structure.
}

int muxers_settings_mux_ctx_cpy(
		const muxers_settings_mux_ctx_t *muxers_settings_mux_ctx_src,
		muxers_settings_mux_ctx_t *muxers_settings_mux_ctx_dst)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(muxers_settings_mux_ctx_src!= NULL, return STAT_ERROR);
	CHECK_DO(muxers_settings_mux_ctx_dst!= NULL, return STAT_ERROR);

	/* Copy simple variable values */
	memcpy(muxers_settings_mux_ctx_dst, muxers_settings_mux_ctx_src,
			sizeof(muxers_settings_mux_ctx_t));

	// Future use: duplicate heap-allocated variables...
	muxers_settings_mux_ctx_dst->rtsp_streaming_session_name= strdup(
			muxers_settings_mux_ctx_src->rtsp_streaming_session_name);
	CHECK_DO(muxers_settings_mux_ctx_dst->rtsp_streaming_session_name!= NULL,
			return STAT_ERROR);

	return STAT_SUCCESS;
}

int muxers_settings_mux_ctx_restful_put(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx,
		const char *str, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	char *rtsp_port_str= NULL, *bit_rate_estimated_str= NULL,
			*time_stamp_freq_str= NULL, *rtsp_streaming_session_name_str= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(muxers_settings_mux_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_EINVAL);

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** Parse RESTful string to get settings parameters **** */

	if(flag_is_query== 1) {

		/* 'rtsp_port' */
		rtsp_port_str= uri_parser_query_str_get_value("rtsp_port", str);
		if(rtsp_port_str!= NULL)
			muxers_settings_mux_ctx->rtsp_port= atoll(rtsp_port_str);

		/* 'time_stamp_freq' */
		time_stamp_freq_str= uri_parser_query_str_get_value("time_stamp_freq",
				str);
		if(time_stamp_freq_str!= NULL)
			muxers_settings_mux_ctx->time_stamp_freq= atoll(
					time_stamp_freq_str);

		/* 'rtsp_streaming_session_name' */
		rtsp_streaming_session_name_str= uri_parser_query_str_get_value(
				"rtsp_streaming_session_name", str);
		if(rtsp_streaming_session_name_str!= NULL) {
			char *rtsp_streaming_session_name;
			CHECK_DO(strlen(rtsp_streaming_session_name_str)> 0,
					end_code= STAT_EINVAL; goto end);

			/* Allocate new session name */
			rtsp_streaming_session_name= strdup(
					rtsp_streaming_session_name_str);
			CHECK_DO(rtsp_streaming_session_name!= NULL, goto end);

			/* Release old session name and set new one */
			if(muxers_settings_mux_ctx->rtsp_streaming_session_name!= NULL)
				free(muxers_settings_mux_ctx->rtsp_streaming_session_name);
			muxers_settings_mux_ctx->rtsp_streaming_session_name=
					rtsp_streaming_session_name;
		}

	} else {

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		/* 'rtsp_port' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "rtsp_port");
		if(cjson_aux!= NULL)
			muxers_settings_mux_ctx->rtsp_port= cjson_aux->valuedouble;

		/* 'time_stamp_freq' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "time_stamp_freq");
		if(cjson_aux!= NULL)
			muxers_settings_mux_ctx->time_stamp_freq= cjson_aux->valuedouble;

		/* 'rtsp_streaming_session_name' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest,
				"rtsp_streaming_session_name");
		if(cjson_aux!= NULL) {
			char *rtsp_streaming_session_name;
			CHECK_DO(strlen(cjson_aux->valuestring)> 0,
					end_code= STAT_EINVAL; goto end);

			/* Allocate new session name */
			rtsp_streaming_session_name= strdup(cjson_aux->valuestring);
			CHECK_DO(rtsp_streaming_session_name!= NULL, goto end);

			/* Release old session name and set new one */
			if(muxers_settings_mux_ctx->rtsp_streaming_session_name!= NULL)
				free(muxers_settings_mux_ctx->rtsp_streaming_session_name);
			muxers_settings_mux_ctx->rtsp_streaming_session_name=
					rtsp_streaming_session_name;
		}

	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(rtsp_port_str!= NULL)
		free(rtsp_port_str);
	if(bit_rate_estimated_str!= NULL)
		free(bit_rate_estimated_str);
	if(time_stamp_freq_str!= NULL)
		free(time_stamp_freq_str);
	if(rtsp_streaming_session_name_str!= NULL)
		free(rtsp_streaming_session_name_str);
	return end_code;
}

int muxers_settings_mux_ctx_restful_get(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(muxers_settings_mux_ctx!= NULL, goto end);
	CHECK_DO(ref_cjson_rest!= NULL, goto end);

	*ref_cjson_rest= NULL;

	/* Create cJSON tree-root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     "rtsp_port":number,
	 *     "time_stamp_freq":number,
	 *     "rtsp_streaming_session_name":string
	 * }
	 */

	/* 'rtsp_port' */
	cjson_aux= cJSON_CreateNumber((double)muxers_settings_mux_ctx->rtsp_port);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "rtsp_port", cjson_aux);

	/* 'time_stamp_freq' */
	cjson_aux= cJSON_CreateNumber((double)
			muxers_settings_mux_ctx->time_stamp_freq);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "time_stamp_freq", cjson_aux);

	/* 'rtsp_streaming_session_name' */
	cjson_aux= cJSON_CreateString(
			muxers_settings_mux_ctx->rtsp_streaming_session_name);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "rtsp_streaming_session_name", cjson_aux);

	*ref_cjson_rest= cjson_rest;
	cjson_rest= NULL;
	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

muxers_settings_dmux_ctx_t* muxers_settings_dmux_ctx_allocate()
{
	return (muxers_settings_dmux_ctx_t*)calloc(1, sizeof(
			muxers_settings_dmux_ctx_t));
}

void muxers_settings_dmux_ctx_release(
		muxers_settings_dmux_ctx_t **ref_muxers_settings_dmux_ctx)
{
	muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx= NULL;

	if(ref_muxers_settings_dmux_ctx== NULL)
		return;

	if((muxers_settings_dmux_ctx= *ref_muxers_settings_dmux_ctx)!= NULL) {

		if(muxers_settings_dmux_ctx->rtsp_url!= NULL) {
			free(muxers_settings_dmux_ctx->rtsp_url);
			muxers_settings_dmux_ctx->rtsp_url= NULL;
		}

		free(muxers_settings_dmux_ctx);
		*ref_muxers_settings_dmux_ctx= NULL;
	}
}

int muxers_settings_dmux_ctx_init(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(muxers_settings_dmux_ctx!= NULL, return STAT_ERROR);

	muxers_settings_dmux_ctx->rtsp_url= NULL;

	return STAT_SUCCESS;
}

void muxers_settings_dmux_ctx_deinit(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx)
{
	if(muxers_settings_dmux_ctx== NULL)
		return;

	if(muxers_settings_dmux_ctx->rtsp_url!= NULL) {
		free(muxers_settings_dmux_ctx->rtsp_url);
		muxers_settings_dmux_ctx->rtsp_url= NULL;
	}

	// Reserved for future use
	// Release here heap-allocated members of the structure.
}

int muxers_settings_dmux_ctx_cpy(
		const muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx_src,
		muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx_dst)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(muxers_settings_dmux_ctx_src!= NULL, return STAT_ERROR);
	CHECK_DO(muxers_settings_dmux_ctx_dst!= NULL, return STAT_ERROR);

	/* Copy simple variable values */
	memcpy(muxers_settings_dmux_ctx_dst, muxers_settings_dmux_ctx_src,
			sizeof(muxers_settings_dmux_ctx_t));

	// Future use: duplicate heap-allocated variables...
	muxers_settings_dmux_ctx_dst->rtsp_url= strdup(
			muxers_settings_dmux_ctx_src->rtsp_url);
	CHECK_DO(muxers_settings_dmux_ctx_dst->rtsp_url!= NULL, return STAT_ERROR);

	return STAT_SUCCESS;
}

int muxers_settings_dmux_ctx_restful_put(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx,
		const char *str, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	cJSON *cjson_rest= NULL;
	cJSON *cjson_aux= NULL;
	char *rtsp_url_str= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(muxers_settings_dmux_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_EINVAL);

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** Parse RESTful string to get settings parameters **** */

	if(flag_is_query== 1) {

		/* 'rtsp_url' */
		rtsp_url_str= uri_parser_query_str_get_value("rtsp_url", str);
		if(rtsp_url_str!= NULL) {
			char *rtsp_url;
			CHECK_DO(strlen(rtsp_url_str)> 0, end_code= STAT_EINVAL; goto end);

			/* Allocate new session name */
			rtsp_url= strdup(rtsp_url_str);
			CHECK_DO(rtsp_url!= NULL, goto end);

			/* Release old session name and set new one */
			if(muxers_settings_dmux_ctx->rtsp_url!= NULL)
				free(muxers_settings_dmux_ctx->rtsp_url);
			muxers_settings_dmux_ctx->rtsp_url= rtsp_url;
		}

	} else {

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		/* 'rtsp_url' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "rtsp_url");
		if(cjson_aux!= NULL) {
			char *rtsp_url;
			CHECK_DO(strlen(cjson_aux->valuestring)> 0,
					end_code= STAT_EINVAL; goto end);

			/* Allocate new session name */
			rtsp_url= strdup(cjson_aux->valuestring);
			CHECK_DO(rtsp_url!= NULL, goto end);

			/* Release old session name and set new one */
			if(muxers_settings_dmux_ctx->rtsp_url!= NULL)
				free(muxers_settings_dmux_ctx->rtsp_url);
			muxers_settings_dmux_ctx->rtsp_url= rtsp_url;
		}
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(rtsp_url_str!= NULL)
		free(rtsp_url_str);
	return end_code;
}

int muxers_settings_dmux_ctx_restful_get(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL;
	cJSON *cjson_aux= NULL;
	LOG_CTX_INIT(log_ctx);

	CHECK_DO(muxers_settings_dmux_ctx!= NULL, goto end);
	CHECK_DO(ref_cjson_rest!= NULL, goto end);

	*ref_cjson_rest= NULL;

	/* Create cJSON tree-root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     "rtsp_url":string
	 * }
	 */

	/* 'rtsp_url' */
	cjson_aux= cJSON_CreateString(muxers_settings_dmux_ctx->rtsp_url);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "rtsp_url", cjson_aux);

	*ref_cjson_rest= cjson_rest;
	cjson_rest= NULL;
	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}
