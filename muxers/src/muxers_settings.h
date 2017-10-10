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
 * @file muxers_settings.h
 * @brief Multiplexers and de-multiplexers generic settings.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_MUXERS_SETTINGS_H_
#define MEDIAPROCESSORS_SRC_MUXERS_SETTINGS_H_

#include <libmediaprocsutils/mem_utils.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct log_ctx_s log_ctx_t;
typedef struct cJSON cJSON;

/**
 * Generic multiplexer settings context structure.
 * This structure may be extended by any specific implementation of a
 * multiplexer.
 */
typedef struct muxers_settings_mux_ctx_s {
	/**
	 * RTSP server port
	 */
	int rtsp_port;
	/**
	 * Multiplexer time-stamping frequency in Hz.
	 */
	int64_t time_stamp_freq;
	/**
	 * RTSP streaming session name.
	 */
	char *rtsp_streaming_session_name;
} muxers_settings_mux_ctx_t;

/**
 * Generic de-multiplexer settings context structure.
 * This structure may be extended by any specific implementation of a
 * de-multiplexer.
 */
typedef struct muxers_settings_dmux_ctx_s {
	/**
	 * RTSP listening URL.
	 */
	char *rtsp_url;
} muxers_settings_dmux_ctx_t;

/* **** Prototypes **** */

/**
 * Allocate generic multiplexer settings context structure.
 * @return Pointer to the generic multiplexer settings context structure.
 */
muxers_settings_mux_ctx_t* muxers_settings_mux_ctx_allocate();

/**
 * Release generic multiplexer settings context structure previously
 * allocated by 'muxers_settings_mux_ctx_allocate()'.
 * @param ref_muxers_settings_mux_ctx
 */
void muxers_settings_mux_ctx_release(
		muxers_settings_mux_ctx_t **ref_muxers_settings_mux_ctx);

/**
 * Initialize multiplexer generic settings to defaults.
 * @param muxers_settings_mux_ctx Pointer to the generic multiplexer settings
 * context structure to be initialized.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_mux_ctx_init(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx);

/**
 * De-initialize multiplexer generic settings.
 * This function release any heap-allocated field or structure member.
 * @param muxers_settings_mux_ctx Pointer to the generic multiplexer settings
 * context structure to be de-initialized.
 */
void muxers_settings_mux_ctx_deinit(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx);

/**
 * Copy multiplexer generic settings members, duplicating any existent heap
 * allocation.
 * @param muxers_settings_mux_ctx_src Pointer to the generic multiplexer
 * settings context structure to be copied (namely, the source structure).
 * @param muxers_settings_mux_ctx_dst Pointer to the generic multiplexer
 * settings context structure that holds the copy (namely, the destination
 * structure).
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_mux_ctx_cpy(
		const muxers_settings_mux_ctx_t *muxers_settings_mux_ctx_src,
		muxers_settings_mux_ctx_t *muxers_settings_mux_ctx_dst);

/**
 * Put new settings passed by argument in query-string or JSON format.
 * @param muxers_settings_mux_ctx Pointer to the generic multiplexer settings
 * context structure to be modified.
 * @param str New parameters passed in query-string or JSON format.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_mux_ctx_restful_put(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx,
		const char *str, log_ctx_t *log_ctx);

/**
 * Translate and get the multiplexer generic settings in a cJSON structure.
 * @param muxers_settings_mux_ctx Pointer to the generic multiplexer settings
 * context structure to be translated.
 * @param ref_cjson_rest Reference to a pointer to a cJSON structure in which
 * the translated settings are returned (by argument).
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_mux_ctx_restful_get(
		volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx);

/**
 * Allocate generic de-multiplexer settings context structure.
 * @return Pointer to the generic de-multiplexer settings context structure.
 */
muxers_settings_dmux_ctx_t* muxers_settings_dmux_ctx_allocate();

/**
 * Release generic de-multiplexer settings context structure previously
 * allocated by 'muxers_settings_dmux_ctx_allocate()'.
 * @param ref_muxers_settings_dmux_ctx
 */
void muxers_settings_dmux_ctx_release(
		muxers_settings_dmux_ctx_t **ref_muxers_settings_dmux_ctx);

/**
 * Initialize de-multiplexer generic settings to defaults.
 * @param muxers_settings_dmux_ctx Pointer to the generic de-multiplexer
 * settings context structure to be initialized.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_dmux_ctx_init(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx);

/**
 * De-initialize de-multiplexer generic settings.
 * This function release any heap-allocated field or structure member.
 * @param muxers_settings_dmux_ctx Pointer to the generic de-multiplexer
 * settings context structure to be de-initialized.
 */
void muxers_settings_dmux_ctx_deinit(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx);

/**
 * Copy de-multiplexer generic settings members, duplicating any existent heap
 * allocation.
 * @param muxers_settings_dmux_ctx_src Pointer to the generic de-multiplexer
 * settings context structure to be copied (namely, the source structure).
 * @param muxers_settings_dmux_ctx_dst Pointer to the generic de-multiplexer
 * settings context structure that holds the copy (namely, the destination
 * structure).
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_dmux_ctx_cpy(
		const muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx_src,
		muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx_dst);

/**
 * Put new settings passed by argument in query-string or JSON format.
 * @param muxers_settings_dmux_ctx Pointer to the generic de-multiplexer
 * settings context structure to be modified.
 * @param str New parameters passed in query-string or JSON format.
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_dmux_ctx_restful_put(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx,
		const char *str, log_ctx_t *log_ctx);

/**
 * Translate and get the de-multiplexer generic settings in a cJSON structure.
 * @param muxers_settings_dmux_ctx Pointer to the generic de-multiplexer
 * settings context structure to be translated.
 * @param ref_cjson_rest Reference to a pointer to a cJSON structure in which
 * the translated settings are returned (by argument).
 * @param log_ctx Externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int muxers_settings_dmux_ctx_restful_get(
		volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx,
		cJSON **ref_cjson_rest, log_ctx_t *log_ctx);

#endif /* MEDIAPROCESSORS_SRC_MUXERS_SETTINGS_H_ */
