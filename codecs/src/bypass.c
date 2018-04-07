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
 * @file bypass.c
 * @author Rafael Antoniello
 */

#include "bypass.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/proc.h>

/* **** Definitions **** */

/**
 * Bypass processor settings context structure.
 */
typedef struct bypass_settings_ctx_s {
	// Reserved for future use
} bypass_settings_ctx_t;

/**
 * Bypass processor context structure.
 */
typedef struct bypass_ctx_s {
	/**
	 * Generic processor context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * Bypass processor settings.
	 */
	volatile struct bypass_settings_ctx_s bypass_settings_ctx;
} bypass_ctx_t;

/* **** Prototypes **** */

static proc_ctx_t* bypass_open(const proc_if_t *proc_if,
		const char *settings_str, const char* href, log_ctx_t *log_ctx,
		va_list arg);
static void bypass_close(proc_ctx_t **ref_proc_ctx);
static int bypass_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *iput_fifo_ctx, fifo_ctx_t *oput_fifo_ctx);
static int bypass_rest_put(proc_ctx_t *proc_ctx, const char *str);
static int bypass_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);

static int bypass_settings_ctx_init(
		volatile bypass_settings_ctx_t *bypass_settings_ctx,
		log_ctx_t *log_ctx);
static void bypass_settings_ctx_deinit(
		volatile bypass_settings_ctx_t *bypass_settings_ctx,
		log_ctx_t *log_ctx);

/* **** Implementations **** */

const proc_if_t proc_if_bypass=
{
	"bypass", "bypass", "video/bypass",
	(uint64_t)(PROC_FEATURE_BITRATE|PROC_FEATURE_REGISTER_PTS),
	bypass_open,
	bypass_close,
	proc_send_frame_default1,
	NULL, // send-no-dup
	proc_recv_frame_default1,
	NULL, // no specific unblock function extension
	bypass_rest_put,
	bypass_rest_get,
	bypass_process_frame,
	NULL, // no extra options
	(void*(*)(const proc_frame_ctx_t*))proc_frame_ctx_dup,
	(void(*)(void**))proc_frame_ctx_release,
	(proc_frame_ctx_t*(*)(const void*))proc_frame_ctx_dup
};

/**
 * Implements the proc_if_s::open callback.
 * See .proc_if.h for further details.
 */
