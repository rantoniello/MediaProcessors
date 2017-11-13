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
 * @file utests_encdec1.cpp
 * @brief Encoding-decoding loop-back unit testing (test suite #1).
 * Simple video/audio encoding-decoding with PROCS API example.
 * @author Rafael Antoniello
 */

#include <UnitTest++/UnitTest++.h>
#include <string>

extern "C" {
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <mongoose.h>
#include <libcjson/cJSON.h>

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/procs.h>
#include <libmediaprocs/procs_api_http.h>
#include <libmediaprocs/proc.h>
#include "../src/ffmpeg_x264.h"
#include "../src/ffmpeg_m2v.h"
#include "../src/ffmpeg_mp3.h"
#include "../src/ffmpeg_lhe.h"
}

#define SQR(x) ((x)*(x))

#define LISTENING_PORT 			"8088"
#define LISTENING_HOST 			"127.0.0.1"
#define TEST_DURATION_SEC 		8
#define MIN_PSNR_VAL 			40
#define OUTPUT_FILE_VIDEO 		"/tmp/out.yuv"
#define OUTPUT_FILE_AUDIO		"/tmp/out.wav"
#define MEDIA_TYPE_VIDEO 		0
#define MEDIA_TYPE_AUDIO 		1

#define VIDEO_WIDTH 			"352"
#define VIDEO_HEIGHT 			"288"

#define SETTINGS_SAMPLE_RATE 	"44100" // [samples/second]
#define SETTINGS_AUDIO_BITRATE 	"128000" // [bits/second]

#define RESPONSE_BUF_SIZE	512*1024

/* Debugging purposes: un-comment the next definition to save output raw
 * YUV 4:2:0 video to a file (.yuv extension) and s16 interlaced audio
 * (.wav extension).
 * You can watch the output video file, for example, by using ffplay command:
 * 'LD_LIBRARY_PATH=<...>/lib <...>/bin/ffplay -video_size 352x288 -framerate 10 /my/output/file.yuv'
 * For the audio:
 * 'LD_LIBRARY_PATH=<...>/lib <...>/bin/ffplay -f s16le -channels 2 /my/output/file.wav'
 */
//#define WRITE_2_OFILE

/* Implementation note: we do not change output with/height settings to be
 * able to validate encoding-decoding loop using PSNR measurements (frames
 * have to be of the same size to be easily compared).
 */
static const char *test_settings_patterns[][2/*V/A*/][2]=
{
		{
				{
					// [0]-> query string to PUT
					"bit_rate_output=500000"
					"&frame_rate_output=25"
					"&width_output="VIDEO_WIDTH"&height_output="VIDEO_HEIGHT
					"&gop_size=20"
					"&conf_preset=medium"
					,
					// [1]-> JSON extract string to compare when checking GET
					"\"bit_rate_output\":500000,"
					"\"frame_rate_output\":25,"
					"\"width_output\":"VIDEO_WIDTH","
					"\"height_output\":"VIDEO_HEIGHT","
					"\"gop_size\":20,"
					"\"conf_preset\":\"medium\""
				},
				{
					"bit_rate_output=64000"
					"&sample_rate_output=44100"
					,
					"\"bit_rate_output\":64000,"
					"\"sample_rate_output\":44100"
				}
		},
		{
				{
					"bit_rate_output=300000"
					"&frame_rate_output=25"
					"&width_output="VIDEO_WIDTH"&height_output="VIDEO_HEIGHT
					"&gop_size=10"
					"&conf_preset=fast"
					,
					"\"bit_rate_output\":300000,"
					"\"frame_rate_output\":25,"
					"\"width_output\":"VIDEO_WIDTH","
					"\"height_output\":"VIDEO_HEIGHT","
					"\"gop_size\":10,"
					"\"conf_preset\":\"fast\""
				},
				{
					"bit_rate_output=80000"
					"&sample_rate_output=44100"
					,
					"\"bit_rate_output\":80000,"
					"\"sample_rate_output\":44100"
				}
		},
		{
				{
					"bit_rate_output=700000"
					"&frame_rate_output=25"
					"&width_output="VIDEO_WIDTH"&height_output="VIDEO_HEIGHT
					"&gop_size=36"
					"&conf_preset=medium"
					,
					"\"bit_rate_output\":700000,"
					"\"frame_rate_output\":25,"
					"\"width_output\":"VIDEO_WIDTH","
					"\"height_output\":"VIDEO_HEIGHT","
					"\"gop_size\":36,"
					"\"conf_preset\":\"medium\""
				},
				{
					"bit_rate_output=64000"
					"&sample_rate_output=48000"
					,
					"\"bit_rate_output\":64000,"
					"\"sample_rate_output\":48000"
				}
		},
		{
				{
					NULL, NULL
				},
				{
					NULL, NULL
				}
		},
};

typedef struct thr_ctx_s {
	volatile int flag_exit;
	volatile int flag_http_server_running;
	int enc_proc_id, dec_proc_id;
	int media_type;
	int min_psnr_val;
	procs_ctx_t *procs_ctx;
} thr_ctx_t;

typedef struct ev_user_data_s {
	volatile int flag_exit;
	const char *ref_response_str;
	procs_ctx_t *procs_ctx;
} ev_user_data_t;

static void http_client_event_handler(struct mg_connection *nc, int ev,
		void *ev_data) {
	struct http_message *hm= (struct http_message*)ev_data;
	struct ev_user_data_s *data= (ev_user_data_t*)nc->user_data;
	int connect_status;

	switch (ev) {
	case MG_EV_CONNECT:
		connect_status= *(int*)ev_data;
		if (connect_status!= 0) {
			printf("Error in HTTP connection: %s\n", strerror(connect_status));
			data->flag_exit= 1;
		}
		break;
	case MG_EV_HTTP_REPLY:
		if(hm->body.len> 0 && hm->body.p!= NULL) {
			//printf("Got reply-:\n%.*s (%d)\n", (int)hm->body.len,
			//		hm->body.p, (int)hm->body.len); //comment-me
			if(hm->body.len< RESPONSE_BUF_SIZE) {
				memcpy((void*)data->ref_response_str, hm->body.p, hm->body.len);
			} else {
				fprintf(stderr, "Message too big!'%s()'\n", __FUNCTION__);
				exit(1);
			}
		}
		nc->flags|= MG_F_SEND_AND_CLOSE;
		data->flag_exit= 1;
		break;
	case MG_EV_CLOSE:
		data->flag_exit= 1;
		break;
	default:
		break;
	}
}

static char* http_client_request(const char *method, const char *url,
		const char *qstring, const char *content)
{
	size_t content_size;
	struct mg_mgr mgr;
	struct mg_connection *nc;
	struct mg_connect_opts opts;
	const char *error_str= NULL;
	char response_buf[RESPONSE_BUF_SIZE]= {0};
	struct ev_user_data_s ev_user_data= {0, response_buf, NULL};

	/* Check arguments.
	 * Note that 'content' may be NULL.
	 */
	if(method== NULL || url== NULL) {
		fprintf(stderr, "Bad argument '%s()'\n", __FUNCTION__);
		exit(1);
	}

	memset(&opts, 0, sizeof(opts));
	opts.error_string= &error_str;
	opts.user_data= &ev_user_data;

	mg_mgr_init(&mgr, NULL);
	nc= mg_connect_opt(&mgr, LISTENING_HOST":"LISTENING_PORT,
			http_client_event_handler, opts);
	if(nc== NULL) {
		fprintf(stderr, "mg_connect(%s:%s) failed: %s\n", LISTENING_HOST,
				LISTENING_PORT, error_str);
		exit(EXIT_FAILURE);
	}
	mg_set_protocol_http_websocket(nc);

	mg_printf(nc, "%s %s%s%s HTTP/1.0\r\n", method, url, qstring? "?": "",
			qstring? qstring: "");
	if(content!= NULL && (content_size= strlen(content))> 0) {
		mg_printf(nc, "Content-Length: %d\r\n", (int)content_size);
		mg_printf(nc, "\r\n");
		mg_printf(nc, "%s", content);
	} else {
		mg_printf(nc, "\r\n");
	}

	while(ev_user_data.flag_exit== 0)
		mg_mgr_poll(&mgr, 1000);

	mg_mgr_free(&mgr);
	//if(ev_user_data.ref_response_str!= NULL)
	//	printf("Got reply: '%s'\n",
	//			ev_user_data.ref_response_str); //comment-me
	return strlen(response_buf)> 0? strdup(response_buf): NULL;
}

static void http_event_handler(struct mg_connection *c, int ev, void *p)
{
	if(ev== MG_EV_RECV) {
		mg_printf(c, "%s", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

	} else if(ev == MG_EV_HTTP_REQUEST) {
		register size_t uri_len= 0, method_len= 0, qs_len= 0, body_len= 0;
		const char *uri_p, *method_p, *qs_p, *body_p;
		struct http_message *hm= (struct http_message*)p;
		char *url_str= NULL, *method_str= NULL, *str_response= NULL,
				*qstring_str= NULL, *body_str= NULL;
		thr_ctx_t *thr_ctx= (thr_ctx_t*)c->user_data;

		if((uri_p= hm->uri.p)!= NULL && (uri_len= hm->uri.len)> 0) {
			url_str= (char*)calloc(1, uri_len+ 1);
			memcpy(url_str, uri_p, uri_len);
		}
		if((method_p= hm->method.p)!= NULL && (method_len= hm->method.len)> 0) {
			method_str= (char*)calloc(1, method_len+ 1);
			memcpy(method_str, method_p, method_len);
		}
		if((qs_p= hm->query_string.p)!= NULL &&
				(qs_len= hm->query_string.len)> 0) {
			qstring_str= (char*)calloc(1, qs_len+ 1);
			memcpy(qstring_str, qs_p, qs_len);
		}
		if((body_p= hm->body.p)!= NULL && (body_len= hm->body.len)> 0) {
			body_str= (char*)calloc(1, body_len+ 1);
			memcpy(body_str, body_p, body_len);
		}

		/* Process HTTP request */
		procs_api_http_req_handler(thr_ctx->procs_ctx, url_str, qstring_str,
				method_str, body_str, body_len, &str_response);

		/* Send response */
		mg_printf(c, "%s", "HTTP/1.1 200 OK\r\n");
		if(str_response!= NULL && strlen(str_response)> 0) {
			//printf("str_response: %s (len: %d)\n", str_response,
			//		(int)strlen(str_response)); //comment-me
			mg_printf(c, "Content-Length: %d\r\n", (int)strlen(str_response));
			mg_printf(c, "\r\n");
			mg_printf(c, "%s", str_response);
		} else {
			mg_printf(c, "Content-Length: %d\r\n", 0);
		}

		if(str_response!= NULL)
			free(str_response);
		if(url_str!= NULL)
			free(url_str);
		if(method_str!= NULL)
			free(method_str);
		if(qstring_str!= NULL)
			free(qstring_str);
		if(body_str!= NULL)
			free(body_str);
	}
}

/**
 * Runs HTTP server thread, listening to the given port.
 */
static void* http_server_thr(void *t)
{
	struct mg_mgr mgr;
	struct mg_connection *c;
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	const char *listening_port= LISTENING_PORT;
	struct mg_bind_opts opts;
	const char *error_str= NULL;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument 'enc_dec_thr()'\n");
		exit(1);
	}

	/* Create and configure the server */
	mg_mgr_init(&mgr, NULL);

	memset(&opts, 0, sizeof(opts));
	opts.error_string= &error_str;
	opts.user_data= thr_ctx;
	c= mg_bind_opt(&mgr, listening_port, http_event_handler, opts);
	if(c== NULL) {
		fprintf(stderr, "mg_bind_opt(%s:%s) failed: %s\n", LISTENING_HOST,
				LISTENING_PORT, error_str);
		exit(EXIT_FAILURE);
	}
	mg_set_protocol_http_websocket(c);

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument 'enc_dec_thr()'\n");
		exit(1);
	}

	while(thr_ctx->flag_exit== 0) {
		mg_mgr_poll(&mgr, 1000);
		thr_ctx->flag_http_server_running= 1;
	}

	mg_mgr_free(&mgr);
	return NULL;
}

