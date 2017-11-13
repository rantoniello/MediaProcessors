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
 * @file ffmpeg_m2v.c
 * @author Rafael Antoniello
 */

#include "ffmpeg_m2v.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <libcjson/cJSON.h>
#include <libavcodec/avcodec.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/proc.h>
#include "ffmpeg_video.h"
#include "proc_frame_2_ffmpeg.h"
#include "video_settings.h"

/* **** Definitions **** */

/**
 * FFmpeg's MPEG-2 video encoder settings context structure.
 */
typedef struct ffmpeg_m2v_enc_settings_ctx_s {
	/**
	 * Generic video encoder settings.
	 * *MUST* be the first field in order to be able to cast to
	 * video_settings_enc_ctx_s.
	 */
	struct video_settings_enc_ctx_s video_settings_enc_ctx;
} ffmpeg_m2v_enc_settings_ctx_t;

/**
 * FFmpeg's MPEG-2 video encoder wrapper context structure.
 */
typedef struct ffmpeg_m2v_enc_ctx_s {
	/**
	 * Generic FFmpeg's video encoder structure.
	 * *MUST* be the first field in order to be able to cast to both
	 * ffmpeg_video_enc_ctx_t or proc_ctx_t.
	 */
	struct ffmpeg_video_enc_ctx_s ffmpeg_video_enc_ctx;
	/**
	 * MPEG-2 video encoder settings.
	 * This structure extends (thus can be casted to) video_settings_enc_ctx_t.
	 */
	volatile struct ffmpeg_m2v_enc_settings_ctx_s ffmpeg_m2v_enc_settings_ctx;
} ffmpeg_m2v_enc_ctx_t;

/**
 * FFmpeg's MPEG-2 video decoder settings context structure.
 */
typedef struct ffmpeg_m2v_dec_settings_ctx_s {
	/**
	 * Generic video decoder settings.
	 * *MUST* be the first field in order to be able to cast to
	 * video_settings_dec_ctx_s.
	 */
	struct video_settings_dec_ctx_s video_settings_dec_ctx;
} ffmpeg_m2v_dec_settings_ctx_t;

/**
 * FFmpeg's MPEG-2 video decoder wrapper context structure.
 */
typedef struct ffmpeg_m2v_dec_ctx_s {
	/**
	 * Generic FFmpeg's video decoder structure.
	 * *MUST* be the first field in order to be able to cast to both
	 * ffmpeg_video_dec_ctx_t or proc_ctx_t.
	 */
	struct ffmpeg_video_dec_ctx_s ffmpeg_video_dec_ctx;
	/**
	 * MPEG-2 video decoder settings.
	 * This structure extends (thus can be casted to) video_settings_dec_ctx_t.
	 */
	volatile struct ffmpeg_m2v_dec_settings_ctx_s ffmpeg_m2v_dec_settings_ctx;
} ffmpeg_m2v_dec_ctx_t;

/* **** Prototypes **** */

/* **** Encoder **** */

static proc_ctx_t* ffmpeg_m2v_enc_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg);
static void ffmpeg_m2v_enc_close(proc_ctx_t **ref_proc_ctx);
static int ffmpeg_m2v_enc_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *iput_fifo_ctx, fifo_ctx_t *oput_fifo_ctx);
static int ffmpeg_m2v_enc_rest_put(proc_ctx_t *proc_ctx, const char *str);
static int ffmpeg_m2v_enc_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);

static int ffmpeg_m2v_enc_settings_ctx_init(
		volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx,
		log_ctx_t *log_ctx);
static void ffmpeg_m2v_enc_settings_ctx_deinit(
		volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx,
		log_ctx_t *log_ctx);

/* **** Decoder **** */

static proc_ctx_t* ffmpeg_m2v_dec_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg);
static void ffmpeg_m2v_dec_close(proc_ctx_t **ref_proc_ctx);
static int ffmpeg_m2v_dec_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx);

static int ffmpeg_m2v_dec_rest_put(proc_ctx_t *proc_ctx, const char *str);
static int ffmpeg_m2v_dec_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);

static int ffmpeg_m2v_dec_settings_ctx_init(
		volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx,
		log_ctx_t *log_ctx);
