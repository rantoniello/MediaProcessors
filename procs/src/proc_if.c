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
 * @file proc_if.c
 * @author Rafael Antoniello
 */

#include "proc_if.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/mem_utils.h>

/* **** Implementations **** */

const proc_sample_fmt_lut_t proc_sample_fmt_lut[]=
{
	{PROC_IF_FMT_UNDEF, "Undefined format"},
	{PROC_IF_FMT_YUV420P, "Planar YUV 4:2:0 with 12bpp"},
	//{PROC_IF_FMT_RGB24, "Packed RGB 8:8:8 with 24bpp"}, // Reserved for future use
	{PROC_IF_FMT_S16, "Interleaved signed 16 bits"},
	{PROC_IF_FMT_S16P, "Planar signed 16 bits"}
};

proc_frame_ctx_t* proc_frame_ctx_allocate()
{
	return (proc_frame_ctx_t*)calloc(1, sizeof(proc_frame_ctx_t));
}

proc_frame_ctx_t* proc_frame_ctx_dup(
		const proc_frame_ctx_t *proc_frame_ctx_arg)
{
	register int i, data_size, end_code= STAT_ERROR;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_frame_ctx_arg!= NULL, return NULL);

	proc_frame_ctx= proc_frame_ctx_allocate();
	CHECK_DO(proc_frame_ctx!= NULL, goto end);

	/* Compute data buffer size (note that data buffer can not be reallocated
	 * because of alignment, thus full-size is needed to allocate buffer).
	 * Copy line-size, width and height parameters for each data plane.
	 */
	for(i= 0, data_size= 0; i< PROC_FRAME_NUM_DATA_POINTERS; i++) {
		register int lsize_src, lsize_dst, width, height;
		const uint8_t *p_data_src= proc_frame_ctx_arg->p_data[i];
		if(p_data_src== NULL)
			continue; // We assume no data available for this plane

		lsize_src= proc_frame_ctx_arg->linesize[i];
		lsize_dst= EXTEND_SIZE_TO_MULTIPLE(lsize_src, CTX_S_BASE_ALIGN);
		width= proc_frame_ctx_arg->width[i];
		height= proc_frame_ctx_arg->height[i];
		CHECK_DO(width> 0 && (width<= PROC_FRAME_MAX_WIDTH || height== 1),
				goto end);
		CHECK_DO(height> 0 && height<= PROC_FRAME_MAX_HEIGHT, goto end);
		CHECK_DO(lsize_src>= width && lsize_dst>= lsize_src, goto end);
		data_size+= lsize_dst* height;
		proc_frame_ctx->linesize[i]= lsize_dst;
		proc_frame_ctx->width[i]= width;
		proc_frame_ctx->height[i]= height;
	}

	/* Allocate data buffer, copy data and set pointers to planes */
	proc_frame_ctx->data= (uint8_t*)aligned_alloc(CTX_S_BASE_ALIGN, data_size);
	CHECK_DO(proc_frame_ctx->data!= NULL, goto end);
	for(i= 0, data_size= 0; i< PROC_FRAME_NUM_DATA_POINTERS; i++) {
		register int l, lsize_src, lsize, width, height;
		const uint8_t *p_data_dst;
		const uint8_t *p_data_src= proc_frame_ctx_arg->p_data[i];
		if(p_data_src== NULL)
			continue;

		lsize_src= proc_frame_ctx_arg->linesize[i];
		lsize= proc_frame_ctx->linesize[i];
		width= proc_frame_ctx->width[i];
		height= proc_frame_ctx->height[i];
		proc_frame_ctx->p_data[i]= p_data_dst= proc_frame_ctx->data+ data_size;
		data_size+= lsize* height;

		for(l= 0; l< height; l++)
			memcpy((void*)&p_data_dst[l* lsize], &p_data_src[l* lsize_src],
					width);
	}

	/* Copy rest of parameters */
	proc_frame_ctx->proc_sample_fmt= proc_frame_ctx_arg->proc_sample_fmt;
	proc_frame_ctx->proc_sampling_rate= proc_frame_ctx_arg->proc_sampling_rate;
	proc_frame_ctx->pts= proc_frame_ctx_arg->pts;
	proc_frame_ctx->dts= proc_frame_ctx_arg->dts;
	proc_frame_ctx->es_id= proc_frame_ctx_arg->es_id;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		proc_frame_ctx_release(&proc_frame_ctx);
	return proc_frame_ctx;
}

