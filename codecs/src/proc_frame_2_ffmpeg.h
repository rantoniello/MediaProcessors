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
 * @file proc_frame_2_ffmpeg.h
 * @brief Facilities to convert processor's input/output frame to FFmpeg's
 * formats and vice versa.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_PROC_FRAME_2_FFMPEG_H_
#define MEDIAPROCESSORS_SRC_PROC_FRAME_2_FFMPEG_H_

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_frame_ctx_s proc_frame_ctx_t;
typedef struct AVFrame AVFrame;
typedef enum AVPixelFormat AVPixelFormat;
typedef enum proc_sample_fmt_enum proc_sample_fmt_t;

/* **** Prototypes **** */

/**
 * Transform a processor frame context structure (type proc_frame_ctx_t)
 * to an FFmpeg's AVFrame structure.
 * @param proc_frame_ctx Pointer to the processor frame context structure.
 * return Opaque pointer that actually can be casted to an AVFrame structure.
 */
void* proc_frame_ctx_2_avframe(const proc_frame_ctx_t *proc_frame_ctx);

/**
 * Allocate an FFmpeg's AVFrame structure for video encoding.
 * This is a wrapper function of the FFmpeg's 'av_frame_alloc()' for
 * allocating an AVFrame structure.
 * @param pix_fmt FFmpeg's video pixel-format identifier.
 * @param width Video frame width.
 * @param height Video frame height.
 * return Pointer to the allocated AVFrame structure.
 */
AVFrame* allocate_frame_video(int pix_fmt, int width, int height);

/**
 * Release FFmpeg's AVFrame structure.
 * This function is a wrapper of FFmpeg's 'av_frame_free()'.
 * @param ref_avfame Opaque reference to a pointer to an AVFrame structure
 * to be released.
 */
void avframe_release(void **ref_avfame);

/**
 * Transform FFmpeg's AVPacket structure to a processor frame context
 * structure (type proc_frame_ctx_t).
 * @param avpacket_arg Opaque pointer to the AVPacket structure.
 * return Pointer to the processor frame context structure.
 */
proc_frame_ctx_t* avpacket_2_proc_frame_ctx(const void *avpacket_arg);

/**
 * Transform a processor frame context structure (type proc_frame_ctx_t)
 * to an FFmpeg's AVPacket structure.
 * @param proc_frame_ctx Pointer to the processor frame context structure.
 * return Opaque pointer that actually can be casted to an AVPacket structure.
 */
void* proc_frame_ctx_2_avpacket(const proc_frame_ctx_t *proc_frame_ctx);

/**
 * Release FFmpeg's AVPacket structure.
 * This function is a wrapper of FFmpeg's 'av_packet_free()'.
 * @param ref_avpacket Opaque reference to a pointer to an AVPacket structure
 * to be released.
 */
void avpacket_release(void **ref_avpacket);

/**
 * Transform FFmpeg's AVFrame structure to a processor frame context
 * structure (type proc_frame_ctx_t).
 * @param avframe Opaque pointer to the AVFrame structure.
 * return Pointer to the processor frame context structure.
 */
proc_frame_ctx_t* avframe_2_proc_frame_ctx(const void *avframe);

#endif /* MEDIAPROCESSORS_SRC_PROC_FRAME_2_FFMPEG_H_ */
