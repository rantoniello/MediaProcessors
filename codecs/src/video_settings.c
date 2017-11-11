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
 * @file video_settings.c
 * @author Rafael Antoniello
 */

#include "video_settings.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/uri_parser.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocs/proc_if.h>

video_settings_enc_ctx_t* video_settings_enc_ctx_allocate()
{
	return (video_settings_enc_ctx_t*)calloc(1, sizeof(
			video_settings_enc_ctx_t));
}

void video_settings_enc_ctx_release(
		video_settings_enc_ctx_t **ref_video_settings_enc_ctx)
{
	video_settings_enc_ctx_t *video_settings_enc_ctx= NULL;

	if(ref_video_settings_enc_ctx== NULL)
		return;

	if((video_settings_enc_ctx= *ref_video_settings_enc_ctx)!= NULL) {

		video_settings_enc_ctx_deinit((volatile video_settings_enc_ctx_t*)
				video_settings_enc_ctx);

		free(video_settings_enc_ctx);
		*ref_video_settings_enc_ctx= NULL;
	}
}

int video_settings_enc_ctx_init(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(video_settings_enc_ctx!= NULL, return STAT_ERROR);

	video_settings_enc_ctx->bit_rate_output= 300*1024;
	video_settings_enc_ctx->frame_rate_output= 15;
	video_settings_enc_ctx->width_output= 352;
	video_settings_enc_ctx->height_output= 288;
	video_settings_enc_ctx->gop_size= 15;
	memset((void*)video_settings_enc_ctx->conf_preset, 0,
			sizeof(video_settings_enc_ctx->conf_preset));
	return STAT_SUCCESS;
}

void video_settings_enc_ctx_deinit(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx)
{
	// Reserved for future use
	// Release here heap-allocated members of the structure.
}

int video_settings_enc_ctx_cpy(
		const video_settings_enc_ctx_t *video_settings_enc_ctx_src,
		video_settings_enc_ctx_t *video_settings_enc_ctx_dst)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(video_settings_enc_ctx_src!= NULL, return STAT_ERROR);
	CHECK_DO(video_settings_enc_ctx_dst!= NULL, return STAT_ERROR);

	/* Copy simple variable values */
	memcpy(video_settings_enc_ctx_dst, video_settings_enc_ctx_src,
			sizeof(video_settings_enc_ctx_t));

	// Future use: duplicate heap-allocated variables...

	return STAT_SUCCESS;
}