static void ffmpeg_m2v_dec_settings_ctx_deinit(
		volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx,
		log_ctx_t *log_ctx);

/* **** Implementations **** */

const proc_if_t proc_if_ffmpeg_m2v_enc=
{
	"ffmpeg_m2v_enc", "encoder", "video/MPV",
	(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
			PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
	ffmpeg_m2v_enc_open,
	ffmpeg_m2v_enc_close,
	ffmpeg_m2v_enc_rest_put,
	ffmpeg_m2v_enc_rest_get,
	ffmpeg_m2v_enc_process_frame,
	NULL, // no extra options
	proc_frame_ctx_2_avframe,
	avframe_release,
	avpacket_2_proc_frame_ctx
};

const proc_if_t proc_if_ffmpeg_m2v_dec=
{
	"ffmpeg_m2v_dec", "decoder", "video/MPV",
	(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
			PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
	ffmpeg_m2v_dec_open,
	ffmpeg_m2v_dec_close,
	ffmpeg_m2v_dec_rest_put,
	ffmpeg_m2v_dec_rest_get,
	ffmpeg_m2v_dec_process_frame,
	NULL, // no extra options
	proc_frame_ctx_2_avpacket,
	avpacket_release,
	avframe_2_proc_frame_ctx
};

/**
 * Implements the proc_if_s::open callback.
 * See .proc_if.h for further details.
 */
static proc_ctx_t* ffmpeg_m2v_enc_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	ffmpeg_m2v_enc_ctx_t *ffmpeg_m2v_enc_ctx= NULL;
	volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx=
			NULL; // Do not release
	ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx= NULL; // Do not release
	volatile video_settings_enc_ctx_t *video_settings_enc_ctx=
			NULL; // Do not release
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Note: 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	ffmpeg_m2v_enc_ctx= (ffmpeg_m2v_enc_ctx_t*)calloc(1, sizeof(
			ffmpeg_m2v_enc_ctx_t));
	CHECK_DO(ffmpeg_m2v_enc_ctx!= NULL, goto end);

	/* Get settings structure */
	ffmpeg_m2v_enc_settings_ctx=
			&ffmpeg_m2v_enc_ctx->ffmpeg_m2v_enc_settings_ctx;

	/* Initialize settings to defaults */
	ret_code= ffmpeg_m2v_enc_settings_ctx_init(ffmpeg_m2v_enc_settings_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= ffmpeg_m2v_enc_rest_put((proc_ctx_t*)ffmpeg_m2v_enc_ctx,
			settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Get generic video encoder and settings context structures */
	ffmpeg_video_enc_ctx= &ffmpeg_m2v_enc_ctx->ffmpeg_video_enc_ctx;
	video_settings_enc_ctx= (volatile video_settings_enc_ctx_t*)
			ffmpeg_m2v_enc_settings_ctx;

	/* Initialize FFMPEG's generic video encoder context */
	ret_code= ffmpeg_video_enc_ctx_init(ffmpeg_video_enc_ctx,
			(int)AV_CODEC_ID_MPEG2VIDEO,
			(const video_settings_enc_ctx_t*)video_settings_enc_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

    end_code= STAT_SUCCESS;
 end:
    if(end_code!= STAT_SUCCESS)
    	ffmpeg_m2v_enc_close((proc_ctx_t**)&ffmpeg_m2v_enc_ctx);
	return (proc_ctx_t*)ffmpeg_m2v_enc_ctx;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void ffmpeg_m2v_enc_close(proc_ctx_t **ref_proc_ctx)
{
	ffmpeg_m2v_enc_ctx_t *ffmpeg_m2v_enc_ctx= NULL;

	if(ref_proc_ctx== NULL)
		return;

	if((ffmpeg_m2v_enc_ctx= (ffmpeg_m2v_enc_ctx_t*)*ref_proc_ctx)!=
			NULL) {
		LOG_CTX_INIT(((proc_ctx_t*)ffmpeg_m2v_enc_ctx)->log_ctx);

		/* De-initialize FFMPEG's generic video encoder context */
		ffmpeg_video_enc_ctx_deinit(&ffmpeg_m2v_enc_ctx->ffmpeg_video_enc_ctx,
				LOG_CTX_GET());

		/* Release settings */
		ffmpeg_m2v_enc_settings_ctx_deinit(
				&ffmpeg_m2v_enc_ctx->ffmpeg_m2v_enc_settings_ctx,
				LOG_CTX_GET());

		// Reserved for future use: release other new variables here...

		/* Release context structure */
		free(ffmpeg_m2v_enc_ctx);
		*ref_proc_ctx= NULL;
	}
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int ffmpeg_m2v_enc_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	ffmpeg_m2v_enc_ctx_t *ffmpeg_m2v_enc_ctx= NULL; // Do not release
	ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx= NULL; // Do not release
	AVFrame *avframe_iput= NULL;
	size_t fifo_elem_size= 0;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get FFmpeg video encoder context */
	ffmpeg_m2v_enc_ctx= (ffmpeg_m2v_enc_ctx_t*)proc_ctx;
	ffmpeg_video_enc_ctx= &ffmpeg_m2v_enc_ctx->ffmpeg_video_enc_ctx;

	/* Get input frame from FIFO buffer */
	ret_code= fifo_get(iput_fifo_ctx, (void**)&avframe_iput, &fifo_elem_size);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);
	if(ret_code== STAT_EAGAIN) {
		/* This means FIFO was unblocked, just go out with EOF status */
		end_code= STAT_EOF;
		goto end;
	}

	/* Encode frame */
	ret_code= ffmpeg_video_enc_frame(ffmpeg_video_enc_ctx, avframe_iput,
			oput_fifo_ctx, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);

	end_code= STAT_SUCCESS;
end:
	if(avframe_iput!= NULL)
		av_frame_free(&avframe_iput);
	return end_code;
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int ffmpeg_m2v_enc_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int ret_code;
	ffmpeg_m2v_enc_ctx_t *ffmpeg_m2v_enc_ctx= NULL;
	volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx= NULL;
	volatile video_settings_enc_ctx_t *video_settings_enc_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get FFmpeg video encoder settings contexts */
	ffmpeg_m2v_enc_ctx= (ffmpeg_m2v_enc_ctx_t*)proc_ctx;
	ffmpeg_m2v_enc_settings_ctx=
			&ffmpeg_m2v_enc_ctx->ffmpeg_m2v_enc_settings_ctx;
	video_settings_enc_ctx=
			&ffmpeg_m2v_enc_settings_ctx->video_settings_enc_ctx;

	/* PUT generic video encoder settings */
	ret_code= video_settings_enc_ctx_restful_put(video_settings_enc_ctx, str,
			LOG_CTX_GET());
	if(ret_code!= STAT_SUCCESS)
		return ret_code;

	/* PUT specific m2v video encoder settings */
	// Reserved for future use

	/* Finally that we have new settings parsed, reset FFMPEG processor */
	ffmpeg_video_reset_on_new_settings(proc_ctx,
			(volatile void*)video_settings_enc_ctx, 1/*Signal is an encoder*/,
			LOG_CTX_GET());

	return STAT_SUCCESS;
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int ffmpeg_m2v_enc_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int ret_code, end_code= STAT_ERROR;
	ffmpeg_m2v_enc_ctx_t *ffmpeg_m2v_enc_ctx= NULL;
	ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx= NULL;
	AVCodecContext *avcodecctx= NULL;
	volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx= NULL;
	volatile video_settings_enc_ctx_t *video_settings_enc_ctx= NULL;
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
	 *         ...
	 *     },
	 *     ... // Reserved for future use
	 * }
	 */

	/* Get FFmpeg video encoder settings contexts */
	ffmpeg_m2v_enc_ctx= (ffmpeg_m2v_enc_ctx_t*)proc_ctx;
	ffmpeg_m2v_enc_settings_ctx=
			&ffmpeg_m2v_enc_ctx->ffmpeg_m2v_enc_settings_ctx;
	video_settings_enc_ctx=
			&ffmpeg_m2v_enc_settings_ctx->video_settings_enc_ctx;

	/* GET generic video encoder settings */
	ret_code= video_settings_enc_ctx_restful_get(video_settings_enc_ctx,
			&cjson_settings, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_settings!= NULL, goto end);

	/* GET specific m2v video encoder settings */
	// Reserved for future use: attach to 'cjson_settings' (should be != NULL)

	/* Attach settings object to REST response */
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	cjson_settings= NULL; // Attached; avoid double referencing

	/* **** Attach data to REST response **** */

	ffmpeg_video_enc_ctx= &ffmpeg_m2v_enc_ctx->ffmpeg_video_enc_ctx;
	avcodecctx= ffmpeg_video_enc_ctx->avcodecctx;
	CHECK_DO(avcodecctx!= NULL, goto end);

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
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

/**
 * Initialize specific MPEG-2 video encoder settings to defaults.
 * @param ffmpeg_m2v_enc_settings_ctx
 * @param log_ctx
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
static int ffmpeg_m2v_enc_settings_ctx_init(
		volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx,
		log_ctx_t *log_ctx)
{
	int ret_code;
	volatile video_settings_enc_ctx_t *video_settings_enc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(ffmpeg_m2v_enc_settings_ctx!= NULL, return STAT_ERROR);

	video_settings_enc_ctx=
			&ffmpeg_m2v_enc_settings_ctx->video_settings_enc_ctx;

	/* Initialize generic video encoder settings */
	ret_code= video_settings_enc_ctx_init(video_settings_enc_ctx);
	if(ret_code!= STAT_SUCCESS)
		return ret_code;

	/* Initialize specific m2v video encoder settings */
	// Reserved for future use

	return STAT_SUCCESS;
}

/**
 * Release specific MPEG-2 video encoder settings (allocated in heap memory).
 * @param ffmpeg_m2v_enc_settings_ctx
 * @param log_ctx
 */
static void ffmpeg_m2v_enc_settings_ctx_deinit(
		volatile ffmpeg_m2v_enc_settings_ctx_t *ffmpeg_m2v_enc_settings_ctx,
		log_ctx_t *log_ctx)
{
	volatile video_settings_enc_ctx_t *video_settings_enc_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(ffmpeg_m2v_enc_settings_ctx!= NULL, return);

	video_settings_enc_ctx=
			&ffmpeg_m2v_enc_settings_ctx->video_settings_enc_ctx;

	/* Release (heap-allocated) generic video encoder settings */
	video_settings_enc_ctx_deinit(video_settings_enc_ctx);

	/* Release specific MPEG-2 video encoder settings */
	// Reserved for future use
}

/**
 * Implements the proc_if_s::open callback.
 * See .proc_if.h for further details.
 */
static proc_ctx_t* ffmpeg_m2v_dec_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	ffmpeg_m2v_dec_ctx_t *ffmpeg_m2v_dec_ctx= NULL;
	volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx=
			NULL; // Do not release
	ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx= NULL; // Do not release
	volatile video_settings_dec_ctx_t *video_settings_dec_ctx=
			NULL; // Do not release
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Note: 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	ffmpeg_m2v_dec_ctx= (ffmpeg_m2v_dec_ctx_t*)calloc(1, sizeof(
			ffmpeg_m2v_dec_ctx_t));
	CHECK_DO(ffmpeg_m2v_dec_ctx!= NULL, goto end);

	/* Get settings structure */
	ffmpeg_m2v_dec_settings_ctx=
			&ffmpeg_m2v_dec_ctx->ffmpeg_m2v_dec_settings_ctx;

	/* Initialize settings to defaults */
	ret_code= ffmpeg_m2v_dec_settings_ctx_init(ffmpeg_m2v_dec_settings_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= ffmpeg_m2v_dec_rest_put((proc_ctx_t*)ffmpeg_m2v_dec_ctx,
			settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Get generic video decoder and settings context structures */
	ffmpeg_video_dec_ctx= &ffmpeg_m2v_dec_ctx->ffmpeg_video_dec_ctx;
	video_settings_dec_ctx= (volatile video_settings_dec_ctx_t*)
			ffmpeg_m2v_dec_settings_ctx;

	/* Initialize FFMPEG's generic video decoder context */
	ret_code= ffmpeg_video_dec_ctx_init(ffmpeg_video_dec_ctx,
			(int)AV_CODEC_ID_MPEG2VIDEO,
			(const video_settings_dec_ctx_t*)video_settings_dec_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

    end_code= STAT_SUCCESS;
 end:
    if(end_code!= STAT_SUCCESS)
    	ffmpeg_m2v_dec_close((proc_ctx_t**)&ffmpeg_m2v_dec_ctx);
	return (proc_ctx_t*)ffmpeg_m2v_dec_ctx;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void ffmpeg_m2v_dec_close(proc_ctx_t **ref_proc_ctx)
{
	ffmpeg_m2v_dec_ctx_t *ffmpeg_m2v_dec_ctx= NULL;

	if(ref_proc_ctx== NULL)
		return;

	if((ffmpeg_m2v_dec_ctx= (ffmpeg_m2v_dec_ctx_t*)*ref_proc_ctx)!= NULL) {
		LOG_CTX_INIT(((proc_ctx_t*)ffmpeg_m2v_dec_ctx)->log_ctx);

		/* De-initialize FFMPEG's generic video decoder context */
		ffmpeg_video_dec_ctx_deinit(&ffmpeg_m2v_dec_ctx->ffmpeg_video_dec_ctx,
				LOG_CTX_GET());

		/* Release settings */
		ffmpeg_m2v_dec_settings_ctx_deinit(
				&ffmpeg_m2v_dec_ctx->ffmpeg_m2v_dec_settings_ctx,
				LOG_CTX_GET());

		// Reserved for future use: release other new variables here...

		/* Release context structure */
		free(ffmpeg_m2v_dec_ctx);
		*ref_proc_ctx= NULL;
	}
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int ffmpeg_m2v_dec_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	ffmpeg_m2v_dec_ctx_t *ffmpeg_m2v_dec_ctx= NULL; // Do not release
	ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx= NULL; // Do not release
	AVPacket *avpacket_iput= NULL;
	size_t fifo_elem_size= 0;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get FFmpeg video decoder context */
	ffmpeg_m2v_dec_ctx= (ffmpeg_m2v_dec_ctx_t*)proc_ctx;
	ffmpeg_video_dec_ctx= &ffmpeg_m2v_dec_ctx->ffmpeg_video_dec_ctx;

	/* Get input packet from FIFO buffer */
	ret_code= fifo_get(iput_fifo_ctx, (void**)&avpacket_iput, &fifo_elem_size);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);
	if(ret_code== STAT_EAGAIN) {
		/* This means FIFO was unblocked, just go out with EOF status */
		end_code= STAT_EOF;
		goto end;
	}

	/* Decode frame */
	ret_code= ffmpeg_video_dec_frame(ffmpeg_video_dec_ctx, avpacket_iput,
			oput_fifo_ctx, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);

	end_code= STAT_SUCCESS;
end:
	if(avpacket_iput!= NULL)
		avpacket_release((void**)&avpacket_iput);
	return end_code;
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int ffmpeg_m2v_dec_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int ret_code;
	ffmpeg_m2v_dec_ctx_t *ffmpeg_m2v_dec_ctx= NULL;
	volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx= NULL;
	volatile video_settings_dec_ctx_t *video_settings_dec_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get FFmpeg video decoder settings contexts */
	ffmpeg_m2v_dec_ctx= (ffmpeg_m2v_dec_ctx_t*)proc_ctx;
	ffmpeg_m2v_dec_settings_ctx=
			&ffmpeg_m2v_dec_ctx->ffmpeg_m2v_dec_settings_ctx;
	video_settings_dec_ctx=
			&ffmpeg_m2v_dec_settings_ctx->video_settings_dec_ctx;

	/* PUT generic video decoder settings */
	ret_code= video_settings_dec_ctx_restful_put(video_settings_dec_ctx, str,
			LOG_CTX_GET());
	if(ret_code!= STAT_SUCCESS)
		return ret_code;

	/* PUT specific MPEG-2 video decoder settings */
	// Reserved for future use

	/* Finally that we have new settings parsed, reset FFMPEG processor */
	ffmpeg_video_reset_on_new_settings(proc_ctx,
			(volatile void*)video_settings_dec_ctx, 0/*Signal is a decoder*/,
			LOG_CTX_GET());

	return STAT_SUCCESS;
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int ffmpeg_m2v_dec_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int ret_code, end_code= STAT_ERROR;
	ffmpeg_m2v_dec_ctx_t *ffmpeg_m2v_dec_ctx= NULL;
	ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx= NULL;
	AVCodecContext *avcodecctx= NULL;
	volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx= NULL;
	volatile video_settings_dec_ctx_t *video_settings_dec_ctx= NULL;
	cJSON *cjson_rest= NULL, *cjson_settings= NULL;
	//cJSON *cjson_aux= NULL; // Do not release // Future use
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
	 *         ...
	 *     },
	 *     ... // reserved for future use
	 * }
	 */

	/* Get FFmpeg video decoder settings contexts */
	ffmpeg_m2v_dec_ctx= (ffmpeg_m2v_dec_ctx_t*)proc_ctx;
	ffmpeg_m2v_dec_settings_ctx=
			&ffmpeg_m2v_dec_ctx->ffmpeg_m2v_dec_settings_ctx;
	video_settings_dec_ctx=
			&ffmpeg_m2v_dec_settings_ctx->video_settings_dec_ctx;

	/* GET generic video decoder settings */
	ret_code= video_settings_dec_ctx_restful_get(video_settings_dec_ctx,
			&cjson_settings, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_settings!= NULL, goto end);

	/* GET specific MPEG-2 video decoder settings */
	// Reserved for future use: attach to 'cjson_settings' (should be != NULL)

	/* Attach settings object to REST response */
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	cjson_settings= NULL; // Attached; avoid double referencing

	/* **** Attach data to REST response **** */

	ffmpeg_video_dec_ctx= &ffmpeg_m2v_dec_ctx->ffmpeg_video_dec_ctx;
	avcodecctx= ffmpeg_video_dec_ctx->avcodecctx;
	CHECK_DO(avcodecctx!= NULL, goto end);

	// Reserved for future use
	/* Example:
	 * cjson_aux= cJSON_CreateNumber((double)avcodecctx->var1);
	 * CHECK_DO(cjson_aux!= NULL, goto end);
	 * cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

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
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

/**
 * Initialize specific MPEG-2 decoder settings to defaults.
 * @param ffmpeg_m2v_dec_settings_ctx
 * @param log_ctx
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
static int ffmpeg_m2v_dec_settings_ctx_init(
		volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx,
		log_ctx_t *log_ctx)
{
	int ret_code;
	volatile video_settings_dec_ctx_t *video_settings_dec_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(ffmpeg_m2v_dec_settings_ctx!= NULL, return STAT_ERROR);

	video_settings_dec_ctx=
			&ffmpeg_m2v_dec_settings_ctx->video_settings_dec_ctx;

	/* Initialize generic video decoder settings */
	ret_code= video_settings_dec_ctx_init(video_settings_dec_ctx);
	if(ret_code!= STAT_SUCCESS)
		return ret_code;

	/* Initialize specific MPEG-2 video decoder settings */
	// Reserved for future use

	return STAT_SUCCESS;
}

/**
 * Release specific MPEG-2 decoder settings (allocated in heap memory).
 * @param ffmpeg_m2v_dec_settings_ctx
 * @param log_ctx
 */
static void ffmpeg_m2v_dec_settings_ctx_deinit(
		volatile ffmpeg_m2v_dec_settings_ctx_t *ffmpeg_m2v_dec_settings_ctx,
		log_ctx_t *log_ctx)
{
	volatile video_settings_dec_ctx_t *video_settings_dec_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(ffmpeg_m2v_dec_settings_ctx!= NULL, return);

	video_settings_dec_ctx=
			&ffmpeg_m2v_dec_settings_ctx->video_settings_dec_ctx;

	/* Release (heap-allocated) generic video decoder settings */
	video_settings_dec_ctx_deinit(video_settings_dec_ctx);

	/* Release specific m2v video decoder settings */
	// Reserved for future use
}