/**
 * Encoding-decoding loop thread.
 */
static void* enc_dec_thr(void *t)
{
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	proc_frame_ctx_t *proc_frame_ctx= NULL;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument '%s()'\n", __FUNCTION__);
		exit(1);
	}

	while(thr_ctx->flag_exit== 0) {
		int ret_code;

		/* Receive encoded frame */
		if(proc_frame_ctx!= NULL)
			proc_frame_ctx_release(&proc_frame_ctx);
		ret_code= procs_recv_frame(thr_ctx->procs_ctx, thr_ctx->enc_proc_id,
				&proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				usleep(1); // Avoid closed loops ("schedule")
			else
				fprintf(stderr, "Error while encoding frame'\n");
			continue;
		}

		/* Send encoded frame to decoder */
		CHECK(proc_frame_ctx!= NULL);
		ret_code= procs_send_frame(thr_ctx->procs_ctx, thr_ctx->dec_proc_id,
				proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				usleep(1); // Avoid closed loops ("schedule")
			else
				fprintf(stderr, "Error while decoding frame'\n");
			continue;
		}
	}

	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return NULL;
}

static void check_psnr(int width, int height, uint8_t *frame_original,
		uint8_t *frame_processed, int min_psnr_val)
{
  int i, y, x, pos_offset= 0;
  int size= height* width;
  double signal=0.0, noise=0.0, peak=0.0, mse;
  double psnr1= 0;

  if(frame_original== NULL || frame_processed== NULL) {
	  CHECK(0);
	  return;
  }

  /* YUV 4:2:0 */
  for(i= 0; i< 3; i++) {
	  int w= width>>(i!= 0), h= height>>(i!= 0);
	  for(y= 0; y< h; y++) {
		  for(x= 0; x< w; x++) {
			  int pos= pos_offset+ y* w+ x;
			  signal+= SQR((double)frame_original[pos]);
			  noise+= SQR((double)frame_original[pos]-
					  (double)frame_processed[pos]);
			  if(peak< (double)frame_original[pos])
				  peak= (double)frame_original[pos];
		  }
	  }
	  pos_offset+= h* w;
  }

  mse= noise/(double)size;  // mean square error
  psnr1= 10.0*log10(SQR(255.0)/mse);
  //printf("MSE: %lf\n", mse);
  //printf("SNR: %lf (dB)\n", 10.0*log10(signal/noise));
  CHECK(psnr1> min_psnr_val);
  if(psnr1<= min_psnr_val)
	  printf("PSNR(max=255): %lf (dB)\n", psnr1);
  //printf("PSNR(max=%lf): %lf (dB)\n", peak, 10.0*log10(SQR(peak)/mse));
}

