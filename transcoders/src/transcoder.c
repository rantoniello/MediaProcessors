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
 * @file transcoder.c
 * @author Rafael Antoniello
 */

#include "transcoder.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/uri_parser.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/proc.h>
#include <libmediaprocs/procs.h>

/* **** Definitions **** */

/**
 * Transcoder settings context structure.
 */
typedef struct transcoder_settings_ctx_s {
	/**
	 * Decoder unambiguous processor identifier name.
	 */
	char *proc_name_dec; //TODO
	/**
	 * Encoder unambiguous processor identifier name.
	 */
	char *proc_name_enc; //TODO
} transcoder_settings_ctx_t;

/**
 * Transcoder context structure.
 */
typedef struct transcoder_ctx_s {
	/**
	 * Generic processor context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * Transcoder settings.
	 */
	volatile struct transcoder_settings_ctx_s transcoder_settings_ctx;
	/**
	 * This parameter refers to the nature of decoding-encoding data path this
	 * processor implements: video transcoder, audio transcoder,
	 * subtitling transcoder, etc.
	 * This parameter can not be considered as a setting as is set only once
	 * at the transcoder instantiation, and cannot be modified later.
	 */
	char *transcoder_subtype;
	/**
	 * pair of decoder->encoder processors.
	 */
	procs_ctx_t *procs_ctx_decenc;
	/**
	 * Decoder processor Id.
	 */
	int proc_id_dec;
	/**
	 * Encoder processor Id.
	 */
	int proc_id_enc;
} transcoder_ctx_t;

/* **** Prototypes **** */

static proc_ctx_t* transcoder_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg);
static void transcoder_close(proc_ctx_t **ref_proc_ctx);
static int transcoder_send_frame(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx);
//static int transcoder_send_frame_nodup(proc_ctx_t *proc_ctx,
//		proc_frame_ctx_t **ref_proc_frame_ctx); //TODO
static int transcoder_recv_frame(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx);
static int transcoder_unblock(proc_ctx_t *proc_ctx);
static int transcoder_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *iput_fifo_ctx, fifo_ctx_t *oput_fifo_ctx);
static int transcoder_rest_put_codec_name(procs_ctx_t *procs_ctx, int proc_id,
		char *volatile*ref_proc_name_curr,
		const char *rest_proc_name_tag /*(e.g. 'proc_name_dec')*/,
		const char *str, log_ctx_t *log_ctx);
static int transcoder_rest_put(proc_ctx_t *proc_ctx, const char *str);
static int transcoder_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);

static int transcoder_settings_ctx_init(
		volatile transcoder_settings_ctx_t *transcoder_settings_ctx,
		log_ctx_t *log_ctx);
static void transcoder_settings_ctx_deinit(
		volatile transcoder_settings_ctx_t *transcoder_settings_ctx,
		log_ctx_t *log_ctx);

/* **** Implementations **** */

const proc_if_t proc_if_transcoder=
{
	"transcoder", "transcoder", "n/a",
	(uint64_t)0, // no specific processor features implemented
	transcoder_open,
	transcoder_close,
	transcoder_send_frame,
	NULL, //transcoder_send_frame_nodup, //TODO
	transcoder_recv_frame,
	transcoder_unblock,
	transcoder_rest_put,
	transcoder_rest_get,
	transcoder_process_frame,
	NULL, // no extra options
	(void*(*)(const proc_frame_ctx_t*))proc_frame_ctx_dup, // *NOT* used really
	(void(*)(void**))proc_frame_ctx_release, // *NOT* used really
	(proc_frame_ctx_t*(*)(const void*))proc_frame_ctx_dup // *NOT* used really
};

/**
 * Implements the proc_if_s::open callback.
 * See .proc_if.h for further details.
 * <br>Variable arguments are:<br>
 * @param transcoder_subtype This parameter refers to the nature of
 * decoding-encoding data path this transcoder implements: video transcoder,
 * audio transcoder, subtitling transcoder, etc. This parameter can not be
 * considered as a setting as is set only once at the transcoder instantiation,
 * and cannot be modified later.
 */