void proc_frame_ctx_release(proc_frame_ctx_t **ref_proc_frame_ctx)
{
	proc_frame_ctx_t *proc_frame_ctx= NULL;

	if(ref_proc_frame_ctx== NULL ||
			(proc_frame_ctx= *ref_proc_frame_ctx)== NULL)
		return;

	if(proc_frame_ctx->data!= NULL) {
		free(proc_frame_ctx->data);
		proc_frame_ctx->data= NULL;
	}

	free(proc_frame_ctx);
	*ref_proc_frame_ctx= NULL;
}

proc_if_t* proc_if_allocate()
{
	return (proc_if_t*)calloc(1, sizeof(proc_if_t));
}

proc_if_t* proc_if_dup(const proc_if_t *proc_if_arg)
{
	int end_code= STAT_ERROR;
	proc_if_t *proc_if= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_if_arg!= NULL, return NULL);

	/* Allocate context structure */
	proc_if= proc_if_allocate();
	CHECK_DO(proc_if!= NULL, goto end);

	/* Copy simple-type members values.
	 * Note that pointers to callback are all supposed to be static values,
	 * for this reason we just copy (not duplicate) the pointer values.
	 */
	memcpy(proc_if, proc_if_arg, sizeof(proc_if_t));

	/* **** Duplicate members that use dynamic memory allocations **** */

	CHECK_DO(proc_if_arg->proc_name!= NULL, goto end);
	proc_if->proc_name= strdup(proc_if_arg->proc_name);
	CHECK_DO(proc_if->proc_name!= NULL, goto end);

	CHECK_DO(proc_if_arg->proc_type!= NULL, goto end);
	proc_if->proc_type= strdup(proc_if_arg->proc_type);
	CHECK_DO(proc_if->proc_type!= NULL, goto end);

	CHECK_DO(proc_if_arg->proc_mime!= NULL, goto end);
	proc_if->proc_mime= strdup(proc_if_arg->proc_mime);
	CHECK_DO(proc_if->proc_mime!= NULL, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		proc_if_release(&proc_if);
	return proc_if;
}

int proc_if_cmp(const proc_if_t* proc_if1, const proc_if_t* proc_if2)
{
	int ret_value= 1; // means "non-equal"
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_if1!= NULL, return 1);
	CHECK_DO(proc_if2!= NULL, return 1);

	/* Compare contexts fields */
	if(strcmp(proc_if1->proc_name, proc_if2->proc_name)!= 0)
		goto end;
	if(strcmp(proc_if1->proc_type, proc_if2->proc_type)!= 0)
		goto end;
	if(strcmp(proc_if1->proc_mime, proc_if2->proc_mime)!= 0)
		goto end;
	if(proc_if1->open!= proc_if2->open)
		goto end;
	if(proc_if1->close!= proc_if2->close)
		goto end;
	if(proc_if1->rest_put!= proc_if2->rest_put)
		goto end;
	if(proc_if1->rest_get!= proc_if2->rest_get)
		goto end;
	if(proc_if1->process_frame!= proc_if2->process_frame)
		goto end;
	if(proc_if1->opt!= proc_if2->opt)
		goto end;
	if(proc_if1->iput_fifo_elem_opaque_dup!=
			proc_if2->iput_fifo_elem_opaque_dup)
		goto end;
	if(proc_if1->iput_fifo_elem_opaque_release!=
			proc_if2->iput_fifo_elem_opaque_release)
		goto end;
	if(proc_if1->oput_fifo_elem_opaque_dup!=
			proc_if2->oput_fifo_elem_opaque_dup)
		goto end;

	// Reserved for future use: compare new fields here...

	ret_value= 0; // contexts are equal
end:
	return ret_value;
}

void proc_if_release(proc_if_t **ref_proc_if)
{
	proc_if_t *proc_if= NULL;

	if(ref_proc_if== NULL)
		return;

	if((proc_if= *ref_proc_if)!= NULL) {

		if(proc_if->proc_name!= NULL) {
			free((void*)proc_if->proc_name);
			proc_if->proc_name= NULL;
		}

		if(proc_if->proc_type!= NULL) {
			free((void*)proc_if->proc_type);
			proc_if->proc_type= NULL;
		}

		if(proc_if->proc_mime!= NULL) {
			free((void*)proc_if->proc_mime);
			proc_if->proc_mime= NULL;
		}

		free(proc_if);
		*ref_proc_if= NULL;
	}
}