static void treat_output_frame_video(proc_frame_ctx_t *proc_frame_ctx,
		FILE **ref_file, int min_psnr_val)
{
	int64_t pts;
	int w_Y, h_Y, i, y, x, pos_dst_offset= 0;
	uint8_t *frame_original= NULL, *frame_encdec= NULL;

	/* Check arguments */
	if(proc_frame_ctx== NULL || ref_file== NULL)
		return;

	w_Y= proc_frame_ctx->width[0];
	h_Y= proc_frame_ctx->height[0];
	frame_original= (uint8_t*)malloc((w_Y* h_Y* 3)>> 1);
	frame_encdec= (uint8_t*)malloc((w_Y* h_Y* 3)>> 1);
	if(frame_original== NULL || frame_encdec== NULL)
		goto end;

	pts= proc_frame_ctx->pts;
	for(i= 0; i< 3; i++) {
		int w= proc_frame_ctx->width[i];
		int h= proc_frame_ctx->height[i];
		int stride= proc_frame_ctx->linesize[i];
		for(y= 0; y< h; y++) {
			for(x= 0; x< w; x++) {
				int pos_src= y* stride+ x;
				int pos_dst= pos_dst_offset+ y* w+ x;
				int dec_val= proc_frame_ctx->p_data[i][pos_src];
				int orig_val= (i== 0)? (x+ y+ pts* 3)& 0xFF:
						(i== 1)? (128+ y+ pts* 2)& 0xFF:
						(64+ x+ pts* 5)& 0xFF;

				/* Store YCrCb values to compute PSNR */
				frame_original[pos_dst]= orig_val;
				frame_encdec[pos_dst]= dec_val;
			}
		}
		pos_dst_offset+= h* w;
	}

	check_psnr(proc_frame_ctx->width[0], proc_frame_ctx->height[0],
			frame_original, frame_encdec, min_psnr_val);

#ifdef WRITE_2_OFILE
	static FILE *file_raw= NULL;
	if(file_raw== NULL) {
	    if((file_raw= fopen("/tmp/out_raw.yuv", "wb"))== NULL) {
	        fprintf(stderr, "Could not open %s\n", "/tmp/out_raw.yuv");
	        exit(1);
	    }
	}
	fwrite(frame_original, 1, pos_dst_offset, file_raw);
	/* Open the output file if applicable */
	if(*ref_file== NULL) {
	    if((*ref_file= fopen(OUTPUT_FILE_VIDEO, "wb"))== NULL) {
	        fprintf(stderr, "Could not open %s\n", OUTPUT_FILE_VIDEO);
	        exit(1);
	    }
	}
	fwrite(frame_encdec, 1, pos_dst_offset, *ref_file);
#endif

end:
	if(frame_original!= NULL) {
		free(frame_original);
		frame_original= NULL;
	}
	if(frame_encdec!= NULL) {
		free(frame_encdec);
		frame_encdec= NULL;
	}
}

