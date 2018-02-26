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
 * @file codecs_muxers_loopback.c
 * @brief Complete encoding->multiplexing->demultiplexing->decoding->rendering
 * loopback example.
 * @author Rafael Antoniello
 *
 *
 */

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
#include <SDL2/SDL.h>

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocsutils/schedule.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/procs.h>
#include <libmediaprocs/procs_api_http.h>
#include <libmediaprocs/proc.h>
#include <libmediaprocsmuxers/live555_rtsp.h>
#include <libmediaprocscodecs/ffmpeg_x264.h>
#include <libmediaprocscodecs/ffmpeg_m2v.h>
#include <libmediaprocscodecs/ffmpeg_mp3.h>
#include <libmediaprocscodecs/ffmpeg_lhe.h>

/* **** Definitions **** */

#define LISTENING_PORT 			"8088"
#define LISTENING_HOST 			"127.0.0.1"

#define VIDEO_WIDTH 			"352"
#define VIDEO_HEIGHT 			"288"

#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

static volatile int flag_app_exit= 0;


/**
 * Common data passed to all the threads launched by this application.
 * To simplify, we put all the necessary data together in the same type of
 * structure.
 */
typedef struct thr_ctx_s {
	volatile int flag_exit;
	int enc_proc_id, dec_proc_id, mux_proc_id, dmux_proc_id;
	int elem_strem_id_video_server;
	const char *mime_setting_video;
	procs_ctx_t *procs_ctx;
} thr_ctx_t;

static void prepare_and_send_raw_video_data(procs_ctx_t *procs_ctx,
		int enc_proc_id, volatile int *ref_flag_exit)
{
    uint8_t *p_data_y, *p_data_cr, *p_data_cb;
    int64_t frame_period_usec, frame_period_90KHz;
	int x, y;
    const int width= atoi(VIDEO_WIDTH), height= atoi(VIDEO_HEIGHT);
    uint8_t *buf= NULL;
    proc_frame_ctx_t proc_frame_ctx= {0};
    const char *fps_cstr= "30";

	/* Prepare raw data buffer */
	buf= (uint8_t*)malloc((width* height* 3)/ 2);
    if(buf== NULL) {
		fprintf(stderr, "Could not allocate producer raw data buffer\n");
		exit(-1);
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
    proc_frame_ctx.es_id= 0;

    /* Encode few seconds of video */
    p_data_y= (uint8_t*)proc_frame_ctx.p_data[0];
    p_data_cr= (uint8_t*)proc_frame_ctx.p_data[1];
    p_data_cb= (uint8_t*)proc_frame_ctx.p_data[2];
    frame_period_usec= 1000000/ atoi(fps_cstr); //usecs
    frame_period_90KHz= (frame_period_usec/1000/*[msec]*/)*
    		90/*[ticks/msec]*/; //ticks
    for(; *ref_flag_exit== 0;) {

        usleep((unsigned int)frame_period_usec); //simulate real-time FPS
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

	if(buf!= NULL) {
		free(buf);
		buf= NULL;
	}
}

/**
 * Raw data producer and encoding thread.
 */
static void* producer_thr_video(void *t)
{
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument '%s'\n", __FUNCTION__);
		exit(1);
	}

	/* Producer loop */
	while(thr_ctx->flag_exit== 0) {
		prepare_and_send_raw_video_data(thr_ctx->procs_ctx,
				thr_ctx->enc_proc_id, &thr_ctx->flag_exit);
	}

	return NULL;
}

/**
 * Multiplexer thread.
 */
static void* mux_thr(void *t)
{
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	proc_frame_ctx_t *proc_frame_ctx= NULL;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument '%s'\n", __FUNCTION__);
		exit(1);
	}

	/* Get frame from encoder and send to multiplexer */
	while(thr_ctx->flag_exit== 0) {
		int ret_code;

		/* Receive encoded frame */
		if(proc_frame_ctx!= NULL)
			proc_frame_ctx_release(&proc_frame_ctx);
		ret_code= procs_recv_frame(thr_ctx->procs_ctx, thr_ctx->enc_proc_id,
				&proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				schedule(); // Avoid closed loops
			else
				fprintf(stderr, "Error while encoding frame'\n");
			continue;
		}

		/* Send encoded frame to multiplexer.
		 * IMPORTANT: Set correctly the elementary stream Id. to be able to
		 * correctly multiplex each frame.
		 */
		if(proc_frame_ctx== NULL)
			continue;
		proc_frame_ctx->es_id= thr_ctx->elem_strem_id_video_server;
		ret_code= procs_send_frame(thr_ctx->procs_ctx, thr_ctx->mux_proc_id,
				proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				schedule(); // Avoid closed loops
			else
				fprintf(stderr, "Error while multiplexing frame'\n");
			continue;
		}
	}

	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return NULL;
}

