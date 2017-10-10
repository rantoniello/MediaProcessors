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
 * @file proc_muxer.c
 * @author Rafael Antoniello
 */

#include "proc_muxer.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <libcjson/cJSON.h>

#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocs/procs.h>

/* **** Definitions **** */

/* **** Implementations **** */

int proc_muxer_mux_ctx_init(proc_muxer_mux_ctx_t *proc_muxer_mux_ctx,
		log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_muxer_mux_ctx!= NULL, goto end);

	proc_muxer_mux_ctx->procs_ctx_es_muxers= procs_open(LOG_CTX_GET());
	CHECK_DO(proc_muxer_mux_ctx->procs_ctx_es_muxers!= NULL, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		proc_muxer_mux_ctx_deinit(proc_muxer_mux_ctx, LOG_CTX_GET());
	return STAT_SUCCESS;
}

void proc_muxer_mux_ctx_deinit(proc_muxer_mux_ctx_t *proc_muxer_mux_ctx,
		log_ctx_t *log_ctx)
{
	if(proc_muxer_mux_ctx== NULL)
		return;

	if(proc_muxer_mux_ctx->procs_ctx_es_muxers!= NULL)
		procs_close(&proc_muxer_mux_ctx->procs_ctx_es_muxers);
}
