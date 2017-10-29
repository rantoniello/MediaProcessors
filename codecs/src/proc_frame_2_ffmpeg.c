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
 * @file proc_frame_2_ffmpeg.c
 * @author Rafael Antoniello
 */

#include "proc_frame_2_ffmpeg.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/mem_utils.h>
#include <libmediaprocs/proc_if.h>

/* **** Definitions **** */

/* **** Prototypes **** */

static int proc_sample_fmt_2_ffmpegfmt(proc_sample_fmt_t proc_sample_fmt);
static proc_sample_fmt_t ffmpegfmt_2_proc_sample_fmt(int ffmpegfmt);

/* **** Implementations **** */

void* proc_frame_ctx_2_avframe(const proc_frame_ctx_t *proc_frame_ctx)
{
	register int i, ffmpeg_fmt, end_code= STAT_ERROR;
	AVFrame *avframe= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_frame_ctx!= NULL, return NULL);

	/* Get FFmpeg's sampling format */
	ffmpeg_fmt= proc_sample_fmt_2_ffmpegfmt(proc_frame_ctx->proc_sample_fmt);

	/* Allocate frame buffer and initialize frame related parameters according
	 * to the given sampling format.
	 */
	switch(ffmpeg_fmt) {
		register int width_Y, height_Y;
	case AV_PIX_FMT_YUV420P:

		/* Get Y width and height according to input data planes */
		width_Y= proc_frame_ctx->width[0];
		if(width_Y< 1 || width_Y> PROC_FRAME_MAX_WIDTH) {
			LOGE("Frame width= %d out of bounds (range is 1..%d)\n",
					width_Y, PROC_FRAME_MAX_WIDTH);
			goto end;
		}
		height_Y= proc_frame_ctx->height[0];
		if(height_Y< 1 || height_Y> PROC_FRAME_MAX_HEIGHT) {
			LOGE("Frame height= %d out of bounds (range is 1..%d)\n",
					height_Y, PROC_FRAME_MAX_HEIGHT);
			goto end;
		}

		/* Allocate FFmpeg's AV-frame structure */
		avframe= allocate_frame_video(ffmpeg_fmt, width_Y, height_Y);
		CHECK_DO(avframe!= NULL, goto end);

		/* Copy data planes */
		for(i= 0; i< PROC_FRAME_NUM_DATA_POINTERS; i++) {
			register int l, lsize_src, lsize_dst, width, height;
			const uint8_t *data_src= proc_frame_ctx->p_data[i];
			uint8_t *data_dst= avframe->data[i];
			if(data_src== NULL)
				continue; // No data for this plane
			CHECK_DO(data_dst!= NULL, goto end);

			/* Copy plane data */
			lsize_src= proc_frame_ctx->linesize[i];
			lsize_dst= avframe->linesize[i];
			width= proc_frame_ctx->width[i];
			height= proc_frame_ctx->height[i];
			CHECK_DO(width== width_Y>> (i!= 0), goto end);
			CHECK_DO(height== height_Y>> (i!= 0), goto end);
			CHECK_DO(lsize_src>= width && lsize_dst>= width, goto end);
			for(l= 0; l< height; l++)
				memcpy(&data_dst[l*lsize_dst], &data_src[l*lsize_src], width);
		}

		/* Copy rest of used fields */
		avframe->format= ffmpeg_fmt;
		avframe->pts= proc_frame_ctx->pts;
		break;
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:

		/* Check width and height */
		if(proc_frame_ctx->width[0]< 1 || proc_frame_ctx->height[0]< 1) {
			LOGE("Frame height and width should be set to '1' and 'data-size' "
					"respectively (this is a 1-dimensional array of data)\n");
			goto end;
		}

		/* Allocate FFmpeg's AV-frame structure */
		avframe= av_frame_alloc();
		CHECK_DO(avframe!= NULL, goto end);

		/* FFmpeg API is a bit confuse: It assumes for audio that left and
		 * right channels have the same number of audio samples characterized
		 * by 'AVFrame::nb_samples'. On the other hand, we can define
		 * different planes with different sizes.
		 * We will work with fixed stereo layout (considering only 2 planes).
		 */
		avframe->nb_samples= proc_frame_ctx->width[0]>> (
				(ffmpeg_fmt== AV_SAMPLE_FMT_S16) // width may include 2 planes
				+ 1); // divide by 2 (signed 16 bits planar samples)
		avframe->format= AV_SAMPLE_FMT_S16P; // the only supported CODEC format
		avframe->channel_layout= AV_CH_LAYOUT_STEREO;
		avframe->linesize[0]= avframe->linesize[1]=
				proc_frame_ctx->linesize[0]>> (ffmpeg_fmt== AV_SAMPLE_FMT_S16);
		avframe->pts= proc_frame_ctx->pts;

	    /* Allocate the data buffers.
	     * Note: function 'av_frame_get_buffer()' uses already initialized
	     * AVFrame structure fields to allocate necessary data buffers.
	     */
		CHECK_DO(av_frame_get_buffer(avframe, 32)>= 0, goto end);

		/* Copy data planes */
		if(ffmpeg_fmt== AV_SAMPLE_FMT_S16) {
			/* We have to convert */
			const int16_t *data_src= (int16_t*)(proc_frame_ctx->p_data[0]);
			int16_t *data_dst_lef= (int16_t*)(avframe->data[0]);
			int16_t *data_dst_rig= (int16_t*)(avframe->data[1]);

			for(i= 0; i< avframe->nb_samples<< 1; i+= 2) {
				int16_t sample_lef= *data_src++;
				int16_t sample_rig= *data_src++;
				*data_dst_lef++= sample_lef;
				*data_dst_rig++= sample_rig;
			}
		} else {
			for(i= 0; i< 2 /*stereo 2 channels*/; i++) {
				register int plane_size;
				const uint8_t *data_src= proc_frame_ctx->p_data[i];
				uint8_t *data_dst= avframe->data[i];
				if(data_src== NULL)
					continue; // No data for this plane
				CHECK_DO(data_dst!= NULL, goto end);
				if((plane_size= proc_frame_ctx->linesize[i])<= 0)
					continue;
				memcpy(data_dst, data_src, plane_size);
			}
		}
		break;
	default:
		LOGE("Unsupported frame samples format at encoder input\n");
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS && avframe!= NULL) {
		av_frame_free(&avframe);
		avframe= NULL; // redundant
	}
	return (void*)avframe;
}

