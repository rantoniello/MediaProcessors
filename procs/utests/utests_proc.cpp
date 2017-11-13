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
 * @file utests_proc.cpp
 * @brief Generic processor (PROC) module unit testing.
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
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/proc.h>
}

/* **** Define a very simple bypass processor **** */

static proc_ctx_t* bypass_proc_open(const proc_if_t*, const char *settings_str,
		log_ctx_t *log_ctx, va_list arg)
{
	return (proc_ctx_t*)calloc(1, sizeof(proc_ctx_t));
}

static void bypass_proc_close(proc_ctx_t **ref_proc_ctx)
{
	proc_ctx_t *proc_ctx= NULL;

	if(ref_proc_ctx== NULL)
		return;

	if((proc_ctx= *ref_proc_ctx)!= NULL) {
		free(proc_ctx);
		*ref_proc_ctx= NULL;
	}
}

static int bypass_proc_rest_put(proc_ctx_t*, const char *str)
{
	return STAT_SUCCESS;
}

static int bypass_proc_rest_get(proc_ctx_t*, const proc_if_rest_fmt_t rest_fmt,
		void **ref_reponse)
{
	return STAT_SUCCESS;
}

static int bypass_proc_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *fifo_ctx_iput, fifo_ctx_t *fifo_ctx_oput)
{
	int ret_code;
	size_t fifo_elem_size= 0;
	proc_frame_ctx_t *proc_frame_ctx= NULL;

	/* Just "bypass" frame from input to output */
	ret_code= fifo_get(proc_ctx->fifo_ctx_array[PROC_IPUT],
			(void**)&proc_frame_ctx, &fifo_elem_size);
	CHECK(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN);
	if(ret_code!= STAT_SUCCESS)
		goto end;

	ret_code= fifo_put_dup(proc_ctx->fifo_ctx_array[PROC_OPUT],
			proc_frame_ctx, sizeof(void*));
	CHECK(ret_code== STAT_SUCCESS || ret_code== STAT_ENOMEM);

end:
	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return STAT_SUCCESS;
}

SUITE(UTESTS_PROC)
{
	TEST(ALLOCATE_RELEASE_PROC)
	{
#define FIFO_SIZE 2
		int frame_idx, i, x, y, ret_code;
		proc_ctx_t *proc_ctx= NULL;
		const proc_if_t proc_if_bypass_proc= {
			"bypass_processor", "encoder", "application/octet-stream",
			(uint64_t)(PROC_FEATURE_RD|PROC_FEATURE_WR|PROC_FEATURE_IOSTATS|
					PROC_FEATURE_IPUT_PTS|PROC_FEATURE_LATSTATS),
			bypass_proc_open,
			bypass_proc_close,
			bypass_proc_rest_put,
			bypass_proc_rest_get,
			bypass_proc_process_frame,
			NULL, NULL, NULL, NULL
		};
		uint32_t fifo_ctx_maxsize[PROC_IO_NUM]= {FIFO_SIZE, FIFO_SIZE};
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

		if(log_module_open()!= STAT_SUCCESS) {
			printf("Could not initialize LOG module\n");
			return;
		}

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
		proc_frame_ctx_yuv.pts= -1;
		proc_frame_ctx_yuv.dts= -1;

		/* Open processor */
		proc_ctx= proc_open(&proc_if_bypass_proc, ""/*settings*/, 0/*index*/,
				fifo_ctx_maxsize, NULL/*LOG*/, NULL);
		CHECK(proc_ctx!= NULL);
		if(proc_ctx== NULL)
			return;

		/* Fill processor input FIFO with two equal YUV frames */
		for(frame_idx= 0; frame_idx< FIFO_SIZE; frame_idx++) {
			ret_code= proc_send_frame(proc_ctx, &proc_frame_ctx_yuv);
			CHECK(ret_code== STAT_SUCCESS);
		}

		/* Read previously pushed frames from processor (in this simple test
		 * the processor is just a "bypass").
		 */
		for(frame_idx= 0; frame_idx< FIFO_SIZE; frame_idx++) {
			if(proc_frame_ctx!= NULL)
				proc_frame_ctx_release(&proc_frame_ctx);
			ret_code= proc_recv_frame(proc_ctx, &proc_frame_ctx);
			CHECK(ret_code== STAT_SUCCESS);
			CHECK(proc_frame_ctx!= NULL);
			if(proc_frame_ctx== NULL)
				goto end;

			CHECK(proc_frame_ctx->proc_sample_fmt== PROC_IF_FMT_UNDEF);
			CHECK(proc_frame_ctx->pts== -1);
			CHECK(proc_frame_ctx->dts== -1);
			for(i= 0; i< 3/*Num. of data planes*/; i++) {
				for(y= 0; y< (int)proc_frame_ctx->height[i]; y++) {
					for(x= 0; x< (int)proc_frame_ctx->width[i]; x++) {
						int data_coord= x+ y* proc_frame_ctx->linesize[i];
						uint8_t data_val= proc_frame_ctx->p_data[i][data_coord];
						int expected_val= x+ y* proc_frame_ctx_yuv.width[i];
						CHECK(data_val== expected_val);
						//printf("0x%02x ", data_val); // comment-me
					}
					//printf("\n"); // comment-me
				}
			}
		}

end:
		/* Close processor and release frame data if applicable */
		proc_close(&proc_ctx);
		proc_frame_ctx_release(&proc_frame_ctx);

		log_module_close();
#undef FIFO_SIZE
	}
}
