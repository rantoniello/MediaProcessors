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
 * @file transcoder.h
 * @brief Generic transcoding module.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_TRANSCODERS_SRC_TRANSCODER_H_
#define MEDIAPROCESSORS_TRANSCODERS_SRC_TRANSCODER_H_

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_if_s proc_if_t;

/* **** Prototypes **** */

/**
 * Processor interface implementing the transcoder.
 */
extern const proc_if_t proc_if_transcoder;

#endif /* MEDIAPROCESSORS_TRANSCODER_SRC_TRANSCODER_H_ */