AVFrame* allocate_frame_video(int pix_fmt, int width, int height)
{
	int ret_code, end_code= STAT_ERROR;
    AVFrame *avframe= NULL;
    LOG_CTX_INIT(NULL);

    /* Check arguments */
    if(width<= 0 || width> PROC_FRAME_MAX_WIDTH ||
       height<= 0 || height> PROC_FRAME_MAX_HEIGHT) {
    	LOGE("Could not allocate video raw picture buffer: video resolution "
    			"out of bounds\n");
    	return NULL;
    }

    avframe= av_frame_alloc();
    CHECK_DO(avframe!= NULL, goto end);

    /* Allocate the buffers for the frame data.
     * The following fields must be set on frame before calling this function:
     * - format (pixel format for video, sample format for audio);
     * - width and height for video;
     * - nb_samples and channel_layout for audio.
     */
    avframe->format= pix_fmt;
    avframe->width= width;
    avframe->height= height;
    ret_code= av_frame_get_buffer(avframe, 32);
    CHECK_DO(ret_code== 0, goto end);

    /* Make sure the frame data is writable */
    ret_code= av_frame_make_writable(avframe);
    if(ret_code< 0)
        exit(1);

    end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS && avframe!= NULL) {
		av_frame_free(&avframe);
		avframe= NULL; // redundant
	}
    return avframe;
}

void avframe_release(void **ref_avfame)
{
	if(ref_avfame== NULL)
		return;
	av_frame_free((AVFrame**)ref_avfame); //<- Internally set pointer to NULL
	*ref_avfame= NULL; // redundant
}