static void treat_output_frame_audio(proc_frame_ctx_t *proc_frame_ctx,
		FILE **ref_file, int min_psnr_val)
{
	/* Check arguments */
	if(proc_frame_ctx== NULL || ref_file== NULL)
		return;

#ifdef WRITE_2_OFILE
	/* Open the output file if applicable */
	if(*ref_file== NULL) {
		if((*ref_file= fopen(OUTPUT_FILE_AUDIO, "wb"))== NULL) {
			fprintf(stderr, "Could not open %s\n", OUTPUT_FILE_AUDIO);
			exit(1);
		}
	}
	// Write interlaced not planar
	fwrite(proc_frame_ctx->p_data[0], 1, proc_frame_ctx->width[0], *ref_file);
#endif
}

static void* consumer_thr(void *t)
{
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	void(*treat_output_frame)(proc_frame_ctx_t*, FILE**, int)= NULL;
	FILE *file= NULL;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument 'consumer_thr()'\n");
		exit(1);
	}

	/* Get function to treat output frame */
	switch(thr_ctx->media_type) {
	case MEDIA_TYPE_VIDEO:
		treat_output_frame= treat_output_frame_video;
		break;
	case MEDIA_TYPE_AUDIO:
		treat_output_frame= treat_output_frame_audio;
		break;
	default:
		break;
	}

	/* Output loop */
	while(thr_ctx->flag_exit== 0) {
		int ret_code;

		/* Receive decoded frame */
		if(proc_frame_ctx!= NULL)
			proc_frame_ctx_release(&proc_frame_ctx);
		ret_code= procs_recv_frame(thr_ctx->procs_ctx, thr_ctx->dec_proc_id,
				&proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				usleep(1); // Avoid closed loops ("schedule")
			else
				fprintf(stderr, "Error while receiving decoded frame'\n");
			continue;
		}
		CHECK(proc_frame_ctx!= NULL);

		/* Write encoded-decoded frame to output */
		if(proc_frame_ctx!= NULL && treat_output_frame!= NULL)
			treat_output_frame(proc_frame_ctx, &file, thr_ctx->min_psnr_val);
	}

	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	if(file!= NULL)
		fclose(file);
	return NULL;
}