static proc_ctx_t* transcoder_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	char *transcoder_subtype_arg;
	int proc_id_dec, proc_id_enc, ret_code, end_code= STAT_ERROR;
	transcoder_ctx_t *transcoder_ctx= NULL;
	volatile transcoder_settings_ctx_t *transcoder_settings_ctx=
			NULL; // Do not release
	procs_ctx_t *procs_ctx_decenc= NULL;
	char *rest_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Note: 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	transcoder_ctx= (transcoder_ctx_t*)calloc(1, sizeof(transcoder_ctx_t));
	CHECK_DO(transcoder_ctx!= NULL, goto end);

	/* Get settings structure */
	transcoder_settings_ctx= &transcoder_ctx->transcoder_settings_ctx;

	/* Initialize settings to defaults */
	ret_code= transcoder_settings_ctx_init(transcoder_settings_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= transcoder_rest_put((proc_ctx_t*)transcoder_ctx, settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	// TODO: Further specific settings or initializations...

	/* Get trascoder 'sub-type' */
	transcoder_subtype_arg= (char*)va_arg(arg, const char*);
	CHECK_DO(transcoder_subtype_arg!= NULL &&
			strlen(transcoder_subtype_arg)> 0, goto end);
	transcoder_ctx->transcoder_subtype= strdup(transcoder_subtype_arg);
	CHECK_DO(transcoder_ctx->transcoder_subtype!= NULL, goto end);

	/* Open decoder->encoder processors module */
	procs_ctx_decenc= procs_open(LOG_CTX_GET(), 2, NULL, NULL);
	CHECK_DO(procs_ctx_decenc!= NULL, goto end);

	/* Open decoder processor */
	ret_code= procs_opt(procs_ctx_decenc, "PROCS_POST",
			transcoder_settings_ctx->proc_name_dec, "", &rest_str);
	CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL, goto end);

	/* Get decoder Id. */
	cjson_rest= cJSON_Parse(rest_str);
	CHECK_DO(cjson_rest!= NULL, goto end);
	cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id");
	CHECK_DO(cjson_aux!= NULL, goto end);
	proc_id_dec= cjson_aux->valuedouble;
	CHECK_DO(proc_id_dec>= 0, goto end);
	transcoder_ctx->proc_id_dec= proc_id_dec;
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;

	/* Open encoder processor */
	ret_code= procs_opt(procs_ctx_decenc, "PROCS_POST",
			transcoder_settings_ctx->proc_name_enc, "", &rest_str);
	CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL, goto end);

	/* Get encoder Id. */
	cjson_rest= cJSON_Parse(rest_str);
	CHECK_DO(cjson_rest!= NULL, goto end);
	cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id");
	CHECK_DO(cjson_aux!= NULL, goto end);
	proc_id_enc= cjson_aux->valuedouble;
	CHECK_DO(proc_id_enc>= 0, goto end);
	transcoder_ctx->proc_id_enc= proc_id_enc;
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;

	transcoder_ctx->procs_ctx_decenc= procs_ctx_decenc;
	procs_ctx_decenc= NULL; // Avoid double referencing
	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		transcoder_close((proc_ctx_t**)&transcoder_ctx);
	if(procs_ctx_decenc!= NULL)
		procs_close(&procs_ctx_decenc);
	if(rest_str!= NULL)
		free(rest_str);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return (proc_ctx_t*)transcoder_ctx;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void transcoder_close(proc_ctx_t **ref_proc_ctx)
{
	transcoder_ctx_t *transcoder_ctx= NULL;
	procs_ctx_t *procs_ctx_decenc= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	if(ref_proc_ctx== NULL ||
			(transcoder_ctx= (transcoder_ctx_t*)*ref_proc_ctx)== NULL)
		return;

	LOG_CTX_SET(((proc_ctx_t*)transcoder_ctx)->log_ctx);

	/* Release settings */
	transcoder_settings_ctx_deinit(&transcoder_ctx->transcoder_settings_ctx,
			LOG_CTX_GET());

	/* Release transcoder 'sub-type' */
	if(transcoder_ctx->transcoder_subtype!= NULL) {
		free(transcoder_ctx->transcoder_subtype);
		transcoder_ctx->transcoder_subtype= NULL;
	}

	/* Release (close) pair of decoder->encoder processors
	 * (unblock processors firstly by applying DELETE operation).
	 */
	if((procs_ctx_decenc= transcoder_ctx->procs_ctx_decenc)!= NULL) {
		ASSERT(procs_opt(procs_ctx_decenc, "PROCS_ID_DELETE",
				transcoder_ctx->proc_id_dec)== STAT_SUCCESS);
		ASSERT(procs_opt(procs_ctx_decenc, "PROCS_ID_DELETE",
				transcoder_ctx->proc_id_enc)== STAT_SUCCESS);
		procs_close(&transcoder_ctx->procs_ctx_decenc);
	}

	// Reserved for future use: release other new variables here...

	/* Release context structure */
	free(transcoder_ctx);
	*ref_proc_ctx= NULL;
}