proc_frame_ctx_t* avpacket_2_proc_frame_ctx(const void *avpacket_arg)
{
	AVPacket *avpacket= (AVPacket*)avpacket_arg;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	uint8_t *data= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(avpacket!= NULL, return NULL);

	proc_frame_ctx= proc_frame_ctx_allocate();
	CHECK_DO(proc_frame_ctx!= NULL, return NULL);

	/* We only use one data plane for compressed data.
	 * Also note that width, height, and pixel-format fields are not used at
	 * encoder output.
	 */
	data= (uint8_t*)malloc(avpacket->size);
	CHECK_DO(data!= NULL, goto end);
	proc_frame_ctx->p_data[0]= proc_frame_ctx->data= data;
	memcpy((void*)data, avpacket->data, avpacket->size);
	data= NULL; // Avoid double referencing
	proc_frame_ctx->linesize[0]= avpacket->size;
	proc_frame_ctx->width[0]= avpacket->size;
	proc_frame_ctx->height[0]= 1;
	proc_frame_ctx->pts= avpacket->pts;
	proc_frame_ctx->dts= avpacket->dts;
	proc_frame_ctx->es_id= avpacket->stream_index;
	proc_frame_ctx->proc_sampling_rate= (int)avpacket->pos; // Hack

end:
	if(data!= NULL)
		free(data);
	av_packet_unref(avpacket);
	return proc_frame_ctx;
}

void* proc_frame_ctx_2_avpacket(const proc_frame_ctx_t *proc_frame_ctx)
{
	register int data_size, ret_code, end_code= STAT_ERROR;
	AVPacket *avpacket= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_frame_ctx!= NULL, return NULL);

	/* Check width and height */
	if(proc_frame_ctx->width[0]< 1 || proc_frame_ctx->height[0]< 1) {
		LOGE("Frame height and width should be set to '1' and 'data-size' "
				"respectively (this is a 1-dimensional array of data)\n");
		goto end;
	}

	/* Allocate FFmpeg's packet structure */
	avpacket= av_packet_alloc(); // Calls 'av_init_packet()' internally
	CHECK_DO(avpacket!= NULL, goto end);

	/* Input encoded data only uses one data plane; note that:
	 * - 'proc_frame_ctx->width[0]': represents the size of the packet in
	 * bytes;
	 * - 'proc_frame_ctx->p_data[0]': is the pointer to the packet data.
	 */
	data_size= proc_frame_ctx->width[0];
	ret_code= av_new_packet(avpacket, data_size);
	CHECK_DO(ret_code== 0 && avpacket->data!= NULL &&
			avpacket->size== data_size, goto end);
	memcpy(avpacket->data, proc_frame_ctx->p_data[0], data_size);

	/* Copy presentation and decoding time-stamps */
	avpacket->pts= proc_frame_ctx->pts;
	avpacket->dts= proc_frame_ctx->dts;
	avpacket->stream_index= proc_frame_ctx->es_id;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS && avpacket!= NULL)
		avpacket_release((void**)&avpacket);
	return (void*)avpacket;
}

void avpacket_release(void **ref_avpacket)
{
	if(ref_avpacket== NULL)
		return;
	av_packet_free((AVPacket**)
			ref_avpacket); //<- Internally set pointer to NULL
	*ref_avpacket= NULL; // redundant
}

