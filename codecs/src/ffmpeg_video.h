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
 * @file ffmpeg_video.h
 * @brief Generic processor module context (see type proc_ctx_t)
 * extension for video encoders and decoders.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_FFMPEG_VIDEO_H_
#define MEDIAPROCESSORS_SRC_FFMPEG_VIDEO_H_

#include <libmediaprocsutils/mem_utils.h>
#include <libmediaprocs/proc.h>

/* **** Definitions **** */

#define SCALE_FLAGS SWS_BICUBIC

/* Forward declarations */
typedef struct AVCodec AVCodec;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVFrame AVFrame;
typedef struct AVPacket AVPacket;
typedef struct AVDictionary AVDictionary;
typedef struct SwsContext SwsContext;
typedef struct video_settings_enc_ctx_s video_settings_enc_ctx_t;
typedef struct video_settings_dec_ctx_s video_settings_dec_ctx_t;

/**
 * FFmpeg video encoding common context structure.
 */
typedef struct ffmpeg_video_enc_ctx_s {
	/**
	 * Generic processor context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * FFmpeg's static CODEC context structure characterizing video encoder.
	 */
	const AVCodec *avcodec;
	/**
	 * FFmpeg's CODEC instance context structure.
	 */
	AVCodecContext *avcodecctx;
	/**
	 * FFmpeg's dictionary structure used for storing key:value pairs for
	 * specific encoder configuration options.
	 */
	AVDictionary *avdictionary;
	/**
	 * FFmpeg's structure for re-scaling/re-formatting raw frames.
	 * If input raw data format is different from the encoder input format,
	 * we will need also a temporary buffer to convert to the required format.
	 */
	AVFrame *avframe_tmp;
	struct SwsContext *sws_ctx;
	/**
	 * Video encoder input frame-rate.
	 */
	int frame_rate_input;
	/**
	 * Video encoder input frame width.
	 */
	int width_input;
	/**
	 * Video encoder input frame height.
	 */
	int height_input;
	/**
	 * Video encoder input pixel format (FFEMPG's identifier).
	 */
	int ffmpeg_pix_fmt_input;
} ffmpeg_video_enc_ctx_t;

/**
 * FFmpeg video decoding common context structure.
 */
typedef struct ffmpeg_video_dec_ctx_s {
	/**
	 * Generic processor context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * FFmpeg's static CODEC context structure characterizing video decoder.
	 */
	const AVCodec *avcodec;
	/**
	 * FFmpeg's decoder instance context structure.
	 */
	AVCodecContext *avcodecctx;
} ffmpeg_video_dec_ctx_t;

/* **** Prototypes **** */

/**
 * Initialize FFmpeg's video encoding common context structure.
 * @param ffmpeg_video_enc_ctx Pointer to the video encoding common context
 * structure to be initialized.
 * @param avcodecid Unambiguous encoder identifier (namely, the encoder
 * type Id.).
 * @param video_settings_enc_ctx Pointer to the initial generic video encoder
 * settings context structure.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int ffmpeg_video_enc_ctx_init(ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx,
		int avcodecid, const video_settings_enc_ctx_t *video_settings_enc_ctx,
		log_ctx_t *log_ctx);

/**
 * De-initialize FFmpeg's video encoding common context structure previously
 * allocated by a call to 'ffmpeg_video_enc_ctx_init()'.
 * This function release any heap-allocated field or structure member.
 * @param ffmpeg_video_enc_ctx Pointer to the video encoding common context
 * structure to be de-initialized.
 * @param log_ctx Externally defined LOG module context structure.
 */
void ffmpeg_video_enc_ctx_deinit(ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx,
		log_ctx_t *log_ctx);

/**
 * Encode a complete video frame. If an output frame is produced, is written
 * to the output FIFO buffer.
 * @param ffmpeg_video_enc_ctx Pointer to the video encoding common context
 * structure.
 * @param avframe_iput Pointer to the (FFmpeg's) input frame structure.
 * @param oput_fifo_ctx Pointer to the output FIFO buffer context structure.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other
 * code values please refer to .stat_codes.h).
 */
int ffmpeg_video_enc_frame(ffmpeg_video_enc_ctx_t *ffmpeg_video_enc_ctx,
		AVFrame *avframe_iput, fifo_ctx_t* oput_fifo_ctx, log_ctx_t *log_ctx);

/**
 * Initialize FFmpeg's video decoding common context structure.
 * @param ffmpeg_video_dec_ctx Pointer to the video decoding common context
 * structure to be initialized.
 * @param avcodecid Unambiguous decoder identifier (namely, the decoder
 * type Id.).
 * @param video_settings_dec_ctx Pointer to the initial generic video decoder
 * settings context structure.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int ffmpeg_video_dec_ctx_init(ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx,
		int avcodecid, const video_settings_dec_ctx_t *video_settings_dec_ctx,
		log_ctx_t *log_ctx);

/**
 * De-initialize FFmpeg's video decoding common context structure previously
 * allocated by a call to 'ffmpeg_video_dec_ctx_init()'.
 * This function release any heap-allocated field or structure member.
 * @param ffmpeg_video_dec_ctx Pointer to the video decoding common context
 * structure to be de-initialized.
 * @param log_ctx Externally defined LOG module context structure.
 */
void ffmpeg_video_dec_ctx_deinit(ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx,
		log_ctx_t *log_ctx);

/**
 * Decode a complete video frame. If an output frame is produced, is written
 * to the output FIFO buffer.
 * @param ffmpeg_video_dec_ctx Pointer to the video decoding common context
 * structure.
 * @param avpacket_iput Pointer to the (FFmpeg's) input packet structure.
 * @param oput_fifo_ctx Pointer to the output FIFO buffer context structure.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other
 * code values please refer to .stat_codes.h).
 */
int ffmpeg_video_dec_frame(ffmpeg_video_dec_ctx_t *ffmpeg_video_dec_ctx,
		AVPacket *avpacket_iput, fifo_ctx_t* oput_fifo_ctx, log_ctx_t *log_ctx);

/**
 * FFmpeg video CODECS are not generally designed to accept changing settings
 * on run-time. Thus, we have to reset (that is, de-initialize and
 * re-initialize) the CODEC to set new settings while running the processor.
 * @param proc_ctx Pointer to the processor (PROC) context structure
 * @param video_settings_opaque Opaque pointer to be casted either to an
 * encoder or decoder video settings context structure.
 * @param flag_is_encoder Set to non-zero to signal that we are resetting an
 * encoder, set to zero to identify a decoder.
 * @param log_ctx Pointer to the LOG module context structure.
 */
void ffmpeg_video_reset_on_new_settings(proc_ctx_t *proc_ctx,
		volatile void *video_settings_opaque, int flag_is_encoder,
		log_ctx_t *log_ctx);

#endif /* MEDIAPROCESSORS_SRC_FFMPEG_VIDEO_H_ */
