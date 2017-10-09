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
 * @file ffmpeg_lhe.h
 * @brief FFmpeg LHE/MLHE video encoder and decoder wrappers.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_FFMPEG_LHE_H_
#define MEDIAPROCESSORS_SRC_FFMPEG_LHE_H_

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_if_s proc_if_t;

/* **** prototypes **** */

/**
 * Processor interface implementing the FFmpeg wrapper of the LHE video
 * encoder.
 */
//extern const proc_if_t proc_if_ffmpeg_lhe_enc; //TODO?

/**
 * Processor interface implementing the FFmpeg wrapper of the LHE video
 * decoder.
 */
//extern const proc_if_t proc_if_ffmpeg_lhe_dec; //TODO?

/**
 * Processor interface implementing the FFmpeg wrapper of the MLHE video
 * encoder.
 */
extern const proc_if_t proc_if_ffmpeg_mlhe_enc;

/**
 * Processor interface implementing the FFmpeg wrapper of the MLHE video
 * decoder.
 */
extern const proc_if_t proc_if_ffmpeg_mlhe_dec;

#endif /* MEDIAPROCESSORS_SRC_FFMPEG_LHE_H_ */
