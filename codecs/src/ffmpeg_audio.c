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
 * @file ffmpeg_audio.c
 * @author Rafael Antoniello
 */

#include "ffmpeg_audio.h"

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

#include "audio_settings.h"

/* **** Definitions **** */

#define LOOP_GUARD_MAX 	20

/* **** Prototypes **** */

/* **** Implementations **** */

int ffmpeg_audio_enc_ctx_init(ffmpeg_audio_enc_ctx_t *ffmpeg_audio_enc_ctx,
		int avcodecid, const audio_settings_enc_ctx_t *audio_settings_enc_ctx,
		log_ctx_t *log_ctx)
{
	const enum AVSampleFormat *samplefmt;
    int loop_guard, ret_code, end_code= STAT_ERROR;
    const AVCodec *avcodec= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release
    AVDictionary *avdictionary= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_audio_enc_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(audio_settings_enc_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Initialize FFmpeg's static CODEC context characterizing audio encoder.
     * Find the encoder and get its static definition structure.
     */
    avcodec= avcodec_find_encoder((enum AVCodecID)avcodecid);
    if(avcodec== NULL) {
        LOGE("Audio encoder not supported '%s'\n", avcodec_get_name(
        		(enum AVCodecID)avcodecid));
        end_code= STAT_EBAVFORMAT;
        goto end;
    }
    ffmpeg_audio_enc_ctx->avcodec= avcodec;
    CHECK_DO(avcodec->type== AVMEDIA_TYPE_AUDIO, goto end);

    /* Initialize FFmpeg's CODEC instance context structure */
    avcodecctx= avcodec_alloc_context3(avcodec);
    CHECK_DO(avcodecctx!= NULL, goto end);
    ffmpeg_audio_enc_ctx->avcodecctx= avcodecctx;

    /* Put settings.
     * NOTE:
     * - Only signed 16 bits planar sample format is supported;
     * - Stereo layout is selected by default.
     */
	avcodecctx->codec_id= avcodecid;
	avcodecctx->bit_rate= audio_settings_enc_ctx->bit_rate_output;
	avcodecctx->sample_fmt= AV_SAMPLE_FMT_NONE;
	for(samplefmt= avcodec->sample_fmts, loop_guard= 0;
			*samplefmt!= AV_SAMPLE_FMT_NONE && loop_guard< LOOP_GUARD_MAX;
			samplefmt++, loop_guard++) {
        if(*samplefmt== AV_SAMPLE_FMT_S16P) {
        	avcodecctx->sample_fmt= AV_SAMPLE_FMT_S16P;
        	break;
        }
	}
	if(avcodecctx->sample_fmt!= AV_SAMPLE_FMT_S16P) {
		LOGE("Unsupported audio sample format %s\n", av_get_sample_fmt_name(
				avcodecctx->sample_fmt));
		goto end;
	}
	avcodecctx->sample_rate= audio_settings_enc_ctx->sample_rate_output;
	avcodecctx->channel_layout= AV_CH_LAYOUT_STEREO;
	avcodecctx->channels= av_get_channel_layout_nb_channels(
			avcodecctx->channel_layout);
	/* Note: It is not necessary to set AVCodecContext::time_base as we do not
	 * use this parameter. For example, PTSs and DTSs are just passed through
	 * by FFmpeg's audio encoders.
	 * avcodecctx->time_base= (AVRational){1, avcodecctx->sample_rate};
	 */

    /* Now that all the parameters are set, we can open the audio encoder and
     * allocate the necessary encoding buffers.
     */
	ret_code= avcodec_open2(ffmpeg_audio_enc_ctx->avcodecctx,
			ffmpeg_audio_enc_ctx->avcodec, NULL);
    if(ret_code< 0) {
        LOGE("Could not open audio encoder: %s.\n", av_err2str(ret_code));
        goto end;
    }

    end_code= STAT_SUCCESS;
end:
	if(avdictionary!= NULL)
		av_dict_free(&avdictionary);
    if(end_code!= STAT_SUCCESS)
    	ffmpeg_audio_enc_ctx_deinit(ffmpeg_audio_enc_ctx, LOG_CTX_GET());
    return end_code;
}

void ffmpeg_audio_enc_ctx_deinit(ffmpeg_audio_enc_ctx_t *ffmpeg_audio_enc_ctx,
		log_ctx_t *log_ctx)
{
	if(ffmpeg_audio_enc_ctx== NULL)
		return;

	if(ffmpeg_audio_enc_ctx->avcodecctx!= NULL)
		avcodec_free_context(&ffmpeg_audio_enc_ctx->avcodecctx);
}

int ffmpeg_audio_enc_frame(ffmpeg_audio_enc_ctx_t *ffmpeg_audio_enc_ctx,
		AVFrame *avframe_iput, fifo_ctx_t* oput_fifo_ctx, log_ctx_t *log_ctx)
{
	const proc_if_t *proc_if;
	uint64_t flag_proc_features;
    int ret_code, end_code= STAT_ERROR;
    proc_ctx_t *proc_ctx= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release
    //AVRational src_time_base= {1, 1000000}; //[usec] // Not used
    AVPacket pkt_oput= {0};
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_audio_enc_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(avframe_iput!= NULL, return STAT_ERROR);
    CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Get (cast to) processor context structure */
    proc_ctx= (proc_ctx_t*)ffmpeg_audio_enc_ctx;

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	flag_proc_features= proc_if->flag_proc_features;

    /* Get audio CODEC context */
    avcodecctx= ffmpeg_audio_enc_ctx->avcodecctx;
    CHECK_DO(avcodecctx!= NULL, goto end);

	/* Initialize output audio packet */
	av_init_packet(&pkt_oput);

	/* Change time-stamp base before encoding */
	//LOGV("Input frame: pts: %"PRId64"\n", avframe_iput->pts); //comment-me
	//avframe_iput->pts= av_rescale_q(avframe_iput->pts, src_time_base,
	//		avcodecctx->time_base); // Not necessary

    /* Send the frame to the encoder */
    ret_code= avcodec_send_frame(avcodecctx, avframe_iput);
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

    	/* Restore time-stamps base */
        //pkt_oput.pts= av_rescale_q(pkt_oput.pts, avcodecctx->time_base,
    	//		src_time_base); // Not necessary
        //LOGV("Output frame: pts: %"PRId64" (size=%d)\n", pkt_oput.pts,
        //		pkt_oput.size); //comment-me

        /* Set sampling rate at output frame.
         * HACK- implementation note:
         * We use AVPacket::pos field to pass 'sampling rate' as
         * no specific field exist for this parameter.
         */
        pkt_oput.pos= avcodecctx->sample_rate;

        /* Latency statistics related */
        if((flag_proc_features&PROC_FEATURE_LATSTATS) &&
        		pkt_oput.pts!= AV_NOPTS_VALUE)
        	proc_acc_latency_measure(proc_ctx, pkt_oput.pts);

		/* Put output frame into output FIFO */
        fifo_put_dup(oput_fifo_ctx, &pkt_oput, sizeof(void*));
    }

	end_code= STAT_SUCCESS;
end:
	av_packet_unref(&pkt_oput);
    return end_code;
}

