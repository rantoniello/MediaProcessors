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
 * @file proc_muxer.h
 * @brief Generic processor module context (see type proc_ctx_t)
 * extension for multiplexers and de-multiplexers.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_MUXERS_SRC_PROC_MUXER_H_
#define MEDIAPROCESSORS_MUXERS_SRC_PROC_MUXER_H_

#include <libmediaprocs/proc.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct procs_ctx_s procs_ctx_t;

/**
 * Multiplexer processing common context structure.
 */
typedef struct proc_muxer_mux_ctx_s {
	/**
	 * Generic processor context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * Set of elementary streams MUXERS to be registered or unregistered.
	 */
	procs_ctx_t *procs_ctx_es_muxers;
} proc_muxer_mux_ctx_t;

/* **** Prototypes **** */

/**
 * Initialize multiplexer common context structure.
 * @param proc_muxer_mux_ctx Pointer to the multiplexer common context
 * structure to be initialized.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int proc_muxer_mux_ctx_init(proc_muxer_mux_ctx_t *proc_muxer_mux_ctx,
		log_ctx_t *log_ctx);

/**
 * De-initialize multiplexer common context structure previously allocated by
 * a call to 'proc_muxer_mux_ctx_init()'.
 * This function release any heap-allocated field or structure member.
 * @param proc_muxer_mux_ctx Pointer to the multiplexer common context
 * structure to be de-initialized.
 * @param log_ctx Externally defined LOG module context structure.
 */
void proc_muxer_mux_ctx_deinit(proc_muxer_mux_ctx_t *proc_muxer_mux_ctx,
		log_ctx_t *log_ctx);

#endif /* MEDIAPROCESSORS_MUXERS_SRC_PROC_MUXER_H_ */
