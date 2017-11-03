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
 * @file utests_proc_if.cpp
 * @brief PROC-IF unit testing.
 * @author Rafael Antoniello
 */

#include <UnitTest++/UnitTest++.h>

extern "C" {
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocs/proc_if.h>
}

/* **** Define dummy callback's for the interface **** */

static proc_ctx_t* dummy_proc_open(const proc_if_t*, const char *settings_str,
		log_ctx_t *log_ctx, va_list arg)
{
	return NULL;
}

static void dummy_proc_close(proc_ctx_t**)
{
}

static int dummy_proc_rest_put(proc_ctx_t*, const char *str)
{
	return STAT_SUCCESS;
}

static int dummy_proc_rest_get(proc_ctx_t*, char **rest_str)
{
	return STAT_SUCCESS;
}

static int dummy_proc_process_frame(proc_ctx_t*, fifo_ctx_t* i, fifo_ctx_t* o)
{
	return STAT_SUCCESS;
}

static int dummy_proc_opt(proc_ctx_t*, const char *tag, va_list arg)
{
	return STAT_SUCCESS;
}

static void* dummy_proc_iput_fifo_elem_dup(const proc_frame_ctx_t*)
{
	return NULL;
}

static void dummy_proc_iput_fifo_elem_release(void**)
{
}

static proc_frame_ctx_t* dummy_proc_oput_fifo_elem_dup(const void*)
{
	return NULL;
}

SUITE(UTESTS_PROC_IF)
{
	TEST(ALLOCATE_DUP_RELEASE_PROC_IF_T)
	{
		proc_if_t *proc_if= NULL;
		const proc_if_t proc_if_dummy_proc= {
			"dummy_processor", "encoder", "application/octet-stream",
			dummy_proc_open,
			dummy_proc_close,
			dummy_proc_rest_put,
			dummy_proc_rest_get,
			dummy_proc_process_frame,
			dummy_proc_opt,
			dummy_proc_iput_fifo_elem_dup,
			dummy_proc_iput_fifo_elem_release,
			dummy_proc_oput_fifo_elem_dup
		};

		/* Duplicate 'dummy' processor interface context structure
		 * (Internally allocates processor interface context structure using
		 * 'proc_if_allocate()').
		 */
		proc_if= proc_if_dup(&proc_if_dummy_proc);
		CHECK(proc_if!= NULL);
		if(proc_if== NULL)
			return;

		CHECK(strcmp(proc_if->proc_name, proc_if_dummy_proc.proc_name)== 0);
		CHECK(strcmp(proc_if->proc_type, proc_if_dummy_proc.proc_type)== 0);
		CHECK(strcmp(proc_if->proc_mime, proc_if_dummy_proc.proc_mime)== 0);
		CHECK(proc_if->open== dummy_proc_open);
		CHECK(proc_if->close== dummy_proc_close);
		CHECK(proc_if->rest_put== dummy_proc_rest_put);
		CHECK(proc_if->rest_get== dummy_proc_rest_get);
		CHECK(proc_if->process_frame== dummy_proc_process_frame);
		CHECK(proc_if->opt== dummy_proc_opt);
		CHECK(proc_if->iput_fifo_elem_opaque_dup==
				dummy_proc_iput_fifo_elem_dup);
		CHECK(proc_if->iput_fifo_elem_opaque_release==
				dummy_proc_iput_fifo_elem_release);
		CHECK(proc_if->oput_fifo_elem_opaque_dup==
				dummy_proc_oput_fifo_elem_dup);

		/* Release processor interface */
		proc_if_release(&proc_if);
	}

	TEST(ALLOCATE_DUP_RELEASE_PROC_FRAME_CTX)
	{
		int i, x, y;
		proc_frame_ctx_t *proc_frame_ctx= NULL;
		uint8_t yuv_frame[48]= { // YUV4:2:0 simple data example
			0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Y
			0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, // Y
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, // Y
			0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, // Y
			0x00, 0x01, 0x02, 0x03, // U
			0x04, 0x05, 0x06, 0x07, // U
			0x00, 0x01, 0x02, 0x03, // V
			0x04, 0x05, 0x06, 0x07  // V
		};
		proc_frame_ctx_t proc_frame_ctx_yuv= {0};

		/* Initialize YUV frame structure */
		proc_frame_ctx_yuv.data= yuv_frame;
		proc_frame_ctx_yuv.p_data[0]= &yuv_frame[0];  // Y
		proc_frame_ctx_yuv.p_data[1]= &yuv_frame[32]; // U
		proc_frame_ctx_yuv.p_data[2]= &yuv_frame[40]; // V
		proc_frame_ctx_yuv.linesize[0]= proc_frame_ctx_yuv.width[0]= 8;
		proc_frame_ctx_yuv.linesize[1]= proc_frame_ctx_yuv.width[1]= 4;
		proc_frame_ctx_yuv.linesize[2]= proc_frame_ctx_yuv.width[2]= 4;
		proc_frame_ctx_yuv.height[0]= 4;
		proc_frame_ctx_yuv.height[1]= proc_frame_ctx_yuv.height[2]= 2;
		proc_frame_ctx_yuv.proc_sample_fmt= PROC_IF_FMT_UNDEF;
		proc_frame_ctx_yuv.proc_sampling_rate= -1;
		proc_frame_ctx_yuv.pts= -1;
		proc_frame_ctx_yuv.dts= -1;
		proc_frame_ctx_yuv.es_id= -1;

		/* Duplicate 'YUV' frame context structure
		 * (Internally allocates frame context structure using
		 * 'proc_frame_ctx_allocate()').
		 */
		proc_frame_ctx= proc_frame_ctx_dup(&proc_frame_ctx_yuv);
		CHECK(proc_frame_ctx!= NULL);
		if(proc_frame_ctx== NULL)
			return;

		CHECK(proc_frame_ctx->proc_sample_fmt== PROC_IF_FMT_UNDEF);
		CHECK(proc_frame_ctx->proc_sampling_rate== -1);
		CHECK(proc_frame_ctx->pts== -1);
		CHECK(proc_frame_ctx->dts== -1);
		CHECK(proc_frame_ctx->es_id== -1);
		for(i= 0; i< 3/*Num. of data planes*/; i++) {
			for(y= 0; y< (int)proc_frame_ctx->height[i]; y++) {
				for(x= 0; x< (int)proc_frame_ctx->width[i]; x++) {
					int data_coord= x+ y* proc_frame_ctx->linesize[i];
					int expected_val= x+ y* proc_frame_ctx_yuv.width[i];
					CHECK(proc_frame_ctx->p_data[i][data_coord]== expected_val);
					//printf("0x%02x ", proc_frame_ctx->data
					//		[i][data_coord]); // comment-me
				}
				//printf("\n"); // comment-me
			}
		}

		/* Release frame structure */
		proc_frame_ctx_release(&proc_frame_ctx);
	}
}
