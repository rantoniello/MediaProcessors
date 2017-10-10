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
 * @file live555_rtsp.h
 * @brief Live555 based RTSP multiplexer and de-multiplexer wrappers.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_LIVE555_RTSP_H_
#define MEDIAPROCESSORS_SRC_LIVE555_RTSP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_if_s proc_if_t;

/* **** prototypes **** */

/**
 * Processor interface implementing the wrapper of the live555 RTSP
 * multiplexer.
 */
extern const proc_if_t proc_if_live555_rtsp_mux;

/**
 * Processor interface implementing the wrapper of the live555 RTSP
 * de-multiplexer.
 */
extern const proc_if_t proc_if_live555_rtsp_dmux;

#ifdef __cplusplus
} //extern "C"
#endif

#endif /* MEDIAPROCESSORS_SRC_LIVE555_RTSP_H_ */