proc_frame_ctx_t* avframe_2_proc_frame_ctx(const void *avframe_arg)
{
	proc_sample_fmt_t proc_sample_fmt;
	register int end_code= STAT_ERROR;
	AVFrame *avframe= (AVFrame*)avframe_arg;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	uint8_t *data= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(avframe!= NULL, goto end);

	/* Allocate processor frame context structure */
	proc_frame_ctx= proc_frame_ctx_allocate();
	CHECK_DO(proc_frame_ctx!= NULL, goto end);

	/* Get processor frame sampling format */
	proc_sample_fmt= ffmpegfmt_2_proc_sample_fmt(avframe->format);

	/* Allocate frame buffer and initialize frame related parameters according
	 * to the given sampling format.
	 */
	switch(proc_sample_fmt) {
		register int i, lsize_dst_Y, lsize_dst_C, size_dst_Y, size_dst_C,
			width_Y= 0, height_Y= 0, lsize_ch, lsize_ch_aligned;
	case PROC_IF_FMT_YUV420P:

		/* Get Y width and height */
		width_Y= avframe->width;
		height_Y= avframe->height;
		if(width_Y< 1 || width_Y> PROC_FRAME_MAX_WIDTH) {
			LOGE("Frame width= %d out of bounds (range is 1..%d)\n",
					width_Y, PROC_FRAME_MAX_WIDTH);
			goto end;
		}
		if(height_Y< 1 || height_Y> PROC_FRAME_MAX_HEIGHT) {
			LOGE("Frame height= %d out of bounds (range is 1..%d)\n",
					height_Y, PROC_FRAME_MAX_HEIGHT);
			goto end;
		}

		/* Allocate data buffer */
		lsize_dst_Y= EXTEND_SIZE_TO_MULTIPLE(width_Y, CTX_S_BASE_ALIGN);
		lsize_dst_C= EXTEND_SIZE_TO_MULTIPLE(width_Y>> 1, CTX_S_BASE_ALIGN);
		size_dst_Y= lsize_dst_Y* height_Y;
		size_dst_C= lsize_dst_C* (height_Y>> 1);
		data= (uint8_t*)aligned_alloc(CTX_S_BASE_ALIGN, size_dst_Y+
				(size_dst_C<< 1));
		CHECK_DO(data!= NULL, goto end);
		proc_frame_ctx->data= data;

		/* Initialize planes properties */
		proc_frame_ctx->p_data[0]= data;
		proc_frame_ctx->p_data[1]= data+ size_dst_Y;
		proc_frame_ctx->p_data[2]= data+ size_dst_Y+ size_dst_C;
		data= NULL; // Avoid double referencing
		proc_frame_ctx->linesize[0]= lsize_dst_Y;
		proc_frame_ctx->linesize[1]= lsize_dst_C;
		proc_frame_ctx->linesize[2]= lsize_dst_C;
		proc_frame_ctx->width[0]= width_Y;
		proc_frame_ctx->width[1]= width_Y>> 1;
		proc_frame_ctx->width[2]= width_Y>> 1;
		proc_frame_ctx->height[0]= height_Y;
		proc_frame_ctx->height[1]= height_Y>> 1;
		proc_frame_ctx->height[2]= height_Y>> 1;

		/* Copy data planes */
		for(i= 0; i< 3; i++) {
			register int l;
			register int h= proc_frame_ctx->height[i];
			register int w= proc_frame_ctx->width[i];
			for(l= 0; l< h; l++) {
				uint8_t *data_src= avframe->data[i];
				uint8_t *data_dst= (uint8_t*)proc_frame_ctx->p_data[i];
				register int lsize_src= avframe->linesize[i];
				register int lsize_dst= proc_frame_ctx->linesize[i];

				CHECK_DO(data_src!= NULL && data_dst!= NULL, goto end);
				CHECK_DO(lsize_src>= w && lsize_dst>= w, goto end);
				memcpy(&data_dst[l*lsize_dst], &data_src[l*lsize_src], w);
			}
		}
		break;
	case PROC_IF_FMT_S16:
	case PROC_IF_FMT_S16P:

		/* Allocate data buffer.
		 * We work with two channels (stereo) each with the same number of
		 * samples.
		 * IMPORTANT NOTE:
		 * The following text is taken from FFmpeg, regarding to
		 * 'AVFrame::linesize' documentation:
		 * "For audio, only linesize[0] may be set. For planar audio, each
		 * channel plane must be the same size".
		 * For this reason, we will only use linesize[0], as we notice that
		 * some decoders (e.g. MP3) do not use linesize[1] despite it uses
		 * two planes for left and right channels.
		 */
		if(avframe->channel_layout!= AV_CH_LAYOUT_STEREO) {
			LOGE("Audio channel layout not supported\n");
			goto end;
		}
		lsize_ch= avframe->linesize[0];
		CHECK_DO(lsize_ch> 0, goto end);
		lsize_ch_aligned= EXTEND_SIZE_TO_MULTIPLE(lsize_ch, CTX_S_BASE_ALIGN);
		data= (uint8_t*)aligned_alloc(CTX_S_BASE_ALIGN, lsize_ch_aligned<< 1);
		CHECK_DO(data!= NULL, goto end);
		proc_frame_ctx->data= data;
		data= NULL; // Avoid double referencing

		/* Copy data planes */
		if(proc_sample_fmt== PROC_IF_FMT_S16) {
			int16_t *data_src_lef;
			int16_t *data_src_rig;
			int16_t *data_dst;

			/* Initialize planes properties */
			proc_frame_ctx->p_data[0]= proc_frame_ctx->data;
			proc_frame_ctx->linesize[0]= lsize_ch_aligned<< 1;
			proc_frame_ctx->width[0]= lsize_ch<< 1;
			proc_frame_ctx->height[0]= 1;

			/* We have to convert */
			data_src_lef= (int16_t*)(avframe->data[0]);
			data_src_rig= (int16_t*)(avframe->data[1]);
			data_dst= (int16_t*)(proc_frame_ctx->p_data[0]);
			for(i= 0; i< avframe->nb_samples<< 1; i+= 2) {
				int16_t sample_lef= *data_src_lef++;
				int16_t sample_rig= *data_src_rig++;
				*data_dst++= sample_lef;
				*data_dst++= sample_rig;
			}
		} else {
			/* Initialize planes properties */
			proc_frame_ctx->p_data[0]= proc_frame_ctx->data;
			proc_frame_ctx->p_data[1]= proc_frame_ctx->data+ lsize_ch_aligned;
			proc_frame_ctx->linesize[0]= lsize_ch_aligned;
			proc_frame_ctx->width[0]= lsize_ch;
			proc_frame_ctx->height[0]= 1;
			proc_frame_ctx->linesize[1]= lsize_ch_aligned;
			proc_frame_ctx->width[1]= lsize_ch;
			proc_frame_ctx->height[1]= 1;

			/* Copy data planes */
			for(i= 0; i< 2; i++) {
				uint8_t *data_src= avframe->data[i];
				uint8_t *data_dst= (uint8_t*)proc_frame_ctx->p_data[i];
				CHECK_DO(data_src!= NULL && data_dst!= NULL, goto end);
				memcpy(data_dst, data_src, lsize_ch);
			}
		}

		break;
	default:
		LOGE("Unsupported frame samples format at decoder output\n");
		goto end;
	}

	/* Initialize rest of fields */
	proc_frame_ctx->proc_sample_fmt= proc_sample_fmt;
	proc_frame_ctx->proc_sampling_rate= avframe->sample_rate;
	proc_frame_ctx->pts= avframe->pts;
	proc_frame_ctx->dts= -1; // Undefined (not used)

	end_code= STAT_SUCCESS;
end:
	if(data!= NULL)
		free(data);
	if(end_code!= STAT_SUCCESS)
		proc_frame_ctx_release(&proc_frame_ctx);
	return proc_frame_ctx;
}