static void prepare_and_send_raw_video_data(procs_ctx_t *procs_ctx,
		int enc_proc_id, volatile int *ref_flag_exit)
{
    uint8_t *p_data_y, *p_data_cr, *p_data_cb;
    int64_t frame_period_usec, frame_period_90KHz;
	int x, y;
    const int width= atoi(VIDEO_WIDTH), height= atoi(VIDEO_HEIGHT);
    uint8_t *buf= NULL;
    proc_frame_ctx_s proc_frame_ctx= {0};
    const char *fps_cstr= "30";

	/* Prepare raw data buffer */
	buf= (uint8_t*)malloc((width* height* 3)/ 2);
    if(buf== NULL) {
        CHECK(false);
        goto end;
    }
    proc_frame_ctx.data= buf;
    proc_frame_ctx.p_data[0]= buf;
    proc_frame_ctx.p_data[1]= proc_frame_ctx.p_data[0]+ (width* height);
    proc_frame_ctx.p_data[2]= proc_frame_ctx.p_data[1]+ ((width* height)/4);
    proc_frame_ctx.width[0]= proc_frame_ctx.linesize[0]= width;
    proc_frame_ctx.width[1]= proc_frame_ctx.linesize[1]= width>> 1;
    proc_frame_ctx.width[2]= proc_frame_ctx.linesize[2]= width>> 1;
    proc_frame_ctx.height[0]= height;
    proc_frame_ctx.height[1]= height>> 1;
    proc_frame_ctx.height[2]= height>> 1;
    proc_frame_ctx.proc_sample_fmt= PROC_IF_FMT_YUV420P;

    /* Encode few seconds of video */
    p_data_y= (uint8_t*)proc_frame_ctx.p_data[0];
    p_data_cr= (uint8_t*)proc_frame_ctx.p_data[1];
    p_data_cb= (uint8_t*)proc_frame_ctx.p_data[2];
    frame_period_usec= 1000000/ atoi(fps_cstr); //usecs
    frame_period_90KHz= (frame_period_usec/1000/*[msec]*/)*
    		90/*[ticks/msec]*/; //ticks
    for(; *ref_flag_exit== 0;) {

        usleep((unsigned int)frame_period_usec); //comment-me
        proc_frame_ctx.pts+= frame_period_90KHz;

        /* Y */
        for(y= 0; y< height; y++)
        	for(x= 0; x< width; x++)
        		p_data_y[y* proc_frame_ctx.linesize[0]+ x]= x+ y+
				proc_frame_ctx.pts* 3;
        /* Cb and Cr */
        for(y= 0; y< height>> 1; y++) {
            for(x= 0; x< width>> 1; x++) {
            	p_data_cr[y* proc_frame_ctx.linesize[1]+ x]= 128+ y+
            			proc_frame_ctx.pts* 2;
            	p_data_cb[y* proc_frame_ctx.linesize[2]+ x]= 64+ x+
            			proc_frame_ctx.pts* 5;
            }
        }

        /* Encode the image */
        procs_send_frame(procs_ctx, enc_proc_id, &proc_frame_ctx);
    }

end:
	if(buf!= NULL) {
		free(buf);
		buf= NULL;
	}
}