static proc_ctx_t* bypass_open(const proc_if_t *proc_if,
		const char *settings_str, const char* href, log_ctx_t *log_ctx,
		va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	bypass_ctx_t *bypass_ctx= NULL;
	volatile bypass_settings_ctx_t *bypass_settings_ctx= NULL; // Do not release
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Parameter 'href' is allowed to be NULL
	// Parameter 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	bypass_ctx= (bypass_ctx_t*)calloc(1, sizeof(bypass_ctx_t));
	CHECK_DO(bypass_ctx!= NULL, goto end);

	/* Get settings structure */
	bypass_settings_ctx= &bypass_ctx->bypass_settings_ctx;

	/* Initialize settings to defaults */
	ret_code= bypass_settings_ctx_init(bypass_settings_ctx, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= bypass_rest_put((proc_ctx_t*)bypass_ctx, settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

    end_code= STAT_SUCCESS;
 end:
    if(end_code!= STAT_SUCCESS)
    	bypass_close((proc_ctx_t**)&bypass_ctx);
	return (proc_ctx_t*)bypass_ctx;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void bypass_close(proc_ctx_t **ref_proc_ctx)
{
	bypass_ctx_t *bypass_ctx= NULL;
	LOG_CTX_INIT(NULL);

	if(ref_proc_ctx== NULL || (bypass_ctx= (bypass_ctx_t*)*ref_proc_ctx)== NULL)
		return;

	LOG_CTX_SET(((proc_ctx_t*)bypass_ctx)->log_ctx);

	/* Release settings */
	bypass_settings_ctx_deinit(&bypass_ctx->bypass_settings_ctx, LOG_CTX_GET());

	// Reserved for future use: release other new variables here...

	/* Release context structure */
	free(bypass_ctx);
	*ref_proc_ctx= NULL;
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int bypass_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	size_t fifo_elem_size= 0;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get input frame from FIFO buffer */
	ret_code= fifo_get(iput_fifo_ctx, (void**)&proc_frame_ctx, &fifo_elem_size);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);
	if(ret_code== STAT_EAGAIN) {
		/* This means FIFO was unblocked, just go out with EOF status */
		end_code= STAT_EOF;
		goto end;
	}
	if(proc_frame_ctx== NULL)
		goto end;

	/* Bypass input frame directly to output buffer
	 * (we do not duplicate buffer, this is just a 'pointer' passing).
	 * Note that if 'fifo_pu()' succeed, 'proc_frame_ctx' is internally set
	 * to NULL.
	 */
	ret_code= fifo_put(oput_fifo_ctx, (void**)&proc_frame_ctx, sizeof(void*));
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_ENOMEM, goto end);

	end_code= STAT_SUCCESS;
end:
	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return end_code;
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int bypass_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	//bypass_ctx_t *bypass_ctx= NULL;
	//volatile bypass_settings_ctx_t *bypass_settings_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	//LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get processor context and settings context */
	//bypass_ctx= (bypass_ctx_t*)proc_ctx;
	//bypass_settings_ctx= &bypass_ctx->bypass_settings_ctx;

	/* PUT specific processor settings */
	// Reserved for future use

	return STAT_SUCCESS;
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int bypass_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int end_code= STAT_ERROR;
	//bypass_ctx_t *bypass_ctx= NULL;
	//volatile bypass_settings_ctx_t *bypass_settings_ctx= NULL;
	cJSON *cjson_rest= NULL, *cjson_settings= NULL;
	//cJSON *cjson_aux= NULL; // Do not release // Reserved for future use
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(rest_fmt< PROC_IF_REST_FMT_ENUM_MAX, return STAT_ERROR);
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	*ref_reponse= NULL;

	/* Create cJSON tree root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     "settings":
	 *     {
	 *         ... // Reserved for future use
	 *     },
	 *     ... // Reserved for future use
	 * }
	 */

	/* Get processor context and settings context */
	//bypass_ctx= (bypass_ctx_t*)proc_ctx;
	//bypass_settings_ctx= &bypass_ctx->bypass_settings_ctx;

	/* Create cJSON settings object */
	cjson_settings= cJSON_CreateObject();
	CHECK_DO(cjson_settings!= NULL, goto end);

	/* GET specific processor settings */
	// Reserved for future use: attach to 'cjson_settings' (should be != NULL)

	/* Attach settings object to REST response */
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	cjson_settings= NULL; // Attached; avoid double referencing

	/* **** Attach data to REST response **** */
	// Reserved for future use
	/* Example:
	 * cjson_aux= cJSON_CreateNumber((double)avcodecctx->var1);
	 * CHECK_DO(cjson_aux!= NULL, goto end);
	 * cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

	// Reserved for future use: set other data values here...

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
		LOGE("Unknown format requested for processor REST\n");
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	return end_code;
}

/**
 * Initialize specific bypass processor settings to defaults.
 * @param bypass_settings_ctx
 * @param log_ctx
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
static int bypass_settings_ctx_init(
		volatile bypass_settings_ctx_t *bypass_settings_ctx,
		log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(bypass_settings_ctx!= NULL, return STAT_ERROR);

	/* Initialize specific processor settings */
	// Reserved for future use

	return STAT_SUCCESS;
}

/**
 * Release specific bypass processor settings (allocated in heap memory).
 * @param bypass_settings_ctx
 * @param log_ctx
 */
static void bypass_settings_ctx_deinit(
		volatile bypass_settings_ctx_t *bypass_settings_ctx,
		log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(bypass_settings_ctx!= NULL, return);

	/* Release specific processor settings */
	// Reserved for future use
}