/**
 * Implements the proc_if_s::send_frame callback.
 * See .proc_if.h for further details.
 */
static int transcoder_send_frame(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx)
{
	transcoder_ctx_t *transcoder_ctx= (transcoder_ctx_t*)proc_ctx;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//CHECK_DO(proc_frame_ctx!= NULL, return STAT_ERROR); // bypassed

	//LOG_CTX_SET(proc_ctx->log_ctx); // Not used

	/* Write frame to decoder's input buffer */
	return procs_send_frame(transcoder_ctx->procs_ctx_decenc,
			transcoder_ctx->proc_id_dec, proc_frame_ctx);
}
#if 0 //TODO
/**
 * Implements the proc_if_s::send_frame_nodup callback.
 * See .proc_if.h for further details.
 */
static int transcoder_send_frame_nodup(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx)
{
	transcoder_ctx_t *transcoder_ctx= (transcoder_ctx_t*)proc_ctx;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//CHECK_DO(ref_proc_frame_ctx!= NULL, return STAT_ERROR); // bypassed

	//LOG_CTX_SET(proc_ctx->log_ctx); // Not used

	/* Write frame to decoder's input buffer */
	return procs_send_frame_nodup(transcoder_ctx->procs_ctx_decenc,
			transcoder_ctx->proc_id_dec, ref_proc_frame_ctx);
}
#endif
/**
 * Implements the proc_if_s::recv_frame callback.
 * See .proc_if.h for further details.
 */
static int transcoder_recv_frame(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx)
{
	transcoder_ctx_t *transcoder_ctx= (transcoder_ctx_t*)proc_ctx;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//CHECK_DO(ref_proc_frame_ctx!= NULL, return STAT_ERROR); // bypassed

	//LOG_CTX_SET(proc_ctx->log_ctx); // Not used

	/* Read frame from encoder's output buffer */
	return procs_recv_frame(transcoder_ctx->procs_ctx_decenc,
			transcoder_ctx->proc_id_enc, ref_proc_frame_ctx);
}

/**
 * Implements the proc_if_s::unblock callback.
 * See .proc_if.h for further details.
 */
static int transcoder_unblock(proc_ctx_t *proc_ctx)
{
	int flag_error= 0;
	transcoder_ctx_t *transcoder_ctx= (transcoder_ctx_t*)proc_ctx;
	procs_ctx_t *procs_ctx_decenc= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Delete pair of decoder->encoder processors to unblock i/o operations */
	if((procs_ctx_decenc= transcoder_ctx->procs_ctx_decenc)!= NULL) {
		CHECK_DO(procs_opt(procs_ctx_decenc, "PROCS_ID_DELETE",
				transcoder_ctx->proc_id_dec)== STAT_SUCCESS, flag_error= 1);
		CHECK_DO(procs_opt(procs_ctx_decenc, "PROCS_ID_DELETE",
				transcoder_ctx->proc_id_enc)== STAT_SUCCESS, flag_error= 1);
	}
	return flag_error!= 0? STAT_ERROR: STAT_SUCCESS;
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int transcoder_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	transcoder_ctx_t *transcoder_ctx= NULL; // Do not release
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	//CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR); // Not used
	//CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR); // Not used

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get transcoder context */
	transcoder_ctx= (transcoder_ctx_t*)proc_ctx;

	/* Get frame from decoder and put (duplicate) to encoder */
	ret_code= procs_recv_frame(transcoder_ctx->procs_ctx_decenc,
			transcoder_ctx->proc_id_dec, &proc_frame_ctx);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);
	if(proc_frame_ctx== NULL) {
		end_code= ret_code;
		goto end;
	}
	ret_code= procs_send_frame(transcoder_ctx->procs_ctx_decenc,
			transcoder_ctx->proc_id_enc, proc_frame_ctx);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);

	end_code= STAT_SUCCESS;
