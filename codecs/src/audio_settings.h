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
 * @file audio_settings.h
 * @brief Audio encoder and decoder generic settings.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_AUDIO_SETTINGS_H_
#define MEDIAPROCESSORS_SRC_AUDIO_SETTINGS_H_

#include <libmediaprocsutils/mem_utils.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct log_ctx_s log_ctx_t;
typedef struct cJSON cJSON;

/**
 * Generic audio encoder settings context structure.
 * This structure may be extended by any specific implementation of an
 * audio encoder.
 */
typedef struct audio_settings_enc_ctx_s {
	/**
	 * Audio encoder output bit-rate [bps].
	 */
	int bit_rate_output;
	/**
	 * Audio encoder output sample-rate.
	 */
	int sample_rate_output;
} audio_settings_enc_ctx_t;

/**
 * Generic audio decoder settings context structure.
 * This structure may be extended by any specific implementation of a
 * audio decoder.
 */
typedef struct audio_settings_dec_ctx_s {
	/**
	 * Audio decoder output samples format.
	 */
	char *samples_format_output;
} audio_settings_dec_ctx_t;

/* **** Prototypes **** */

/**
 * Allocate generic audio encoder settings context structure.
 * @return Pointer to the generic audio encoder settings context structure.
 */
audio_settings_enc_ctx_t* audio_settings_enc_ctx_allocate();

/**
 * Release generic audio encoder settings context structure previously
 * allocated by 'audio_settings_enc_ctx_allocate()'.
 * @param ref_audio_settings_enc_ctx
 */
void audio_settings_enc_ctx_release(
		audio_settings_enc_ctx_t **ref_audio_settings_enc_ctx);

/**
 * Initialize audio encoder generic settings to defaults.
 * @param audio_settings_enc_ctx Pointer to the generic audio encoder settings
 * context structure to be initialized.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_enc_ctx_init(
		volatile audio_settings_enc_ctx_t *audio_settings_enc_ctx);

/**
 * De-initialize audio encoder generic settings.
 * This function release any heap-allocated field or structure member.
 * @param audio_settings_enc_ctx Pointer to the generic audio encoder settings
 * context structure to be de-initialized.
 */
void audio_settings_enc_ctx_deinit(
		volatile audio_settings_enc_ctx_t *audio_settings_enc_ctx);

/**
 * Copy audio encoder generic settings members, duplicating any existent heap
 * allocation.
 * @param audio_settings_enc_ctx_src Pointer to the generic audio encoder
 * settings context structure to be copied (namely, the source structure).
 * @param audio_settings_enc_ctx_dst Pointer to the generic audio encoder
 * settings context structure that holds the copy (namely, the destination
 * structure).
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_enc_ctx_cpy(
		const audio_settings_enc_ctx_t *audio_settings_enc_ctx_src,
		audio_settings_enc_ctx_t *audio_settings_enc_ctx_dst);

/**
 * Put new settings passed by argument in query-string or JSON format.
 * @param audio_settings_enc_ctx Pointer to the generic audio encoder settings
 * context structure to be modified.
 * @param str New parameters passed in query-string or JSON format.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_enc_ctx_restful_put(
		volatile audio_settings_enc_ctx_t *audio_settings_enc_ctx,
		const char *str, log_ctx_t *log_ctx);

/**
 * Translate and get the audio encoder generic settings in a cJSON structure.
 * @param audio_settings_enc_ctx Pointer to the generic audio encoder settings
 * context structure to be translated.
 * @param ref_cjson_rest Reference to a pointer to a cJSON structure in which
 * the translated settings are returned (by argument).
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_enc_ctx_restful_get(
		volatile audio_settings_enc_ctx_t *audio_settings_enc_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx);

/**
 * Allocate generic audio decoder settings context structure.
 * @return Pointer to the generic audio decoder settings context structure.
 */
audio_settings_dec_ctx_t* audio_settings_dec_ctx_allocate();

/**
 * Release generic audio decoder settings context structure previously
 * allocated by 'audio_settings_dec_ctx_allocate()'.
 * @param ref_audio_settings_dec_ctx
 */
void audio_settings_dec_ctx_release(
		audio_settings_dec_ctx_t **ref_audio_settings_dec_ctx);

/**
 * Initialize audio decoder generic settings to defaults.
 * @param audio_settings_dec_ctx Pointer to the generic audio decoder settings
 * context structure to be initialized.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_dec_ctx_init(
		volatile audio_settings_dec_ctx_t *audio_settings_dec_ctx);

/**
 * De-initialize audio decoder generic settings.
 * This function release any heap-allocated field or structure member.
 * @param audio_settings_dec_ctx Pointer to the generic audio decoder settings
 * context structure to be de-initialized.
 */
void audio_settings_dec_ctx_deinit(
		volatile audio_settings_dec_ctx_t *audio_settings_dec_ctx);

/**
 * Copy audio decoder generic settings members, duplicating any existent heap
 * allocation.
 * @param audio_settings_dec_ctx_src Pointer to the generic audio decoder
 * settings context structure to be copied (namely, the source structure).
 * @param audio_settings_dec_ctx_dst Pointer to the generic audio decoder
 * settings context structure that holds the copy (namely, the destination
 * structure).
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_dec_ctx_cpy(
		const audio_settings_dec_ctx_t *audio_settings_dec_ctx_src,
		audio_settings_dec_ctx_t *audio_settings_dec_ctx_dst);

/**
 * Put new settings passed by argument in query-string or JSON format.
 * @param audio_settings_dec_ctx Pointer to the generic audio decoder settings
 * context structure to be modified.
 * @param str New parameters passed in query-string or JSON format.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_dec_ctx_restful_put(
		volatile audio_settings_dec_ctx_t *audio_settings_dec_ctx,
		const char *str, log_ctx_t *log_ctx);

/**
 * Translate and get the audio decoder generic settings in a cJSON structure.
 * @param audio_settings_dec_ctx Pointer to the generic audio decoder settings
 * context structure to be translated.
 * @param ref_cjson_rest Reference to a pointer to a cJSON structure in which
 * the translated settings are returned (by argument).
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int audio_settings_dec_ctx_restful_get(
		volatile audio_settings_dec_ctx_t *audio_settings_dec_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx);

#endif /* MEDIAPROCESSORS_SRC_AUDIO_SETTINGS_H_ */
