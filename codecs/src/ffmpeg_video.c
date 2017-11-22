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
 * @file ffmpeg_video.c
 * @author Rafael Antoniello
 */

#include "ffmpeg_video.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/fair_lock.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/proc.h>
#include "proc_frame_2_ffmpeg.h"
#include "video_settings.h"

/* **** Definitions **** */

/* **** Prototypes **** */

/* **** Implementations **** */

int ffmpeg_video_enc_ctx_init(ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx,
		int avcodecid, const video_settings_enc_ctx_t *video_settings_enc_ctx,
		log_ctx_t *log_ctx)
{
    int ret_code, end_code= STAT_ERROR;
    const AVCodec *avcodec= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release
    AVDictionary *avdictionary= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_video_enc_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(video_settings_enc_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Initialize FFmpeg's static CODEC context characterizing video encoder.
     * Find the encoder and get its static definition structure.
     */
    avcodec= avcodec_find_encoder((enum AVCodecID)avcodecid);
    if(avcodec== NULL) {
        LOGE("Video encoder not supported '%s'\n", avcodec_get_name(
        		(enum AVCodecID)avcodecid));
        end_code= STAT_EBAVFORMAT;
        goto end;
    }
    ffmpeg_video_enc_ctx->avcodec= avcodec;
    CHECK_DO(avcodec->type== AVMEDIA_TYPE_VIDEO, goto end);

    /* Initialize FFmpeg's CODEC instance context structure */
    avcodecctx= avcodec_alloc_context3(avcodec);
    CHECK_DO(avcodecctx!= NULL, goto end);
    ffmpeg_video_enc_ctx->avcodecctx= avcodecctx;

    /* Put settings */
	avcodecctx->codec_id= avcodecid;
	avcodecctx->bit_rate= video_settings_enc_ctx->bit_rate_output;
	avcodecctx->framerate= (AVRational) {
		video_settings_enc_ctx->frame_rate_output, 1};
	ffmpeg_video_enc_ctx->frame_rate_input=
			video_settings_enc_ctx->frame_rate_output;
    /* Time-base: This is the fundamental unit of time (in seconds) in terms
     * of which frame time-stamps are represented. For fixed-fps content,
     * time-base should be 1/frame-rate and time-stamp increments should be
     * identical to 1. */
	avcodecctx->time_base= (AVRational)
			{1, video_settings_enc_ctx->frame_rate_output};
	avcodecctx->width= ffmpeg_video_enc_ctx->width_input=
			video_settings_enc_ctx->width_output;
	avcodecctx->height= ffmpeg_video_enc_ctx->height_input=
			video_settings_enc_ctx->height_output;
	avcodecctx->gop_size= video_settings_enc_ctx->gop_size;
	avcodecctx->pix_fmt= ffmpeg_video_enc_ctx->ffmpeg_pix_fmt_input=
			AV_PIX_FMT_YUV420P; // natively supported
	if(strlen(video_settings_enc_ctx->conf_preset)> 0) {
		av_opt_set(avcodecctx->priv_data, "preset",
				video_settings_enc_ctx->conf_preset, 0);
	}

    /* Initialize FFmpeg's dictionary structure used for storing key:value
     * pairs for specific encoder configuration options.
     */
    // reserved for future use: put dictionary settings
    ret_code= av_dict_copy(&avdictionary, ffmpeg_video_enc_ctx->avdictionary,
    		0);
    CHECK_DO(ret_code== 0, goto end);

    /* Allocate temporally intermediate buffer for re-sampling.
     * If raw input format is not YUV420P or if input spatial resolution is
     * different from output resolution, we would need a temporary buffer to
     * convert to the required encoder pixel format and output resolution.
     * Note that for FFmpeg video CODECS, every time a setting is changed, we
     * perform a 'ffmpeg_video_enc_ctx_deinit()' and a
     * 'ffmpeg_video_enc_ctx_init()'; this ensures that all resources, as this
     * temporally buffer, are adequately reset to current settings.
     */
    ffmpeg_video_enc_ctx->avframe_tmp= allocate_frame_video(AV_PIX_FMT_YUV420P,
    		avcodecctx->width, avcodecctx->height);
	if(ffmpeg_video_enc_ctx->avframe_tmp== NULL) {
		LOGE("Could not allocate temporal video raw frame.\n");
		goto end;
	}

    /* Conversion module 'sws_ctx' will be updated in the processing thread
	 * according to the input frame information. This is because input format
	 * or resolution may change at any time. We can initialize our temporally
	 * intermediate buffer 'avframe_tmp', as the CODEC ("destination")
	 * parameters are at this point set and known, but inpur ("source")
	 * parameters are volatile.
     */
    ffmpeg_video_enc_ctx->sws_ctx= NULL;

    /* Now that all the parameters are set, we can open the video encoder and
     * allocate the necessary encoding buffers.
     */
	ret_code= avcodec_open2(ffmpeg_video_enc_ctx->avcodecctx,
			ffmpeg_video_enc_ctx->avcodec, &ffmpeg_video_enc_ctx->avdictionary);
    if(ret_code< 0) {
        LOGE("Could not open video encoder: %s.\n", av_err2str(ret_code));
        goto end;
    }

    end_code= STAT_SUCCESS;
end:
	if(avdictionary!= NULL)
		av_dict_free(&avdictionary);
    if(end_code!= STAT_SUCCESS)
    	ffmpeg_video_enc_ctx_deinit(ffmpeg_video_enc_ctx, LOG_CTX_GET());
    return end_code;
}

void ffmpeg_video_enc_ctx_deinit(ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx,
		log_ctx_t *log_ctx)
{
	if(ffmpeg_video_enc_ctx== NULL)
		return;

	if(ffmpeg_video_enc_ctx->avcodecctx!= NULL)
		avcodec_free_context(&ffmpeg_video_enc_ctx->avcodecctx);

	if(ffmpeg_video_enc_ctx->avdictionary!= NULL) {
		av_dict_free(&ffmpeg_video_enc_ctx->avdictionary);
		ffmpeg_video_enc_ctx->avdictionary= NULL;
	}

	if(ffmpeg_video_enc_ctx->avframe_tmp!= NULL)
		av_frame_free(&ffmpeg_video_enc_ctx->avframe_tmp);

	if(ffmpeg_video_enc_ctx->sws_ctx!= NULL) {
		sws_freeContext(ffmpeg_video_enc_ctx->sws_ctx);
		ffmpeg_video_enc_ctx->sws_ctx= NULL;
	}
}

int ffmpeg_video_enc_frame(ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx,
		AVFrame *avframe_iput, fifo_ctx_t* oput_fifo_ctx, log_ctx_t *log_ctx)
{
	register int prev_pix_fmt_iput, pix_fmt_iput, pix_fmt_native_codec;
	register int prev_width_iput, prev_height_iput, width_iput, height_iput,
		width_codec_oput, height_codec_oput;
	const proc_if_t *proc_if;
	uint64_t flag_proc_features;
    int ret_code, end_code= STAT_ERROR;
    proc_ctx_t *proc_ctx= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release
    AVFrame *avframe_p= NULL; // Do not release
	AVFrame *avframe_tmp= NULL;
    struct SwsContext *sws_ctx= NULL;
    AVPacket pkt_oput= {0};
    //AVRational src_time_base= {1, 90000}; //[sec]
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_video_enc_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(avframe_iput!= NULL, return STAT_ERROR);
    CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Get (cast to) processor context structure */
    proc_ctx= (proc_ctx_t*)ffmpeg_video_enc_ctx;

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	flag_proc_features= proc_if->flag_proc_features;

    /* Get video CODEC context */
    avcodecctx= ffmpeg_video_enc_ctx->avcodecctx;
    CHECK_DO(avcodecctx!= NULL, goto end);

	/* Initialize output video packet */
	av_init_packet(&pkt_oput);

	/* Initialize pixel format related variables */
	prev_pix_fmt_iput= ffmpeg_video_enc_ctx->ffmpeg_pix_fmt_input;
	pix_fmt_iput= avframe_iput->format;
	pix_fmt_native_codec= avcodecctx->pix_fmt;

	/* Initialize spatial resolution related variables */
	prev_width_iput= ffmpeg_video_enc_ctx->width_input;
	prev_height_iput= ffmpeg_video_enc_ctx->height_input;
	width_iput= avframe_iput->width;
	height_iput= avframe_iput->height;
	width_codec_oput= avcodecctx->width;
	height_codec_oput= avcodecctx->height;

	/* We will use a variable pointer to the input frame. Initially, it points
	 * to the actual input frame. But if scaling is to be applied, we will feed
	 * the encoder with a temporally frame instead.
	 */
	avframe_p= avframe_iput;

	/* Convert input frame to the encoder pixel format if applicable. Firstly
	 * we have to check if our conversion module is well initialized
	 * (input parameters may change any time).
	 * If not, we have to re-initialize.
	 */
	if(prev_pix_fmt_iput!= pix_fmt_iput || prev_width_iput!= width_iput ||
			prev_height_iput!= height_iput) {

		/* Check if input format is known */
		if(pix_fmt_iput== AV_PIX_FMT_NONE) {
			LOGE("Unknown or not supported input pixel format\n");
			goto end;
		}

		/* Re-initialize conversion module */
		sws_ctx= sws_getContext(width_iput, height_iput, pix_fmt_iput,
				width_codec_oput, height_codec_oput, AV_PIX_FMT_YUV420P,
				SCALE_FLAGS, NULL, NULL, NULL);
		CHECK_DO(sws_ctx!= NULL, goto end);
		if(ffmpeg_video_enc_ctx->sws_ctx!= NULL) // release old
			sws_freeContext(ffmpeg_video_enc_ctx->sws_ctx);
		ffmpeg_video_enc_ctx->sws_ctx= sws_ctx;
		sws_ctx= NULL; // Avoid double referencing

		/* Update "previous" frame variables to actual values */
		ffmpeg_video_enc_ctx->width_input= width_iput;
		ffmpeg_video_enc_ctx->height_input= height_iput;
		ffmpeg_video_enc_ctx->ffmpeg_pix_fmt_input= pix_fmt_iput;
	}

	/* Actually convert input frame format/size to the encoder format/size if
	 * applicable.
	 */
	if(pix_fmt_iput!= pix_fmt_native_codec || width_iput!= width_codec_oput ||
			height_iput!= height_codec_oput) {
		CHECK_DO(ffmpeg_video_enc_ctx->sws_ctx!= NULL, goto end);
		CHECK_DO(ffmpeg_video_enc_ctx->avframe_tmp!= NULL, goto end);
		sws_scale(ffmpeg_video_enc_ctx->sws_ctx,
				(const uint8_t* const*)avframe_p->data, // source
				avframe_p->linesize, // source
				0/*Y offset*/, avframe_p->height, // source
				ffmpeg_video_enc_ctx->avframe_tmp->data, // destination
				ffmpeg_video_enc_ctx->avframe_tmp->linesize // destination
		);
		ffmpeg_video_enc_ctx->avframe_tmp->pts= avframe_iput->pts;
		avframe_p= ffmpeg_video_enc_ctx->avframe_tmp;
	}

	/* Change time-stamp base before encoding */ //TODO: discard frames
	//avframe_p->pts= av_rescale_q(avframe_p->pts, src_time_base,
	//		avcodecctx->time_base);

    /* Send frame to the encoder */
    //LOGV("Frame: %dx%d pts: %"PRId64"\n", avframe_p->width, avframe_p->height,
    //		avframe_p->pts); //comment-me
    ret_code= avcodec_send_frame(avcodecctx, avframe_p);
    CHECK_DO(ret_code>= 0, goto end);

    /* Read output packet from the encoder and put into output FIFO buffer */
    while(ret_code>= 0 && proc_ctx->flag_exit== 0) {


    	av_packet_unref(&pkt_oput);
    	ret_code= avcodec_receive_packet(avcodecctx, &pkt_oput);
        if(ret_code== AVERROR(EAGAIN) || ret_code== AVERROR_EOF) {
            end_code= STAT_EAGAIN;
            goto end;
        }
        CHECK_DO(ret_code>= 0, goto end);

    	/* Restore time-stamps base */ //Not used
        /*pkt_oput.pts= av_rescale_q(pkt_oput.pts, avcodecctx->time_base,
    			src_time_base);
        pkt_oput.dts= av_rescale_q(pkt_oput.dts, avcodecctx->time_base,
    			src_time_base);*/
        //LOGV("Write frame: pts: %"PRId64" dts: %"PRId64" (size=%d)\n",
        //		pkt_oput.pts, pkt_oput.dts, pkt_oput.size); //comment-me

        /* Set sampling rate at output frame.
         * HACK- implementation note:
         * We use AVPacket::pos field to pass 'sampling rate' as
         * no specific field exist for this parameter.
         */
        pkt_oput.pos= avcodecctx->framerate.num;

        /* Latency statistics related */
        if((flag_proc_features&PROC_FEATURE_LATSTATS) &&
        		pkt_oput.pts!= AV_NOPTS_VALUE)
        	proc_acc_latency_measure(proc_ctx, pkt_oput.pts);

		/* Put output frame into output FIFO */
        fifo_put_dup(oput_fifo_ctx, &pkt_oput, sizeof(void*));
    }

	end_code= STAT_SUCCESS;
end:
	if(avframe_tmp!= NULL)
		av_frame_free(&avframe_tmp);
	if(sws_ctx!= NULL)
		sws_freeContext(sws_ctx);
	av_packet_unref(&pkt_oput);
    return end_code;
}

int ffmpeg_video_dec_ctx_init(ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx,
		int avcodecid, const video_settings_dec_ctx_t *video_settings_dec_ctx,
		log_ctx_t *log_ctx)
{
    int ret_code, end_code= STAT_ERROR;
    const AVCodec *avcodec= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_video_dec_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(video_settings_dec_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Initialize FFmpeg's static CODEC context characterizing video decoder.
     * Find the decoder and get its static definition structure.
     */
    avcodec= avcodec_find_decoder((enum AVCodecID)avcodecid);
    if(avcodec== NULL) {
        LOGE("Video decoder not supported '%s'\n", avcodec_get_name(
        		(enum AVCodecID)avcodecid));
        end_code= STAT_EBAVFORMAT;
        goto end;
    }
    ffmpeg_video_dec_ctx->avcodec= avcodec;
    CHECK_DO(avcodec->type== AVMEDIA_TYPE_VIDEO, goto end);

    /* Initialize FFmpeg's CODEC instance context structure */
    avcodecctx= avcodec_alloc_context3(avcodec);
    CHECK_DO(avcodecctx!= NULL, goto end);
    ffmpeg_video_dec_ctx->avcodecctx= avcodecctx;

    /* Put settings */
    avcodecctx->codec_id= avcodecid;
    // { //RAL //TODO: this is to discuss with LHE CODEC designers
    // The following initialization is not necessary for most of decoders
    // (as descos should be able to adapt pixel-format and resolution to input
    // frame). Nevertheless, we add a default initialization here to
    // support specific descos as LHE.
    avcodecctx->pix_fmt= AV_PIX_FMT_YUV420P; // natively supported
	avcodecctx->width= 352;
	avcodecctx->height= 288;
	// {
    //if(avcodec->capabilities& AV_CODEC_CAP_TRUNCATED) // Do not use!!!
    //	avcodecctx->flags|=
    //			AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames

    // Reserved for future use: put other new settings here...

    /* Now that all the parameters are set, we can open the video decoder */
	ret_code= avcodec_open2(ffmpeg_video_dec_ctx->avcodecctx,
			ffmpeg_video_dec_ctx->avcodec, NULL);
    if(ret_code< 0) {
        LOGE("Could not open video decoder: %s.\n", av_err2str(ret_code));
        goto end;
    }

    end_code= STAT_SUCCESS;
end:
    if(end_code!= STAT_SUCCESS)
    	ffmpeg_video_dec_ctx_deinit(ffmpeg_video_dec_ctx, LOG_CTX_GET());
    return end_code;
}

void ffmpeg_video_dec_ctx_deinit(ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx,
		log_ctx_t *log_ctx)
{
	if(ffmpeg_video_dec_ctx== NULL)
		return;

	if(ffmpeg_video_dec_ctx->avcodecctx!= NULL)
		avcodec_free_context(&ffmpeg_video_dec_ctx->avcodecctx);
}

int ffmpeg_video_dec_frame(ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx,
		AVPacket *avpacket_iput, fifo_ctx_t* oput_fifo_ctx, log_ctx_t *log_ctx)
{
	const proc_if_t *proc_if;
	uint64_t flag_proc_features;
    int ret_code, end_code= STAT_ERROR;
    proc_ctx_t *proc_ctx= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release;
    //AVRational src_time_base= {1, 1000000}; //[usec]
    AVFrame *avframe_oput= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_video_dec_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(avpacket_iput!= NULL, return STAT_ERROR);
    CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Get (cast to) processor context structure */
    proc_ctx= (proc_ctx_t*)ffmpeg_video_dec_ctx;

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	flag_proc_features= proc_if->flag_proc_features;

    /* Get video CODEC context */
    avcodecctx= ffmpeg_video_dec_ctx->avcodecctx;
    CHECK_DO(avcodecctx!= NULL, goto end);

	/* Change time-stamps base before decoding */ //TODO: discard frames
    //avpacket_iput->pts= av_rescale_q(avpacket_iput->pts, src_time_base,
    //		avcodecctx->time_base);
    //avpacket_iput->dts= av_rescale_q(avpacket_iput->dts, src_time_base,
    //		avcodecctx->time_base);

    /* Send the packet to the decoder */
	ret_code= avcodec_send_packet(avcodecctx, avpacket_iput);
    CHECK_DO(ret_code>= 0, goto end);

    /* Read output frame from the decoder and put into output FIFO buffer */
    while(ret_code>= 0 && proc_ctx->flag_exit== 0) {
    	if(avframe_oput!= NULL)
    		av_frame_free(&avframe_oput);
    	avframe_oput= av_frame_alloc();
    	CHECK_DO(avframe_oput!= NULL, goto end);
    	ret_code= avcodec_receive_frame(avcodecctx, avframe_oput);
        if(ret_code== AVERROR(EAGAIN) || ret_code== AVERROR_EOF) {
            end_code= STAT_EAGAIN;
            goto end;
        }
        CHECK_DO(ret_code>= 0, goto end);

    	/* Restore time-stamps base */ //Not used
        //avframe_oput->pts= av_rescale_q(avframe_oput->pts,
        //		avcodecctx->time_base, src_time_base);
        //LOGV("Output frame: %dx%d pts: %"PRId64"\n", avframe_oput->width,
        //		avframe_oput->height, avframe_oput->pts); //comment-me

        /* Set sampling rate at output frame.
         * HACK- implementation note:
         * We use AVFrame::sample_rate field to pass 'sampling rate' as
         * no specific video field exist for this parameter.
         */
        avframe_oput->sample_rate= avcodecctx->framerate.num;

        /* Latency statistics related */
        if((flag_proc_features&PROC_FEATURE_LATSTATS) &&
        		avframe_oput->pts!= AV_NOPTS_VALUE)
        	proc_acc_latency_measure(proc_ctx, avframe_oput->pts);

		/* Put output frame into output FIFO */
    	fifo_put_dup(oput_fifo_ctx, avframe_oput, sizeof(void*));
    }

	end_code= STAT_SUCCESS;
end:
	if(avframe_oput!= NULL)
		av_frame_free(&avframe_oput);
    return end_code;
}

void ffmpeg_video_reset_on_new_settings(proc_ctx_t *proc_ctx,
		volatile void *video_settings_opaque, int flag_is_encoder,
		log_ctx_t *log_ctx)
{
    int ret_code, flag_io_locked= 0, flag_thr_joined= 0;
    void *thread_end_code= NULL;
    AVDictionary *avdictionary= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(proc_ctx!= NULL, return);

    /* If processor interface was not set yet, it means this function is being
     * call in processor opening phase, so it must be skipped.
     */
    if(proc_ctx->proc_if== NULL)
    	return;

    /* Firstly, stop processing thread:
     * - Signal processing to end;
     * - Unlock i/o FIFOs;
     * - Lock i/o critical section (to make FIFOs unreachable);
     * - Join the thread.
     * IMPORTANT: *do not* set a jump here (return or goto)
     */
	proc_ctx->flag_exit= 1;
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 0);
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 0);
	fair_lock(proc_ctx->fair_lock_io_array[PROC_IPUT]);
	fair_lock(proc_ctx->fair_lock_io_array[PROC_OPUT]);
	flag_io_locked= 1;
	//LOGV("Waiting thread to join... "); // comment-me
	pthread_join(proc_ctx->proc_thread, &thread_end_code);
	if(thread_end_code!= NULL) {
		ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
		free(thread_end_code);
		thread_end_code= NULL;
	}
	//LOGV("joined O.K.\n"); // comment-me
	flag_thr_joined= 1;

	/* Empty i/o FIFOs */
	fifo_empty(proc_ctx->fifo_ctx_array[PROC_IPUT]);
	fifo_empty(proc_ctx->fifo_ctx_array[PROC_OPUT]);

	/* Reset FFmpeg resources */
	if(flag_is_encoder!= 0) {
	    enum AVCodecID avcodecid;
	    AVCodecContext *avcodecctx= NULL; // Do not release;
		ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx= (ffmpeg_video_enc_ctx_t*)
				proc_ctx;
		video_settings_enc_ctx_t *video_settings_enc_ctx=
				(video_settings_enc_ctx_t*)video_settings_opaque;

	    /* Get video CODEC context and CODEC Id. */
	    avcodecctx= ffmpeg_video_enc_ctx->avcodecctx;
	    CHECK_DO(avcodecctx!= NULL, goto end);
	    avcodecid= avcodecctx->codec_id;

	    /* Back-up dictionary */
	    ret_code= av_dict_copy(&avdictionary,
	    		ffmpeg_video_enc_ctx->avdictionary, 0);
	    CHECK_DO(ret_code== 0, goto end);

	    /* De-initialize FFmpeg's video encoder */
		ffmpeg_video_enc_ctx_deinit(ffmpeg_video_enc_ctx, LOG_CTX_GET());

	    /* Restore dictionary */
	    ret_code= av_dict_copy(&ffmpeg_video_enc_ctx->avdictionary,
	    		avdictionary, 0);
	    CHECK_DO(ret_code== 0, goto end);

		ret_code= ffmpeg_video_enc_ctx_init(ffmpeg_video_enc_ctx,
				(int)avcodecid, video_settings_enc_ctx, LOG_CTX_GET());
		CHECK_DO(ret_code== STAT_SUCCESS, goto end);
	} else {
	    enum AVCodecID avcodecid;
	    AVCodecContext *avcodecctx= NULL; // Do not release;
		ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx= (ffmpeg_video_dec_ctx_t*)
				proc_ctx;
		video_settings_dec_ctx_t *video_settings_dec_ctx=
				(video_settings_dec_ctx_t*)video_settings_opaque;

	    /* Get video CODEC context and CODEC Id. */
	    avcodecctx= ffmpeg_video_dec_ctx->avcodecctx;
	    CHECK_DO(avcodecctx!= NULL, goto end);
	    avcodecid= avcodecctx->codec_id;

		ffmpeg_video_dec_ctx_deinit(ffmpeg_video_dec_ctx, LOG_CTX_GET());
		ret_code= ffmpeg_video_dec_ctx_init(ffmpeg_video_dec_ctx,
				(int)avcodecid, video_settings_dec_ctx, LOG_CTX_GET());
		CHECK_DO(ret_code== STAT_SUCCESS, goto end);
	}

end:
	/* Restore FIFOs blocking mode if applicable */
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 1);
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 1);

	/* Re-launch PROC thread if applicable */
	if(flag_thr_joined!= 0) {
		proc_ctx->flag_exit= 0;
		ret_code= pthread_create(&proc_ctx->proc_thread, NULL,
				(void*(*)(void*))proc_ctx->start_routine, proc_ctx);
		CHECK_DO(ret_code== 0, goto end);
	}

	/* Unlock i/o critical sections if applicable */
	if(flag_io_locked!= 0) {
		fair_unlock(proc_ctx->fair_lock_io_array[PROC_IPUT]);
		fair_unlock(proc_ctx->fair_lock_io_array[PROC_OPUT]);
	}

	if(avdictionary!= NULL)
		av_dict_free(&avdictionary);
	return;
}