end:
	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return end_code;
}

////////////////////////////////////////////////////////////////////////////////////
static int transcoder_rest_parse4codec_names(procs_ctx_t *procs_ctx, int proc_id,
		char *volatile*ref_proc_name_curr,
		const char *rest_proc_name_tag /*(e.g. 'proc_name_dec')*/,
		const char *str, log_ctx_t *log_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	char *proc_name_str_dec= NULL, *proc_name_str_enc= NULL;
	cJSON *cjson_rest= NULL;
	cJSON *cjson_aux= NULL; // Do not release
	size_t proc_name_str_len= 0;
	char name_put_array[256]= {0}; // should be enough
	cJSON *cjson_settings= NULL, *cjson_proc_name= NULL; // DO not release
	char *get_rest_str= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_id>= 0, return STAT_ERROR);
	CHECK_DO(ref_proc_name_curr!= NULL && *ref_proc_name_curr!= NULL,
			return STAT_ERROR);
	CHECK_DO(rest_proc_name_tag!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);
	//log_ctx is allowed to be NULL

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** PUT decoder and encoder processor names ****
	 * First of all we put eventually new decoder and encoder processor
	 * names. If any of the processors names have changed, a new processor
	 * type will be instantiated substituting the current one (and recycling
	 * current settings that are also applicable to the new type). If names
	 * does not change, putting processors names has no effect.
	 */
	if(flag_is_query== 1) {
		proc_name_str_dec= uri_parser_query_str_get_value("proc_name_dec", str);
		proc_name_str_enc= uri_parser_query_str_get_value("proc_name_enc", str);
	} else {
		cJSON *cjson_proc_name_dec, *cjson_proc_name_enc;

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		cjson_proc_name_dec= cJSON_GetObjectItem(cjson_rest, "proc_name_dec");
		if(cjson_proc_name_dec!= NULL)
			proc_name_str_dec= strdup(cjson_proc_name_dec->valuestring);
		cjson_proc_name_enc= cJSON_GetObjectItem(cjson_rest, "proc_name_enc");
		if(cjson_proc_name_enc!= NULL)
			proc_name_str_enc= strdup(cjson_proc_name_enc->valuestring);
	}

	/* If no new name is specified we are done */
	if(proc_name_str== NULL || (proc_name_str_len= strlen(proc_name_str))== 0
			|| strcmp(proc_name_str, *ref_proc_name_curr)== 0) {
		end_code= STAT_SUCCESS;
		goto end;
	}



	/* Once confirmed PUT succeed, we set new processor name */
	free(*ref_proc_name_curr);
	*ref_proc_name_curr= proc_name_str;
	proc_name_str= NULL; // Avoid double referencing
	end_code= STAT_SUCCESS;
end:
	if(proc_name_str!= NULL)
		free(proc_name_str);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(get_rest_str!= NULL)
		free(get_rest_str);
	return end_code;
}

/////////////////////////////////////////////////////////////////////