/**
 * De-multiplexer thread.
 */
static void* dmux_thr(void *t)
{
	int i, ret_code, elem_strem_id_video_client= -1;
	int elementary_streams_cnt= 0;
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	char *rest_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_es_array= NULL, *cjson_aux= NULL;

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument '%s'\n", __FUNCTION__);
		exit(1);
	}

	/* Receive first frame from de-multiplexer -EPILOGUE-.
	 * The first time we receive data we have to check the elementary stream
	 * Id's. The idea is to use the elementary stream Id's to send each
	 * de-multiplexed frame to the correct decoding sink.
	 * We do this once, the first time we are receiving any frame,
	 * by consulting the de-multiplexer API.
	 */
	ret_code= STAT_EAGAIN;
	while(ret_code!= STAT_SUCCESS && thr_ctx->flag_exit== 0) {
		schedule(); // Avoid closed loops
		ret_code= procs_recv_frame(thr_ctx->procs_ctx, thr_ctx->dmux_proc_id,
				&proc_frame_ctx);
	}
	if(ret_code!= STAT_SUCCESS || proc_frame_ctx== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

	/* Parse elementary streams Id's */
	ret_code= procs_opt(thr_ctx->procs_ctx, "PROCS_ID_GET",
			thr_ctx->dmux_proc_id, &rest_str);
	if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	// Elementary streams objects array
	if((cjson_es_array= cJSON_GetObjectItem(cjson_rest,
			"elementary_streams"))== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	// Iterate elementary stream objects and find the corresponding Id.
	elementary_streams_cnt= cJSON_GetArraySize(cjson_es_array);
	for(i= 0; i< elementary_streams_cnt; i++) {
		cJSON *cjson_es= cJSON_GetArrayItem(cjson_es_array, i);
		if(cjson_es!= NULL) {
			int elementary_stream_id;
			char *mime;
			const char *mime_needle= thr_ctx->mime_setting_video+
					strlen("sdp_mimetype=");

			/* Get stream Id. */
			cjson_aux= cJSON_GetObjectItem(cjson_es, "elementary_stream_id");
			if(cjson_aux== NULL) {
				fprintf(stderr, "Error at line: %d\n", __LINE__);
				exit(-1);
			}
			elementary_stream_id= cjson_aux->valueint;

			/* Check MIME type and assign Id. */
			cjson_aux= cJSON_GetObjectItem(cjson_es, "sdp_mimetype");
			if(cjson_aux== NULL) {
				fprintf(stderr, "Error at line: %d\n", __LINE__);
				exit(-1);
			}
			mime= cjson_aux->valuestring;
			if(mime!= NULL && strcasecmp(mime_needle, mime)== 0)
				elem_strem_id_video_client= elementary_stream_id;
		}
	}
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;
	if(elem_strem_id_video_client< 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

	/* Send first received frame to decoder */
	ret_code= procs_send_frame(thr_ctx->procs_ctx, thr_ctx->dec_proc_id,
			proc_frame_ctx);
	if(ret_code!= STAT_SUCCESS)
		fprintf(stderr, "Error while decoding frame'\n");

	/* De-multiplexer loop */
	while(thr_ctx->flag_exit== 0) {

		/* Receive frame from de-multiplexer */
		if(proc_frame_ctx!= NULL)
			proc_frame_ctx_release(&proc_frame_ctx);
		ret_code= procs_recv_frame(thr_ctx->procs_ctx, thr_ctx->dmux_proc_id,
				&proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				schedule(); // Avoid closed loops
			else
				fprintf(stderr, "Error while de-multiplexing frame'\n");
			continue;
		}

		/* Send received encoded frame to decoder */
		if(proc_frame_ctx== NULL)
			continue;
		ret_code= procs_send_frame(thr_ctx->procs_ctx, thr_ctx->dec_proc_id,
				proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				schedule(); // Avoid closed loops
			else
				fprintf(stderr, "Error while decoding frame'\n");
			continue;
		}
	}

	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	return NULL;
}

static void* consumer_thr_video(void *t)
{
	thr_ctx_t *thr_ctx= (thr_ctx_t*)t;
	SDL_Window *sdlWindow= NULL;
	SDL_Renderer *sdlRenderer= NULL;
	const uint32_t pixformat= SDL_PIXELFORMAT_IYUV;
	SDL_Texture *sdlTexture= NULL;
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	int w_Y= atoi(VIDEO_WIDTH),
			h_Y= atoi(VIDEO_HEIGHT); // Initial values, may change and update.

	/* Check argument */
	if(thr_ctx== NULL) {
		fprintf(stderr, "Bad argument '%s'\n", __FUNCTION__);
		exit(-1);
	}

	/* **** SDL2 initialization **** */

	if(SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
		exit(-1);
	}
	sdlWindow= SDL_CreateWindow("codecs_muxers_loopback.c",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w_Y, h_Y,
			SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
	if(!sdlWindow) {
		fprintf(stderr, "SDL: could not create window:%s\n",SDL_GetError());
		exit(-1);
	}
	sdlRenderer= SDL_CreateRenderer(sdlWindow, -1, 0);
	if(!sdlRenderer) {
		fprintf(stderr, "SDL: could not create renderer:%s\n",SDL_GetError());
		exit(-1);
	}
	sdlTexture= SDL_CreateTexture(sdlRenderer, pixformat,
			SDL_TEXTUREACCESS_STREAMING, w_Y, h_Y);
	if(!sdlTexture) {
		fprintf(stderr, "SDL: could not create texture:%s\n",SDL_GetError());
		exit(-1);
	}

	/* **** Output loop **** */
	while(thr_ctx->flag_exit== 0) {
		SDL_Event event;
		int w_Y_iput, h_Y_iput, ret_code;

		/* Receive decoded frame */
		if(proc_frame_ctx!= NULL)
			proc_frame_ctx_release(&proc_frame_ctx);
		ret_code= procs_recv_frame(thr_ctx->procs_ctx, thr_ctx->dec_proc_id,
				&proc_frame_ctx);
		if(ret_code!= STAT_SUCCESS) {
			if(ret_code== STAT_EAGAIN)
				schedule(); // Avoid closed loops
			else
				fprintf(stderr, "Error while receiving decoded frame'\n");
			continue;
		}

		/* **** Write encoded-decoded frame to output if applicable **** */

		if(proc_frame_ctx== NULL) {
			continue;
		}

		/* Get and check input frame with and height */
		w_Y_iput= proc_frame_ctx->width[0];
		h_Y_iput= proc_frame_ctx->height[0];
		if(w_Y_iput<= 0 || h_Y_iput<= 0) {
			fprintf(stderr, "Invalid frame size\n");
			exit(-1);
		}

		/* Resize if applicable */
		if(w_Y_iput!= w_Y || h_Y_iput!= h_Y) {

			w_Y= w_Y_iput;
			h_Y= h_Y_iput;

			/* Push resize event to SDL2 */
			event.type= SDL_WINDOWEVENT;
			SDL_PushEvent(&event);
		}

		/* Push refresh event to SDL2 */
		event.type= REFRESH_EVENT;
		SDL_PushEvent(&event);

		/* Wait for SDL event to be consumed */
		SDL_WaitEvent(&event);
		if(event.type== REFRESH_EVENT) {
			SDL_Rect sdlRect= {
					0, //x
					0, //y
					w_Y_iput, h_Y_iput
			};
			SDL_UpdateYUVTexture(sdlTexture, NULL,
					proc_frame_ctx->p_data[0], proc_frame_ctx->linesize[0],
					proc_frame_ctx->p_data[1], proc_frame_ctx->linesize[1],
					proc_frame_ctx->p_data[2], proc_frame_ctx->linesize[2]);
			SDL_RenderClear(sdlRenderer);
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
			SDL_RenderPresent(sdlRenderer);

		}else if(event.type==SDL_WINDOWEVENT){
			/* Resize */
			SDL_SetWindowSize(sdlWindow, w_Y, h_Y);
		}else if(event.type==SDL_QUIT){
			flag_app_exit= 1;
		}else if(event.type==BREAK_EVENT){
			break;
		}

	} // end-loop

	if(proc_frame_ctx!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx);
	if(sdlTexture!= NULL)
		SDL_DestroyTexture(sdlTexture);
	if(sdlRenderer!= NULL) {
		SDL_RenderPresent(sdlRenderer);
		SDL_RenderClear(sdlRenderer);
		SDL_DestroyRenderer(sdlRenderer);
	}
	if(sdlWindow!= NULL)
		SDL_DestroyWindow(sdlWindow);
	SDL_Quit();
	return NULL;
}

static void http_event_handler(struct mg_connection *c, int ev, void *p)
{
#define URI_MAX 4096
#define METH_MAX 16
#define BODY_MAX 4096000

	if(ev== MG_EV_HTTP_REQUEST) {
		register size_t uri_len= 0, method_len= 0, qs_len= 0, body_len= 0;
		const char *uri_p, *method_p, *qs_p, *body_p;
		struct http_message *hm= (struct http_message*)p;
		char *url_str= NULL, *method_str= NULL, *str_response= NULL,
				*qstring_str= NULL, *body_str= NULL;
		thr_ctx_t *thr_ctx= (thr_ctx_t*)c->user_data;

		if((uri_p= hm->uri.p)!= NULL && (uri_len= hm->uri.len)> 0 &&
				uri_len< URI_MAX) {
			url_str= (char*)calloc(1, uri_len+ 1);
			if(url_str!= NULL)
				memcpy(url_str, uri_p, uri_len);
		}
		if((method_p= hm->method.p)!= NULL && (method_len= hm->method.len)> 0
				 && method_len< METH_MAX) {
			method_str= (char*)calloc(1, method_len+ 1);
			if(method_str!= NULL)
				memcpy(method_str, method_p, method_len);
		}
		if((qs_p= hm->query_string.p)!= NULL &&
				(qs_len= hm->query_string.len)> 0 && qs_len< URI_MAX) {
			qstring_str= (char*)calloc(1, qs_len+ 1);
			if(qstring_str!= NULL)
				memcpy(qstring_str, qs_p, qs_len);
		}
		if((body_p= hm->body.p)!= NULL && (body_len= hm->body.len)> 0
				&& body_len< BODY_MAX) {
			body_str= (char*)calloc(1, body_len+ 1);
			if(body_str!= NULL)
				memcpy(body_str, body_p, body_len);
		}

		/* Process HTTP request */
		if(url_str!= NULL && method_str!= NULL)
			procs_api_http_req_handler(thr_ctx->procs_ctx, url_str,
					qstring_str, method_str, body_str, body_len, &str_response);
		/* Send response */
		if(str_response!= NULL && strlen(str_response)> 0) {
			//printf("str_response: %s (len: %d)\n", str_response,
			//		(int)strlen(str_response)); //comment-me
			mg_printf(c, "%s", "HTTP/1.1 200 OK\r\n");
			mg_printf(c, "Content-Length: %d\r\n", (int)strlen(str_response));
			mg_printf(c, "\r\n");
			mg_printf(c, "%s", str_response);
		} else {
			mg_printf(c, "%s", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
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
	} else if(ev== MG_EV_RECV) {
		mg_printf(c, "%s", "HTTP/1.1 202 ACCEPTED\r\nContent-Length: 0\r\n");
	} else if(ev== MG_EV_SEND) {
		mg_printf(c, "%s", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
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
		fprintf(stderr, "Bad argument '%s'\n", __FUNCTION__);
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

	while(flag_app_exit== 0)
		mg_mgr_poll(&mgr, 1000);

	mg_mgr_free(&mgr);
	return NULL;
}

static void stream_proc_quit_signal_handler()
{
	printf("signaling application to finalize...\n"); fflush(stdout);
	flag_app_exit= 1;
}

/**
 * Register (open) a processor instance:
 * 1.- Register processor with given initial settings if desired,
 * 2.- Parse JSON-REST response to get processor Id.
 */
static void procs_post(procs_ctx_t *procs_ctx, const char *proc_name,
		const char *proc_settings, int *ref_proc_id)
{
	int ret_code;
	char *rest_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;

	ret_code= procs_opt(procs_ctx, "PROCS_POST", proc_name, proc_settings,
			&rest_str);
	if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "proc_id"))== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((*ref_proc_id= cjson_aux->valuedouble)< 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;
}

int main(int argc, char* argv[])
{
	sigset_t set;
	pthread_t producer_thread, mux_thread, dmux_thread, consumer_thread;
	int ret_code, enc_proc_id= -1, dec_proc_id= -1, mux_proc_id= -1,
			dmux_proc_id= -1, elem_strem_id_video_server= -1;
	procs_ctx_t *procs_ctx= NULL;
	char *rest_str= NULL, *settings_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
    thr_ctx_t thr_ctx= {0};
    const char *video_settings=
    		"width_output="VIDEO_WIDTH
			"&height_output="VIDEO_HEIGHT;
#define MPEG2_VIDEO
#ifdef MPEG2_VIDEO
    const proc_if_t *proc_if_enc= &proc_if_ffmpeg_m2v_enc;
    const proc_if_t *proc_if_dec= &proc_if_ffmpeg_m2v_dec;
    const char *mime_setting= "sdp_mimetype=video/mp2v";
#endif
#ifdef LHE_VIDEO
    const proc_if_t *proc_if_enc= &proc_if_ffmpeg_mlhe_enc;
    const proc_if_t *proc_if_dec= &proc_if_ffmpeg_mlhe_dec;
    const char *mime_setting= "sdp_mimetype=video/mlhe";
#endif
#ifdef X264_VIDEO
    const proc_if_t *proc_if_enc= &proc_if_ffmpeg_x264_enc;
    const proc_if_t *proc_if_dec= &proc_if_ffmpeg_x264_dec;
    const char *mime_setting= "sdp_mimetype=video/avc1";
#endif
    const proc_if_t *proc_if_mux= &proc_if_live555_rtsp_mux;
    const proc_if_t *proc_if_dmux= &proc_if_live555_rtsp_dmux;

	/* Set SIGNAL handlers to this process */
	sigfillset(&set);
	sigdelset(&set, SIGINT);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	signal(SIGINT, stream_proc_quit_signal_handler);

    /* Open LOG module */
    log_module_open();

	/* Register all FFmpeg's CODECS */
	avcodec_register_all();

	/* Open processors (PROCS) module */
	if(procs_module_open(NULL)!= STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

	/* Register encoders, decoders, RTSP multiplexer and RTSP de-multiplexer
	 * processor types.
	 */
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_ffmpeg_m2v_enc)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_ffmpeg_m2v_dec)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_ffmpeg_mlhe_enc)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_ffmpeg_mlhe_dec)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_ffmpeg_x264_enc)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_ffmpeg_x264_dec)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_live555_rtsp_mux)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if(procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_live555_rtsp_dmux)!=
			STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

	/* Get PROCS module's instance */
	if((procs_ctx= procs_open(NULL, 16, NULL, NULL))== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

    /* Register an encoder instance and get corresponding processor Id. */
	procs_post(procs_ctx, proc_if_enc->proc_name, video_settings, &enc_proc_id);

    /* Register a decoder instance and get corresponding processor Id. */
	procs_post(procs_ctx, proc_if_dec->proc_name, "", &dec_proc_id);

    /* Register RTSP multiplexer instance and get corresponding Id. */
	procs_post(procs_ctx, proc_if_mux->proc_name, "rtsp_port=8574",
			&mux_proc_id);
	/* Register an elementary stream for the multiplexer */
	ret_code= procs_opt(procs_ctx, "PROCS_ID_ES_MUX_REGISTER", mux_proc_id,
			mime_setting, &rest_str);
	if(ret_code!= STAT_SUCCESS || rest_str== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((cjson_rest= cJSON_Parse(rest_str))== NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((cjson_aux= cJSON_GetObjectItem(cjson_rest, "elementary_stream_id"))==
			NULL) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	if((elem_strem_id_video_server= cjson_aux->valuedouble)< 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	free(rest_str); rest_str= NULL;
	cJSON_Delete(cjson_rest); cjson_rest= NULL;

    /* Register RTSP de-multiplexer instance and get corresponding Id. */
	procs_post(procs_ctx, proc_if_dmux->proc_name,
			"rtsp_url=rtsp://127.0.0.1:8574/session", &dmux_proc_id);

    /* Launch producer, encoding-multiplexing, de-multiplexing-decoder,
     * consumer (rendering) and HTTP-sever threads.
     */
    thr_ctx.flag_exit= 0;
    thr_ctx.enc_proc_id= enc_proc_id;
    thr_ctx.dec_proc_id= dec_proc_id;
    thr_ctx.mux_proc_id= mux_proc_id;
    thr_ctx.elem_strem_id_video_server= elem_strem_id_video_server;
    thr_ctx.mime_setting_video= mime_setting;
    thr_ctx.dmux_proc_id= dmux_proc_id;
    thr_ctx.procs_ctx= procs_ctx;
	ret_code= pthread_create(&producer_thread, NULL, producer_thr_video,
			&thr_ctx);
	if(ret_code!= 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	ret_code= pthread_create(&mux_thread, NULL, mux_thr, &thr_ctx);
	if(ret_code!= 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	ret_code= pthread_create(&dmux_thread, NULL, dmux_thr, &thr_ctx);
	if(ret_code!= 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	ret_code= pthread_create(&consumer_thread, NULL, consumer_thr_video,
			&thr_ctx);
	if(ret_code!= 0) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

	/* Server main processing loop */
	printf("Starting server...\n"); fflush(stdout);
	http_server_thr(&thr_ctx);

    /* Join the threads */
    thr_ctx.flag_exit= 1;
	ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE",
			enc_proc_id); // before joining to unblock processor
	if(ret_code!= STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", dec_proc_id);
	if(ret_code!= STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", dmux_proc_id);
	if(ret_code!= STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}
	ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", mux_proc_id);
	if(ret_code!= STAT_SUCCESS) {
		fprintf(stderr, "Error at line: %d\n", __LINE__);
		exit(-1);
	}

	pthread_join(producer_thread, NULL);
	pthread_join(mux_thread, NULL);
	pthread_join(dmux_thread, NULL);
	pthread_join(consumer_thread, NULL);

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
	return 0;
}