static int proc_sample_fmt_2_ffmpegfmt(proc_sample_fmt_t proc_sample_fmt)
{
	int ffmpegfmt= -1;

	switch(proc_sample_fmt) {
	case PROC_IF_FMT_YUV420P:
		ffmpegfmt= AV_PIX_FMT_YUV420P;
		break;
	//case PROC_IF_FMT_RGB24: // Reserved for future use
	//	ffmpegfmt= AV_PIX_FMT_RGB24;
	//	break;
	case PROC_IF_FMT_S16:
		ffmpegfmt= AV_SAMPLE_FMT_S16;
		break;
	case PROC_IF_FMT_S16P:
		ffmpegfmt= AV_SAMPLE_FMT_S16P;
		break;
	default:
		//ffmpegfmt= -1;
		break;
	}
	return ffmpegfmt;
}

static proc_sample_fmt_t ffmpegfmt_2_proc_sample_fmt(int ffmpegfmt)
{
	proc_sample_fmt_t proc_sample_fmt= PROC_IF_FMT_UNDEF;

	switch(ffmpegfmt) {
	case AV_PIX_FMT_YUV420P:
		proc_sample_fmt= PROC_IF_FMT_YUV420P;
		break;
	//case AV_PIX_FMT_RGB24: // Reserved for future use
	//	proc_sample_fmt= PROC_IF_FMT_RGB24;
	//	break;
	case AV_SAMPLE_FMT_S16:
		proc_sample_fmt= PROC_IF_FMT_S16;
		break;
	case AV_SAMPLE_FMT_S16P:
		proc_sample_fmt= PROC_IF_FMT_S16P;
		break;
	default:
		//proc_sample_fmt= PROC_IF_FMT_UNDEF;
		break;
	}
	return proc_sample_fmt;
}