static int transcoder_rest_put_codec_name(procs_ctx_t *procs_ctx, int proc_id,
		char *volatile*ref_proc_name_curr,
		const char *rest_proc_name_tag /*(e.g. 'proc_name_dec')*/,
		const char *str, log_ctx_t *log_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	int flag_is_query= 0; // 0-> JSON / 1->query string
	char *proc_name_str= NULL;
	cJSON *cjson_rest= NULL;
	cJSON *cjson_aux= NULL; // Do not release
	size_t proc_name_str_len= 0;
	char name_put_array[256]= {0}; // should be enough
	cJSON *cjson_settings= NULL, *cjson_proc_name= NULL; // DO not release
	char *get_rest_str= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(proc_id>= 0, return STAT_ERROR);
	CHECK_DO(ref_proc_name_curr!= NULL && *ref_proc_name_curr!= NULL,
			return STAT_ERROR);
	CHECK_DO(rest_proc_name_tag!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);
	//log_ctx is allowed to be NULL

	/* Guess string representation format (JSON-REST or Query) */
	//LOGV("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** PUT decoder and encoder processor names ****
	 * First of all we put eventually new decoder and encoder processor
	 * names. If any of the processors names have changed, a new processor
	 * type will be instantiated substituting the current one (and recycling
	 * current settings that are also applicable to the new type). If names
	 * does not change, putting processors names has no effect.
	 */
	if(flag_is_query== 1) {
		proc_name_str= uri_parser_query_str_get_value(rest_proc_name_tag, str);
	} else {
		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		cjson_aux= cJSON_GetObjectItem(cjson_rest, rest_proc_name_tag);
		if(cjson_aux!= NULL)
			proc_name_str= strdup(cjson_aux->valuestring);
	}

	/* If no new name is specified we are done */
	if(proc_name_str== NULL || (proc_name_str_len= strlen(proc_name_str))== 0
			|| strcmp(proc_name_str, *ref_proc_name_curr)== 0) {
		end_code= STAT_SUCCESS;
		goto end;
	}

	/* **** At this point we assume we have a new processor name **** */

	/* Compose and PUT encoder processor name */
	CHECK_DO(sizeof(name_put_array)> (strlen("proc_name=")+ proc_name_str_len),
			goto end);
	snprintf(name_put_array, sizeof(name_put_array), "proc_name=%s",
			proc_name_str);
	ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id, name_put_array);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Despite computational penalty, it is important to check coherence
	 * between transcoder name-setting and processor name-settings.
	 */

	/* Get processors REST */
	if(cjson_rest!= NULL) {
		cJSON_Delete(cjson_rest);
		cjson_rest= NULL;
	}
	ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id, &get_rest_str);
	CHECK_DO(ret_code== STAT_SUCCESS && get_rest_str!= NULL &&
			(cjson_rest= cJSON_Parse(get_rest_str))!= NULL, goto end);

	/* Parse REST; it has the following form :
	 * {
	 *     ...
	 *     "settings":{
	 *         "proc_name":"ffmpeg_m2v_enc",
	 *         ...
	 *     }
	 * }
	 */
	cjson_settings= cJSON_GetObjectItem(cjson_rest, "settings");
	CHECK_DO(cjson_settings!= NULL, goto end);

	cjson_proc_name= cJSON_GetObjectItem(cjson_settings, "proc_name");
	CHECK_DO(cjson_proc_name!= NULL && cjson_proc_name->valuestring!= NULL,
			goto end);
	CHECK_DO(strcmp(cjson_proc_name->valuestring, proc_name_str)== 0, goto end);

	/* Once confirmed PUT succeed, we set new processor name */
	free(*ref_proc_name_curr);
	*ref_proc_name_curr= proc_name_str;
	proc_name_str= NULL; // Avoid double referencing
	end_code= STAT_SUCCESS;
