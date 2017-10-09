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
 * @file utests_video_settings.cpp
 * @brief Encoder video setting unit testing.
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

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocs/proc_if.h>
#include "../src/video_settings.h"
}

SUITE(UTESTS_VIDEO_SETTINGS_CTX)
{
	TEST(UTESTS_VIDEO_SETTINGS_CTX_T)
	{
		int ret_code;
		video_settings_enc_ctx_t *video_settings_enc_ctx= NULL;
		video_settings_enc_ctx_t *video_settings_enc_ctx2= NULL;
		std::string settings_cppstr;
		cJSON *cjson_rest= NULL;
		char *rest_response_str= NULL;

		/* Allocate structure */
		video_settings_enc_ctx= video_settings_enc_ctx_allocate();
		CHECK(video_settings_enc_ctx!= NULL);
		if(video_settings_enc_ctx== NULL)
			goto end;
		video_settings_enc_ctx2= video_settings_enc_ctx_allocate();
		CHECK(video_settings_enc_ctx2!= NULL);
		if(video_settings_enc_ctx2== NULL)
			goto end;

		/* Initialize */
		ret_code= video_settings_enc_ctx_init(video_settings_enc_ctx);
		CHECK(ret_code== STAT_SUCCESS);

		/* Copy structure '1' to '2' */
		ret_code= video_settings_enc_ctx_cpy(video_settings_enc_ctx,
				video_settings_enc_ctx2);
		CHECK(ret_code== STAT_SUCCESS);
		CHECK(video_settings_enc_ctx->bit_rate_output==
				video_settings_enc_ctx2->bit_rate_output);
		CHECK(video_settings_enc_ctx->frame_rate_output==
				video_settings_enc_ctx2->frame_rate_output);
		CHECK(video_settings_enc_ctx->width_output==
				video_settings_enc_ctx2->width_output);
		CHECK(video_settings_enc_ctx->height_output==
				video_settings_enc_ctx2->height_output);
		CHECK(video_settings_enc_ctx->gop_size==
				video_settings_enc_ctx2->gop_size);
		CHECK(strncmp(video_settings_enc_ctx->conf_preset,
				video_settings_enc_ctx2->conf_preset, sizeof(
						video_settings_enc_ctx2->conf_preset))== 0);

		/* Put some settings via query string.
		 * NOTE: query string passed already omits '?' character at the
		 * beginning.
		 */
		settings_cppstr= (std::string)"bit_rate_output=1234&"
				"frame_rate_output=60&width_output=720&height_output=576&"
				"gop_size=123&conf_preset=ultrafast";
		ret_code= video_settings_enc_ctx_restful_put(video_settings_enc_ctx,
				settings_cppstr.c_str(), NULL);
		CHECK(ret_code== STAT_SUCCESS);
		CHECK(video_settings_enc_ctx->bit_rate_output== 1234);
		CHECK(video_settings_enc_ctx->frame_rate_output== 60);
		CHECK(video_settings_enc_ctx->width_output== 720);
		CHECK(video_settings_enc_ctx->height_output== 576);
		CHECK(video_settings_enc_ctx->gop_size== 123);
		CHECK(strncmp(video_settings_enc_ctx->conf_preset, "ultrafast",
				sizeof(video_settings_enc_ctx->conf_preset))== 0);

		/* Put settings via JSON */
		settings_cppstr= (std::string)"{"
				"\"bit_rate_output\":4321,"
				"\"frame_rate_output\":61,"
				"\"width_output\":1920,"
				"\"height_output\":1080,"
				"\"gop_size\":321,"
				"\"conf_preset\":\"veryfast\""
				"}";
		ret_code= video_settings_enc_ctx_restful_put(video_settings_enc_ctx,
				settings_cppstr.c_str(), NULL);
		CHECK(ret_code== STAT_SUCCESS);
		CHECK(video_settings_enc_ctx->bit_rate_output== 4321);
		CHECK(video_settings_enc_ctx->frame_rate_output== 61);
		CHECK(video_settings_enc_ctx->width_output== 1920);
		CHECK(video_settings_enc_ctx->height_output== 1080);
		CHECK(video_settings_enc_ctx->gop_size== 321);
		CHECK(strncmp(video_settings_enc_ctx->conf_preset, "veryfast",
				sizeof(video_settings_enc_ctx->conf_preset))== 0);

		/* Get RESTful char string */
		ret_code= video_settings_enc_ctx_restful_get(video_settings_enc_ctx,
				&cjson_rest, NULL);
		CHECK(ret_code== STAT_SUCCESS && cjson_rest!= NULL);
		if(cjson_rest== NULL)
			goto end;

		/* Print cJSON structure data to char string */
		rest_response_str= cJSON_PrintUnformatted(cjson_rest);
		CHECK(rest_response_str!= NULL && strlen(rest_response_str)> 0);
		if(rest_response_str== NULL)
			goto end;
		CHECK(strcmp(settings_cppstr.c_str(), rest_response_str)== 0);

end:
		if(video_settings_enc_ctx!= NULL)
			video_settings_enc_ctx_deinit(video_settings_enc_ctx);
		if(cjson_rest!= NULL)
			cJSON_Delete(cjson_rest);
		if(rest_response_str!= NULL)
			free(rest_response_str);
		video_settings_enc_ctx_release(&video_settings_enc_ctx);
		video_settings_enc_ctx_release(&video_settings_enc_ctx2);
	}
}