static void prepare_and_send_raw_audio_data(procs_ctx_t *procs_ctx,
		int enc_proc_id, volatile int *ref_flag_exit)
{
    float sin_time, tincr;
    int64_t frame_period_usec, frame_period_90KHz;
    int frame_size_bytes, frame_size_samples, ret_code;
	char *rest_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
    uint8_t *buf= NULL;
    proc_frame_ctx_s proc_frame_ctx= {0};
    const char *sample_rate_cstr= SETTINGS_SAMPLE_RATE;

	/* Get frame size requested by audio encoder. We need this data to
	 * know how many bytes to send to the encoder.
	 */
	ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", enc_proc_id, &rest_str);
	if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
		CHECK(false);
		goto end;
	}
	//printf("PROCS_ID_GET: '%s'\n", rest_str); fflush(stdout); //comment-me
	if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
		CHECK(false);
		goto end;
	}
	if((cjson_aux= cJSON_GetObjectItem(cjson_rest,
			"expected_frame_size_iput"))== NULL) {
		CHECK(false);
		goto end;
	}
	CHECK((frame_size_bytes= cjson_aux->valuedouble* sizeof(uint16_t))>= 0);
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;

    /* Allocate raw-data frame buffer */
	buf= (uint8_t*)malloc(frame_size_bytes* 2); // stereo: two channels
    if(buf== NULL) {
        fprintf(stderr, "Could not allocate raw image buffer\n");
        CHECK(false);
        goto end;
    }
    frame_size_samples= frame_size_bytes>> 1; // 16-bit samples
    proc_frame_ctx.data= buf;
    proc_frame_ctx.p_data[0]= buf;
    proc_frame_ctx.p_data[1]= buf+ frame_size_bytes;
    proc_frame_ctx.width[0]= proc_frame_ctx.linesize[0]= frame_size_bytes;
    proc_frame_ctx.width[1]= proc_frame_ctx.linesize[1]= frame_size_bytes;
    proc_frame_ctx.height[0]= 1;
    proc_frame_ctx.height[1]= 1;
    proc_frame_ctx.proc_sample_fmt= PROC_IF_FMT_S16P;
    proc_frame_ctx.pts= 0;

    /* Prepare raw data for a single 440Hz tone sound and send to encoder */
    frame_period_usec= (1000000* frame_size_samples)/
    		atoi(sample_rate_cstr); //usecs
    frame_period_90KHz= (frame_period_usec/1000/*[msec]*/)*
    		90/*[ticks/msec]*/; //ticks
    sin_time= 0;
    tincr= 2* M_PI* 440.0/ atoi(sample_rate_cstr);
    for(; *ref_flag_exit== 0;) {
    	int j;
    	uint16_t *samples= (uint16_t*)proc_frame_ctx.data;

        usleep((unsigned int)frame_period_usec); //comment-me
        proc_frame_ctx.pts+= frame_period_90KHz;

        for(j= 0; j< frame_size_samples; j++, sin_time+= tincr) {
        	int sample_val= (int)(sin(sin_time)* 10000);
            samples[j]= sample_val; // right channel
            samples[j+ frame_size_samples]= sample_val; // left channel
        }
        /* Encode the samples */
        ret_code= procs_send_frame(procs_ctx, enc_proc_id, &proc_frame_ctx);
    }

end:
	if(rest_str!= NULL)
		free(rest_str);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(buf!= NULL) {
		free(buf);
		buf= NULL;
	}
}