int video_settings_enc_ctx_restful_put(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx,
		const char *str, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	char *bit_rate_output_str= NULL, *frame_rate_output_str= NULL,
			*width_output_str= NULL, *height_output_str= NULL,
			*gop_size_str= NULL, *sample_fmt_input_str= NULL,
			*profile_str= NULL, *conf_preset_str= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(video_settings_enc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_EINVAL);

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** Parse RESTful string to get settings parameters **** */

	if(flag_is_query== 1) {

		/* 'bit_rate_output' */
		bit_rate_output_str= uri_parser_query_str_get_value("bit_rate_output",
				str);
		if(bit_rate_output_str!= NULL)
			video_settings_enc_ctx->bit_rate_output= atoll(bit_rate_output_str);

		/* 'frame_rate_output' */
		frame_rate_output_str= uri_parser_query_str_get_value(
				"frame_rate_output", str);
		if(frame_rate_output_str!= NULL)
			video_settings_enc_ctx->frame_rate_output=
					atoll(frame_rate_output_str);

		/* 'width_output' */
		width_output_str= uri_parser_query_str_get_value("width_output", str);
		if(width_output_str!= NULL)
			video_settings_enc_ctx->width_output= atoll(width_output_str);

		/* 'height_output' */
		height_output_str= uri_parser_query_str_get_value("height_output", str);
		if(height_output_str!= NULL)
			video_settings_enc_ctx->height_output= atoll(height_output_str);

		/* 'gop_size' */
		gop_size_str= uri_parser_query_str_get_value("gop_size", str);
		if(gop_size_str!= NULL)
			video_settings_enc_ctx->gop_size= atoll(gop_size_str);

		/* 'conf_preset' */
		conf_preset_str= uri_parser_query_str_get_value("conf_preset", str);
		if(conf_preset_str!= NULL) {
			CHECK_DO(strlen(conf_preset_str)<
					(sizeof(video_settings_enc_ctx->conf_preset)- 1),
					end_code= STAT_EINVAL; goto end);
			memcpy((void*)video_settings_enc_ctx->conf_preset, conf_preset_str,
					strlen(conf_preset_str));
			video_settings_enc_ctx->conf_preset[strlen(conf_preset_str)]= 0;
		}

	} else {

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		/* 'bit_rate_output' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "bit_rate_output");
		if(cjson_aux!= NULL)
			video_settings_enc_ctx->bit_rate_output= cjson_aux->valuedouble;

		/* 'frame_rate_output' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "frame_rate_output");
		if(cjson_aux!= NULL)
			video_settings_enc_ctx->frame_rate_output= cjson_aux->valuedouble;

		/* 'width_output' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "width_output");
		if(cjson_aux!= NULL)
			video_settings_enc_ctx->width_output= cjson_aux->valuedouble;

		/* 'height_output' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "height_output");
		if(cjson_aux!= NULL)
			video_settings_enc_ctx->height_output= cjson_aux->valuedouble;

		/* 'gop_size' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "gop_size");
		if(cjson_aux!= NULL)
			video_settings_enc_ctx->gop_size= cjson_aux->valuedouble;

		/* 'conf_preset' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "conf_preset");
		if(cjson_aux!= NULL && cjson_aux->valuestring!= NULL) {
			CHECK_DO(strlen(cjson_aux->valuestring)<
					(sizeof(video_settings_enc_ctx->conf_preset)- 1),
					end_code= STAT_EINVAL; goto end);
			memcpy((void*)video_settings_enc_ctx->conf_preset,
					cjson_aux->valuestring, strlen(cjson_aux->valuestring));
			video_settings_enc_ctx->conf_preset
			[strlen(cjson_aux->valuestring)]= 0;
		}
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(bit_rate_output_str!= NULL)
		free(bit_rate_output_str);
	if(frame_rate_output_str!= NULL)
		free(frame_rate_output_str);
	if(width_output_str!= NULL)
		free(width_output_str);
	if(height_output_str!= NULL)
		free(height_output_str);
	if(gop_size_str!= NULL)
		free(gop_size_str);
	if(sample_fmt_input_str!= NULL)
		free(sample_fmt_input_str);
	if(profile_str!= NULL)
		free(profile_str);
	if(conf_preset_str!= NULL)
		free(conf_preset_str);
	return end_code;
}

int video_settings_enc_ctx_restful_get(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	LOG_CTX_INIT(log_ctx);

	CHECK_DO(video_settings_enc_ctx!= NULL, goto end);
	CHECK_DO(ref_cjson_rest!= NULL, goto end);

	*ref_cjson_rest= NULL;

	/* Create cJSON tree-root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     "bit_rate_output":number,
	 *     "frame_rate_output":number,
	 *     "width_output":number,
	 *     "height_output":number,
	 *     "gop_size":number,
	 *     "conf_preset":string
	 * }
	 */

	/* 'bit_rate_output' */
	cjson_aux= cJSON_CreateNumber((double)
			video_settings_enc_ctx->bit_rate_output);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "bit_rate_output", cjson_aux);

	/* 'frame_rate_output' */
	cjson_aux= cJSON_CreateNumber((double)
			video_settings_enc_ctx->frame_rate_output);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "frame_rate_output", cjson_aux);

	/* 'width_output' */
	cjson_aux= cJSON_CreateNumber((double)video_settings_enc_ctx->width_output);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "width_output", cjson_aux);

	/* 'height_output' */
	cjson_aux= cJSON_CreateNumber((double)
			video_settings_enc_ctx->height_output);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "height_output", cjson_aux);

	/* 'gop_size' */
	cjson_aux= cJSON_CreateNumber((double)video_settings_enc_ctx->gop_size);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "gop_size", cjson_aux);

	/* 'conf_preset' */
	if(video_settings_enc_ctx->conf_preset!= NULL &&
			strlen((const char*)video_settings_enc_ctx->conf_preset)> 0) {
		cjson_aux= cJSON_CreateString((const char*)
				video_settings_enc_ctx->conf_preset);
	} else {
		cjson_aux= cJSON_CreateNull();
	}
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "conf_preset", cjson_aux);

	*ref_cjson_rest= cjson_rest;
	cjson_rest= NULL;
	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

