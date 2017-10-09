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
 * @file ffmpeg_m2v.h
 * @brief FFmpeg mpeg-2 video encoder and decoder wrappers.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_FFMPEG_M2V_H_
#define MEDIAPROCESSORS_SRC_FFMPEG_M2V_H_

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_if_s proc_if_t;

/* **** prototypes **** */

/**
 * Processor interface implementing the FFmpeg wrapper of the MPEG-2 video
 * encoder.
 */
extern const proc_if_t proc_if_ffmpeg_m2v_enc;

/**
 * Processor interface implementing the FFmpeg wrapper of the MPEG-2 video
 * decoder.
 */
extern const proc_if_t proc_if_ffmpeg_m2v_dec;

#endif /* MEDIAPROCESSORS_SRC_FFMPEG_M2V_H_ */