static void* producer_thr(void *t)
{
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	void(*prepare_and_send_raw_data)(procs_ctx_t *procs_ctx, int enc_proc_id,
			volatile int *ref_flag_exit)= NULL;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument 'consumer_thr()'\n");
		exit(1);
	}

	/* Get function to treat output frame */
	switch(thr_ctx->media_type) {
	case MEDIA_TYPE_VIDEO:
		prepare_and_send_raw_data= prepare_and_send_raw_video_data;
		break;
	case MEDIA_TYPE_AUDIO:
		prepare_and_send_raw_data= prepare_and_send_raw_audio_data;
		break;
	default:
		break;
	}

	/* Producer loop */
	while(thr_ctx->flag_exit== 0) {
		usleep(1); // Kind-of schedule to avoid 100% CPU loops
		prepare_and_send_raw_data(thr_ctx->procs_ctx, thr_ctx->enc_proc_id,
				&thr_ctx->flag_exit);
	}

	return NULL;
}

static void encdec_loopback(const proc_if_t *proc_if_enc,
		const proc_if_t *proc_if_dec, int media_type, int min_psnr_val)
{
	pthread_t producer_thread, enc_dec_thread, consumer_thread,
		http_server_thread;
	int ret_code, enc_proc_id= -1, dec_proc_id= -1;
	procs_ctx_t *procs_ctx= NULL;
	char *rest_str= NULL, *settings_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
    thr_ctx_s thr_ctx= {0};

    if(proc_if_enc== NULL || proc_if_dec== NULL) {
    	fprintf(stderr, "%s %d Bad arguments.\n", __FILE__, __LINE__);
    	return;
    }

    /* Open LOG module */
    log_module_open();

	/* Register all FFmpeg's CODECS */
	avcodec_register_all();

	/* Open processors (PROCS) module */
	ret_code= procs_module_open(NULL);
	if(ret_code!= STAT_SUCCESS) {
		CHECK(false);
		goto end;
	}

	/* Register encoder and decoder processor types */
	ret_code= procs_module_opt("PROCS_REGISTER_TYPE", proc_if_enc);
	if(ret_code!= STAT_SUCCESS) {
		CHECK(false);
		goto end;
	}
	ret_code= procs_module_opt("PROCS_REGISTER_TYPE", proc_if_dec);
	if(ret_code!= STAT_SUCCESS) {
		CHECK(false);
		goto end;
	}

	/* Get PROCS module's instance */
	procs_ctx= procs_open(NULL);
	if(procs_ctx== NULL) {
		CHECK(false);
		goto end;
	}

    /* Register (open) a encoder instance */
	ret_code= procs_opt(procs_ctx, "PROCS_POST", proc_if_enc->proc_name, "",
			&rest_str);
	if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
		CHECK(false);
		goto end;
	}

	/* Parse response to get encoder Id. */
	if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
		CHECK(false);
		goto end;
	}
	if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id"))== NULL) {
		CHECK(false);
		goto end;
	}
	CHECK((enc_proc_id= cjson_aux->valuedouble)>= 0);
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;

    /* Register (open) a decoder instance */
	ret_code= procs_opt(procs_ctx, "PROCS_POST", proc_if_dec->proc_name, "",
			&rest_str);
	if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
		CHECK(false);
		goto end;
	}

	/* Parse response to get encoder Id. */
	if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
		CHECK(false);
		goto end;
	}
	if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id"))== NULL) {
		CHECK(false);
		goto end;
	}
	CHECK((dec_proc_id= cjson_aux->valuedouble)>= 0);
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;

    /* Launch producer, encoding-decoding, consumer and HTTP-sever threads */
    thr_ctx.flag_exit= 0;
    thr_ctx.flag_http_server_running= 0;
    thr_ctx.enc_proc_id= enc_proc_id;
    thr_ctx.dec_proc_id= dec_proc_id;
    thr_ctx.media_type= media_type;
    thr_ctx.min_psnr_val= min_psnr_val;
    thr_ctx.procs_ctx= procs_ctx;
	ret_code= pthread_create(&producer_thread, NULL, producer_thr, &thr_ctx);
	if(ret_code!= 0) {
		CHECK(false);
		goto end;
	}
	ret_code= pthread_create(&enc_dec_thread, NULL, enc_dec_thr, &thr_ctx);
	if(ret_code!= 0) {
		CHECK(false);
		goto end;
	}
	ret_code= pthread_create(&consumer_thread, NULL, consumer_thr, &thr_ctx);
	if(ret_code!= 0) {
		CHECK(false);
		goto end;
	}
	ret_code= pthread_create(&http_server_thread, NULL, http_server_thr,
			&thr_ctx);
	if(ret_code!= 0) {
		CHECK(false);
		goto end;
	}

	/* Ugly (we should use signal and wait...) but practical */
	while(thr_ctx.flag_http_server_running!= 1) {
		usleep(1000*100);
	}

	/* Change settings using API Rest through HTTP */
	for(int i= 0; test_settings_patterns[i][media_type][0]!= NULL; i++) {
		char *settings_str_p;
		const char *query_str= test_settings_patterns[i][media_type][0];
		const char *json_str= test_settings_patterns[i][media_type][1];

		/* Put new settings.
		 * (We know Id. 0 is the first instantiated processor: the encoder).
		 */
		printf("\nNew settings\n"); //comment-me
		printf("//------------------------//\n"); fflush(stdout); //comment-me
		rest_str= http_client_request("PUT", "/procs/0.json", query_str, NULL);
		if(ret_code!= STAT_SUCCESS) {
			CHECK(false);
			goto end;
		}
		if(rest_str!= NULL) {
			free(rest_str); rest_str= NULL;
		}

		/* Check new settings */
		rest_str= http_client_request("GET", "/procs/0.json", NULL,
				NULL);
		if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
			CHECK(false);
			goto end;
		}
		if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
			CHECK(false);
			goto end;
		}
		if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "data"))== NULL) {
			CHECK(false);
			goto end;
		}
		if((cjson_aux= cJSON_GetObjectItem(cjson_aux, "settings"))== NULL) {
			CHECK(false);
			goto end;
		}
		if(settings_str!= NULL) {
			free(settings_str);
			settings_str= NULL;
		}
		if((settings_str= cJSON_PrintUnformatted(cjson_aux))== NULL) {
			CHECK(false);
			goto end;
		}
		printf("Response (settings): '%s'\n", settings_str); //comment-me
		fflush(stdout); //comment-me
		/* Check response.
		 * Note we omit specific codec parameters -as response may be an
		 * extended settings version- and the closing '}' character
		 * (thus the '-1').
		 */
		settings_str_p= strstr(settings_str, "\"bit_rate_output\"");
		CHECK(strncmp(settings_str_p, json_str, strlen(json_str))== 0);
		free(rest_str); rest_str= NULL;
		cJSON_Delete(cjson_rest); cjson_rest= NULL;

		usleep(1000*1000*TEST_DURATION_SEC);
	}

    /* Join the threads */
    thr_ctx.flag_exit= 1;
	ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE",
			enc_proc_id); // before joining to unblock processor
	CHECK(ret_code== STAT_SUCCESS);
	ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", dec_proc_id);
	CHECK(ret_code== STAT_SUCCESS);
	pthread_join(producer_thread, NULL);
	pthread_join(enc_dec_thread, NULL);
	pthread_join(consumer_thread, NULL);
	pthread_join(http_server_thread, NULL);