int ffmpeg_audio_dec_ctx_init(ffmpeg_audio_dec_ctx_t *ffmpeg_audio_dec_ctx,
		int avcodecid, const audio_settings_dec_ctx_t *audio_settings_dec_ctx,
		log_ctx_t *log_ctx)
{
	char *fmt_output;
    int ret_code, end_code= STAT_ERROR;
    const AVCodec *avcodec= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_audio_dec_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(audio_settings_dec_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Initialize FFmpeg's static CODEC context characterizing audio decoder.
     * Find the decoder and get its static definition structure.
     */
    avcodec= avcodec_find_decoder((enum AVCodecID)avcodecid);
    if(avcodec== NULL) {
        LOGE("Audio decoder not supported '%s'\n", avcodec_get_name(
        		(enum AVCodecID)avcodecid));
        end_code= STAT_EBAVFORMAT;
        goto end;
    }
    ffmpeg_audio_dec_ctx->avcodec= avcodec;
    CHECK_DO(avcodec->type== AVMEDIA_TYPE_AUDIO, goto end);

    /* Initialize FFmpeg's CODEC instance context structure */
    avcodecctx= avcodec_alloc_context3(avcodec);
    CHECK_DO(avcodecctx!= NULL, goto end);
    ffmpeg_audio_dec_ctx->avcodecctx= avcodecctx;

    /* Initialize user specified samples output format
     * (may differ from native-decoder format).
     */
    fmt_output= audio_settings_dec_ctx->samples_format_output;
    CHECK_DO(fmt_output!= NULL, goto end);
    ffmpeg_audio_dec_ctx->sample_fmt_output= AV_SAMPLE_FMT_NONE;
    if(strncmp(fmt_output, "planar_signed_16b",
    		strlen("planar_signed_16b"))== 0) {
    	ffmpeg_audio_dec_ctx->sample_fmt_output= AV_SAMPLE_FMT_S16P;
    } else if(strncmp(fmt_output, "interleaved_signed_16b",
    		strlen("interleaved_signed_16b"))== 0) {
    	ffmpeg_audio_dec_ctx->sample_fmt_output= AV_SAMPLE_FMT_S16;
    }
    CHECK_DO(ffmpeg_audio_dec_ctx->sample_fmt_output!= AV_SAMPLE_FMT_NONE,
    		goto end);

    /* Put settings */
	/* Note: It is not necessary to set AVCodecContext::time_base as we do not
	 * use this parameter. For example, PTSs and DTSs are just passed through
	 * by FFmpeg's audio decoders.
	 * avcodecctx->time_base= (AVRational){1, 'sample_rate'};
	 */
    // Reserved for future use: put other new settings here...

    /* Now that all the parameters are set, we can open the audio decoder */
	ret_code= avcodec_open2(ffmpeg_audio_dec_ctx->avcodecctx,
			ffmpeg_audio_dec_ctx->avcodec, NULL);
    if(ret_code< 0) {
        LOGE("Could not open audio decoder: %s.\n", av_err2str(ret_code));
        goto end;
    }

    end_code= STAT_SUCCESS;
end:
    if(end_code!= STAT_SUCCESS)
    	ffmpeg_audio_dec_ctx_deinit(ffmpeg_audio_dec_ctx, LOG_CTX_GET());
    return end_code;
}

void ffmpeg_audio_dec_ctx_deinit(ffmpeg_audio_dec_ctx_t *ffmpeg_audio_dec_ctx,
		log_ctx_t *log_ctx)
{
	if(ffmpeg_audio_dec_ctx== NULL)
		return;

	if(ffmpeg_audio_dec_ctx->avcodecctx!= NULL)
		avcodec_free_context(&ffmpeg_audio_dec_ctx->avcodecctx);
}

int ffmpeg_audio_dec_frame(ffmpeg_audio_dec_ctx_t *ffmpeg_audio_dec_ctx,
		AVPacket *avpacket_iput, fifo_ctx_t* oput_fifo_ctx, log_ctx_t *log_ctx)
{
	const proc_if_t *proc_if;
	uint64_t flag_proc_features;
    int ret_code, end_code= STAT_ERROR;
    proc_ctx_t *proc_ctx= NULL; // Do not release
    AVCodecContext *avcodecctx= NULL; // Do not release;
    //AVRational src_time_base= {1, 1000000}; //[usec] // Not used
    AVFrame *avframe_oput= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(ffmpeg_audio_dec_ctx!= NULL, return STAT_ERROR);
    CHECK_DO(avpacket_iput!= NULL, return STAT_ERROR);
    CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);
    // Note: argument 'log_ctx' is allowed to be NULL

    /* Get (cast to) processor context structure */
    proc_ctx= (proc_ctx_t*)ffmpeg_audio_dec_ctx;

	/* Get required variables from PROC interface structure */
	proc_if= proc_ctx->proc_if;
	CHECK_DO(proc_if!= NULL, goto end);
	flag_proc_features= proc_if->flag_proc_features;

    /* Get audio CODEC context */
    avcodecctx= ffmpeg_audio_dec_ctx->avcodecctx;
    CHECK_DO(avcodecctx!= NULL, goto end);

	/* Change time-stamps base before decoding */
    //LOGV("Input frame: pts: %"PRId64"\n", avpacket_iput->pts); //comment-me
    //avpacket_iput->pts= av_rescale_q(avpacket_iput->pts, src_time_base,
	//		avcodecctx->time_base); // Not necessary

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

    	/* Restore time-stamps base */
        //avframe_oput->pts= av_rescale_q(avframe_oput->pts,
        //		avcodecctx->time_base, src_time_base); // Not necessary
        //LOGV("Output frame: pts: %"PRId64"\n",
        //		avframe_oput->pts); //comment-me

        /* Set format to use in 'fifo_put_dup()' */
        avframe_oput->format= ffmpeg_audio_dec_ctx->sample_fmt_output;

        /* Set sampling rate at output frame */
        avframe_oput->sample_rate= avcodecctx->sample_rate;

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

void ffmpeg_audio_reset_on_new_settings(proc_ctx_t *proc_ctx,
		volatile void *audio_settings_opaque, int flag_is_encoder,
		log_ctx_t *log_ctx)
{
    int ret_code, flag_io_locked= 0, flag_thr_joined= 0;
    void *thread_end_code= NULL;
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
		ffmpeg_audio_enc_ctx_t *ffmpeg_audio_enc_ctx= (ffmpeg_audio_enc_ctx_t*)
				proc_ctx;
		audio_settings_enc_ctx_t *audio_settings_enc_ctx=
				(audio_settings_enc_ctx_t*)audio_settings_opaque;

	    /* Get audio CODEC context and CODEC Id. */
	    avcodecctx= ffmpeg_audio_enc_ctx->avcodecctx;
	    CHECK_DO(avcodecctx!= NULL, goto end);
	    avcodecid= avcodecctx->codec_id;

		ffmpeg_audio_enc_ctx_deinit(ffmpeg_audio_enc_ctx, LOG_CTX_GET());
		ret_code= ffmpeg_audio_enc_ctx_init(ffmpeg_audio_enc_ctx,
				(int)avcodecid, audio_settings_enc_ctx, LOG_CTX_GET());
		CHECK_DO(ret_code== STAT_SUCCESS, goto end);
	} else {
	    enum AVCodecID avcodecid;
	    AVCodecContext *avcodecctx= NULL; // Do not release;
		ffmpeg_audio_dec_ctx_t *ffmpeg_audio_dec_ctx= (ffmpeg_audio_dec_ctx_t*)
				proc_ctx;
		audio_settings_dec_ctx_t *audio_settings_dec_ctx=
				(audio_settings_dec_ctx_t*)audio_settings_opaque;

	    /* Get audio CODEC context and CODEC Id. */
	    avcodecctx= ffmpeg_audio_dec_ctx->avcodecctx;
	    CHECK_DO(avcodecctx!= NULL, goto end);
	    avcodecid= avcodecctx->codec_id;

		ffmpeg_audio_dec_ctx_deinit(ffmpeg_audio_dec_ctx, LOG_CTX_GET());
		ret_code= ffmpeg_audio_dec_ctx_init(ffmpeg_audio_dec_ctx,
				(int)avcodecid, audio_settings_dec_ctx, LOG_CTX_GET());
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
	return;
}