end:
	if(proc_name_str!= NULL)
		free(proc_name_str);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(get_rest_str!= NULL)
		free(get_rest_str);
	return end_code;
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int transcoder_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int ret_code, end_code= STAT_ERROR, proc_id_dec= -1, proc_id_enc= -1;
	transcoder_ctx_t *transcoder_ctx= NULL; // Do not release
	procs_ctx_t *procs_ctx_decenc= NULL; // Do not release
	volatile transcoder_settings_ctx_t *transcoder_settings_ctx=
			NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	transcoder_ctx= (transcoder_ctx_t*)proc_ctx;

	procs_ctx_decenc= transcoder_ctx->procs_ctx_decenc;
	CHECK_DO(procs_ctx_decenc!= NULL, goto end);

	proc_id_dec= transcoder_ctx->proc_id_dec;
	proc_id_enc= transcoder_ctx->proc_id_enc;

	/* Get transcoder settings context */
	transcoder_settings_ctx= &transcoder_ctx->transcoder_settings_ctx;

	/* **** PUT decoder and encoder processor names ****
	 * First of all we put eventually new decoder and encoder processor
	 * names. If any of the processors names have changed, a new processor
	 * type will be instantiated substituting the current one (and recycling
	 * current settings that are also applicable to the new type). If names
	 * does not change, putting processors names has no effect.
	 */
	ret_code= transcoder_rest_put_codec_name(procs_ctx_decenc, proc_id_dec,
			&transcoder_settings_ctx->proc_name_dec, "proc_name_dec", str,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	ret_code= transcoder_rest_put_codec_name(procs_ctx_decenc, proc_id_enc,
			&transcoder_settings_ctx->proc_name_enc, "proc_name_enc", str,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* In a transcoder, settings coincide exactly to the encoder settings
	 * (at the only exception of the decoder name which is the only decoder
	 * setting used). Thus, we pass the rest of the settings to the encoder.
	 * Note that we might pass again the encoder processor name setting, but
	 * this has no effect as it was already set previously in the code above
	 * (moreover, the identifier 'proc_name_enc' will be probably ignored as
	 * unknown).
	 */
	ret_code= procs_opt(procs_ctx_decenc, "PROCS_ID_PUT", proc_id_enc, str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* PUT other specific transcoder settings */
	// Reserved for future use

	end_code= STAT_SUCCESS;
end:
	return end_code;
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int transcoder_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int ret_code, end_code= STAT_ERROR, proc_id_dec= -1, proc_id_enc= -1;
	transcoder_ctx_t *transcoder_ctx= NULL; // Do not release
	procs_ctx_t *procs_ctx_decenc= NULL; // Do not release
	volatile transcoder_settings_ctx_t *transcoder_settings_ctx=
			NULL; // Do not release
	char *dec_rest_str= NULL, *enc_rest_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_rest_dec= NULL, *cjson_rest_enc= NULL,
			*cjson_settings= NULL, *cjson_proc_name= NULL;
	cJSON *cjson_aux= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(rest_fmt< PROC_IF_REST_FMT_ENUM_MAX, return STAT_ERROR);
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	transcoder_ctx= (transcoder_ctx_t*)proc_ctx;

	*ref_reponse= NULL;

	procs_ctx_decenc= transcoder_ctx->procs_ctx_decenc;
	CHECK_DO(procs_ctx_decenc!= NULL, goto end);

	proc_id_dec= transcoder_ctx->proc_id_dec;
	proc_id_enc= transcoder_ctx->proc_id_enc;

	/* Create cJSON tree root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     ... selected data from decoder & encoder ...
	 *     "settings":
	 *     {
	 *         "proc_name_dec":string,
	 *         "proc_name_enc":string,
	 *         ... copy encoder settings (except processor name) ...
	 *     },
	 *     ... // Reserved for future use
	 * }
	 */

	/* GET decoder processor REST */
	ret_code= procs_opt(procs_ctx_decenc, "PROCS_ID_GET", proc_id_dec,
			&dec_rest_str);
	CHECK_DO(ret_code== STAT_SUCCESS && dec_rest_str!= NULL &&
			(cjson_rest_dec= cJSON_Parse(dec_rest_str))!= NULL, goto end);

	/* GET encoder processor REST */
	ret_code= procs_opt(procs_ctx_decenc, "PROCS_ID_GET", proc_id_enc,
			&enc_rest_str);
	CHECK_DO(ret_code== STAT_SUCCESS && enc_rest_str!= NULL &&
			(cjson_rest_enc= cJSON_Parse(enc_rest_str))!= NULL, goto end);

	/* **** Attach data to REST response **** */

	// Reserved for future use: set other data values here...
	/* Example:
	 * cjson_aux= cJSON_CreateNumber((double)avcodecctx->var1);
	 * CHECK_DO(cjson_aux!= NULL, goto end);
	 * cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

	/* **** Compose settings object **** */

	/* Get transcoder settings context */
	transcoder_settings_ctx=
			&transcoder_ctx->transcoder_settings_ctx;

	/* Detach encoder processor settings REST */
	cjson_settings= cJSON_DetachItemFromObject(cjson_rest_enc, "settings");
	CHECK_DO(cjson_settings!= NULL, goto end);

	/* Detach encoder name setting 'proc_name' */
	cjson_proc_name= cJSON_DetachItemFromObject(cjson_settings, "proc_name");
	CHECK_DO(cjson_proc_name!= NULL, goto end);
	cJSON_Delete(cjson_proc_name);
	cjson_proc_name= NULL;

	/* Set 'proc_name_enc'
	 * We do a little HACK to insert elements at top as cJSON library does not
	 * support it natively -it always insert at the bottom-
	 * We do this at the risk of braking in a future library version, as we
	 * base current solution on the internal implementation of function
	 * 'cJSON_AddItemToObject()' -may change in future-.
	 */
	cjson_aux= cJSON_CreateString(transcoder_settings_ctx->proc_name_enc);
	CHECK_DO(cjson_aux!= NULL, goto end);
	// Hack of 'cJSON_AddItemToObject(cjson_rest, "latency_avg_usec",
	// 		cjson_aux);':
	cjson_aux->string= (char*)strdup("proc_name_enc");
	cjson_aux->type|= cJSON_StringIsConst;
	//cJSON_AddItemToArray(cjson_rest, cjson_aux);
	cJSON_InsertItemInArray(cjson_settings, 0, cjson_aux); // Insert at top
	cjson_aux->type&= ~cJSON_StringIsConst;

	/* Set 'proc_name_dec' */
	cjson_aux= cJSON_CreateString(transcoder_settings_ctx->proc_name_dec);
	CHECK_DO(cjson_aux!= NULL, goto end);
	// Hack of 'cJSON_AddItemToObject(cjson_rest, "latency_avg_usec",
	// 		cjson_aux);':
	cjson_aux->string= (char*)strdup("proc_name_dec");
	cjson_aux->type|= cJSON_StringIsConst;
	//cJSON_AddItemToArray(cjson_rest, cjson_aux);
	cJSON_InsertItemInArray(cjson_settings, 0, cjson_aux); // Insert at top
	cjson_aux->type&= ~cJSON_StringIsConst;

	/* Attach specific transcoder settings from transcoder context structure */
	// Reserved for future use: attach to 'cjson_settings' (should be != NULL)

	/* Attach settings object to REST response */
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	cjson_settings= NULL; // Attached; avoid double referencing

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
	if(dec_rest_str!= NULL)
		free(dec_rest_str);
	if(cjson_rest_dec!= NULL)
		cJSON_Delete(cjson_rest_dec);
	if(enc_rest_str!= NULL)
		free(enc_rest_str);
	if(cjson_rest_enc!= NULL)
		cJSON_Delete(cjson_rest_enc);
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	if(cjson_proc_name!= NULL)
		cJSON_Delete(cjson_proc_name);
	return end_code;
}

static int transcoder_settings_ctx_init(
		volatile transcoder_settings_ctx_t *transcoder_settings_ctx,
		log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(transcoder_settings_ctx!= NULL, return STAT_ERROR);

	/* **** Initialize specific transcoder settings **** */

	transcoder_settings_ctx->proc_name_dec= strdup("bypass");
	CHECK_DO(transcoder_settings_ctx->proc_name_dec!= NULL, return STAT_ERROR);

	transcoder_settings_ctx->proc_name_enc= strdup("bypass");
	CHECK_DO(transcoder_settings_ctx->proc_name_enc!= NULL, return STAT_ERROR);

	return STAT_SUCCESS;
}

static void transcoder_settings_ctx_deinit(
		volatile transcoder_settings_ctx_t *transcoder_settings_ctx,
		log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(transcoder_settings_ctx!= NULL, return);

	/* **** Release specific transcoder settings **** */

	if(transcoder_settings_ctx->proc_name_dec!= NULL) {
		free(transcoder_settings_ctx->proc_name_dec);
		transcoder_settings_ctx->proc_name_dec= NULL;
	}

	if(transcoder_settings_ctx->proc_name_enc!= NULL) {
		free(transcoder_settings_ctx->proc_name_enc);
		transcoder_settings_ctx->proc_name_enc= NULL;
	}
}