end:
	if(procs_ctx!= NULL)
		procs_close(&procs_ctx);
	procs_module_close();
	log_module_close();
	if(rest_str!= NULL)
		free(rest_str);
	if(settings_str!= NULL)
		free(settings_str);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
}

SUITE(UTESTS_ENCODE_DECODE_1)
{
	TEST(ENCODE_DECODE_X264)
	{
		encdec_loopback(&proc_if_ffmpeg_x264_enc, &proc_if_ffmpeg_x264_dec,
				MEDIA_TYPE_VIDEO, MIN_PSNR_VAL);
	}
	TEST(ENCODE_DECODE_MPEG2_VIDEO)
	{
		encdec_loopback(&proc_if_ffmpeg_m2v_enc, &proc_if_ffmpeg_m2v_dec,
				MEDIA_TYPE_VIDEO, MIN_PSNR_VAL);
	}
	TEST(ENCODE_DECODE_MLHE)
	{
		/* NOTE: For MLHE we still do not get all PSNR values above 15 dB, so
		 * we treat it as a special case.
		 */
		//encdec_loopback(&proc_if_ffmpeg_mlhe_enc, &proc_if_ffmpeg_mlhe_dec,
		//		MEDIA_TYPE_VIDEO, 15/*MIN_PSNR_VAL*/);
		// Commented: by the moment LHE has some memory leaks
	}
	TEST(ENCODE_DECODE_MP3)
	{
		encdec_loopback(&proc_if_ffmpeg_mp3_enc, &proc_if_ffmpeg_mp3_dec,
				MEDIA_TYPE_AUDIO, MIN_PSNR_VAL);
	}
}