video_settings_dec_ctx_t* video_settings_dec_ctx_allocate()
{
	return (video_settings_dec_ctx_t*)calloc(1, sizeof(
			video_settings_dec_ctx_t));
}

void video_settings_dec_ctx_release(
		video_settings_dec_ctx_t **ref_video_settings_dec_ctx)
{
	video_settings_dec_ctx_t *video_settings_dec_ctx= NULL;

	if(ref_video_settings_dec_ctx== NULL)
		return;

	if((video_settings_dec_ctx= *ref_video_settings_dec_ctx)!= NULL) {

		free(video_settings_dec_ctx);
		*ref_video_settings_dec_ctx= NULL;
	}
}

int video_settings_dec_ctx_init(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(video_settings_dec_ctx!= NULL, return STAT_ERROR);

	// Reserved for future use
	// Initialize here structure members, for example:
	// video_settings_dec_ctx->varX= valueX;

	return STAT_SUCCESS;
}

void video_settings_dec_ctx_deinit(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx)
{
	// Reserved for future use
	// Release here heap-allocated members of the structure.
}

int video_settings_dec_ctx_cpy(
		const video_settings_dec_ctx_t *video_settings_dec_ctx_src,
		video_settings_dec_ctx_t *video_settings_dec_ctx_dst)
{
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(video_settings_dec_ctx_src!= NULL, return STAT_ERROR);
	CHECK_DO(video_settings_dec_ctx_dst!= NULL, return STAT_ERROR);

	// Reserved for future use
	// Copy values of simple variables, duplicate heap-allocated variables.

	return STAT_SUCCESS;
}

int video_settings_dec_ctx_restful_put(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx,
		const char *str, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	cJSON *cjson_rest= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(video_settings_dec_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_EINVAL);

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** Parse RESTful string to get settings parameters **** */

	if(flag_is_query== 1) {

		/* Reserved for future use
		 *
		 * Example: If 'var1' is a number value passed as query-string
		 * '...var1_name=number&...':
		 *
		 * char *var1_str= NULL;
		 * ...
		 * var1_str= uri_parser_query_str_get_value("var1_name", str);
		 * if(var1_str!= NULL)
		 *     video_settings_dec_ctx->var1= atoll(var1_str);
		 * ...
		 * if(var1_str!= NULL)
		 *     free(var1_str);
		 */

	} else {

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		/* Reserved for future use
		 *
		 * Example: If 'var1' is a number value passed as JSON-string
		 * '{..., "var1_name":number, ...}':
		 *
		 * *cjson_aux= NULL;
		 * ...
		 * cjson_aux= cJSON_GetObjectItem(cjson_rest, "var1_name");
		 * if(cjson_aux!= NULL)
		 *     video_settings_dec_ctx->var1= cjson_aux->valuedouble;
		 */
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

int video_settings_dec_ctx_restful_get(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	cJSON *cjson_rest= NULL;
	LOG_CTX_INIT(log_ctx);

	CHECK_DO(video_settings_dec_ctx!= NULL, goto end);
	CHECK_DO(ref_cjson_rest!= NULL, goto end);

	*ref_cjson_rest= NULL;

	/* Create cJSON tree-root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     //Reserved for future use: add settings to the REST string
	 *     // e.g.: "var1_name":number, ...
	 * }
	 */

	/* Reserved for future use
	 *
	 * Example: If 'var1' is a number value:
	 *
	 * *cjson_aux= NULL;
	 * ...
	 * 	cjson_aux= cJSON_CreateNumber((double)video_settings_dec_ctx->var1);
	 * 	CHECK_DO(cjson_aux!= NULL, goto end);
	 * 	cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

	*ref_cjson_rest= cjson_rest;
	cjson_rest= NULL;
	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}
