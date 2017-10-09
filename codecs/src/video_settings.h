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
 * @file video_settings.h
 * @brief Video encoder and decoder generic settings.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_VIDEO_SETTINGS_H_
#define MEDIAPROCESSORS_SRC_VIDEO_SETTINGS_H_

#include <libmediaprocsutils/mem_utils.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct log_ctx_s log_ctx_t;
typedef struct cJSON cJSON;

/**
 * Generic video encoder settings context structure.
 * This structure may be extended by any specific implementation of a
 * video encoder.
 */
typedef struct video_settings_enc_ctx_s {
	/**
	 * Video encoder target output bit-rate [bps].
	 */
	int bit_rate_output;
	/**
	 * Video encoder output frame-rate.
	 */
	int frame_rate_output;
	/**
	 * Video encoder output frame width.
	 */
	int width_output;
	/**
	 * Video encoder output frame height.
	 */
	int height_output;
	/**
	 * Video encoder group of pictures (GOP) size, in picture units.
	 * Set to zero for intra_only.
	 */
	int gop_size;
	/**
	 * Video encoder configuration preset, if applicable.
	 */
	char conf_preset[128];
} video_settings_enc_ctx_t;

/**
 * Generic video decoder settings context structure.
 * This structure may be extended by any specific implementation of a
 * video decoder.
 */
typedef struct video_settings_dec_ctx_s {
	// Reserved for future use
} video_settings_dec_ctx_t;

/* **** Prototypes **** */

/**
 * Allocate generic video encoder settings context structure.
 * @return Pointer to the generic video encoder settings context structure.
 */
video_settings_enc_ctx_t* video_settings_enc_ctx_allocate();

/**
 * Release generic video encoder settings context structure previously
 * allocated by 'video_settings_enc_ctx_allocate()'.
 * @param ref_video_settings_enc_ctx
 */
void video_settings_enc_ctx_release(
		video_settings_enc_ctx_t **ref_video_settings_enc_ctx);

/**
 * Initialize video encoder generic settings to defaults.
 * @param video_settings_enc_ctx Pointer to the generic video encoder settings
 * context structure to be initialized.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_enc_ctx_init(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx);

/**
 * De-initialize video encoder generic settings.
 * This function release any heap-allocated field or structure member.
 * @param video_settings_enc_ctx Pointer to the generic video encoder settings
 * context structure to be de-initialized.
 */
void video_settings_enc_ctx_deinit(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx);

/**
 * Copy video encoder generic settings members, duplicating any existent heap
 * allocation.
 * @param video_settings_enc_ctx_src Pointer to the generic video encoder
 * settings context structure to be copied (namely, the source structure).
 * @param video_settings_enc_ctx_dst Pointer to the generic video encoder
 * settings context structure that holds the copy (namely, the destination
 * structure).
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_enc_ctx_cpy(
		const video_settings_enc_ctx_t *video_settings_enc_ctx_src,
		video_settings_enc_ctx_t *video_settings_enc_ctx_dst);

/**
 * Put new settings passed by argument in query-string or JSON format.
 * @param video_settings_enc_ctx Pointer to the generic video encoder settings
 * context structure to be modified.
 * @param str New parameters passed in query-string or JSON format.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_enc_ctx_restful_put(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx,
		const char *str, log_ctx_t *log_ctx);

/**
 * Translate and get the video encoder generic settings in a cJSON structure.
 * @param video_settings_enc_ctx Pointer to the generic video encoder settings
 * context structure to be translated.
 * @param ref_cjson_rest Reference to a pointer to a cJSON structure in which
 * the translated settings are returned (by argument).
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_enc_ctx_restful_get(
		volatile video_settings_enc_ctx_t *video_settings_enc_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx);

/**
 * Allocate generic video decoder settings context structure.
 * @return Pointer to the generic video decoder settings context structure.
 */
video_settings_dec_ctx_t* video_settings_dec_ctx_allocate();

/**
 * Release generic video decoder settings context structure previously
 * allocated by 'video_settings_dec_ctx_allocate()'.
 * @param ref_video_settings_dec_ctx
 */
void video_settings_dec_ctx_release(
		video_settings_dec_ctx_t **ref_video_settings_dec_ctx);

/**
 * Initialize video decoder generic settings to defaults.
 * @param video_settings_dec_ctx Pointer to the generic video decoder settings
 * context structure to be initialized.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_dec_ctx_init(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx);

/**
 * De-initialize video decoder generic settings.
 * This function release any heap-allocated field or structure member.
 * @param video_settings_dec_ctx Pointer to the generic video decoder settings
 * context structure to be de-initialized.
 */
void video_settings_dec_ctx_deinit(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx);

/**
 * Copy video decoder generic settings members, duplicating any existent heap
 * allocation.
 * @param video_settings_dec_ctx_src Pointer to the generic video decoder
 * settings context structure to be copied (namely, the source structure).
 * @param video_settings_dec_ctx_dst Pointer to the generic video decoder
 * settings context structure that holds the copy (namely, the destination
 * structure).
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_dec_ctx_cpy(
		const video_settings_dec_ctx_t *video_settings_dec_ctx_src,
		video_settings_dec_ctx_t *video_settings_dec_ctx_dst);

/**
 * Put new settings passed by argument in query-string or JSON format.
 * @param video_settings_dec_ctx Pointer to the generic video decoder settings
 * context structure to be modified.
 * @param str New parameters passed in query-string or JSON format.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_dec_ctx_restful_put(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx,
		const char *str, log_ctx_t *log_ctx);

/**
 * Translate and get the video decoder generic settings in a cJSON structure.
 * @param video_settings_dec_ctx Pointer to the generic video decoder settings
 * context structure to be translated.
 * @param ref_cjson_rest Reference to a pointer to a cJSON structure in which
 * the translated settings are returned (by argument).
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int video_settings_dec_ctx_restful_get(
		volatile video_settings_dec_ctx_t *video_settings_dec_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx);

#endif /* MEDIAPROCESSORS_SRC_VIDEO_SETTINGS_H_ */
