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
 * @file proc_if.h
 * @brief PROC interface prototype related definitions and functions.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORS_SRC_PROC_IF_H_
#define MEDIAPROCESSORS_SRC_PROC_IF_H_

#include <sys/types.h>
#include <inttypes.h>
#include <stdarg.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_ctx_s proc_ctx_t;
typedef struct proc_if_s proc_if_t;
typedef struct log_ctx_s log_ctx_t;
typedef struct fifo_ctx_s fifo_ctx_t;

/**
 * Maximum width for the input/output processor frame.
 */
#define PROC_FRAME_MAX_WIDTH 	4096
/**
 * Maximum height for the input/output processor frame.
 */
#define PROC_FRAME_MAX_HEIGHT 	4096

/**
 * Processor samples format types (enumeration of supported formats).
 */
typedef enum proc_sample_fmt_enum {
	PROC_IF_FMT_UNDEF= 0,   ///< Undefined format
	PROC_IF_FMT_YUV420P, 	///< Planar YUV 4:2:0 with 12bpp (video)
	//PROC_IF_FMT_RGB24, 		///< Packed RGB 8:8:8 with 24bpp (video) //Reserved for future use
	PROC_IF_FMT_S16, 		///< Interleaved signed 16 bits (typically audio)
	PROC_IF_FMT_S16P, 		///< Planar signed 16 bits (typically audio)
} proc_sample_fmt_t;

/**
 * Processor samples format look-up table entry.
 * Each entry of the look-up table consists in an identifier and a
 * descriptive text.
 */
typedef struct proc_sample_fmt_lut_s {
	int id;
	char desc[256];
} proc_sample_fmt_lut_t;

/**
 * Input/output frame context structure.
 * This aims to be a generic input/output frame structure for any kind of
 * processing (e.g. video, audio, subtitles, data, ...). Not all the fields
 * will be necessarily used by all the processors, in fact, most of the
 * processors will only use a subset of these fields.
 */
typedef struct proc_frame_ctx_s {
#define PROC_FRAME_NUM_DATA_POINTERS 8
	/**
	 * Pointer to data buffer. Data may be organized in one or more
	 * "data planes".
	 */
	uint8_t *data;
	/**
	 * Pointers to data planes. These pointers refer to a position
	 * within the data buffer.
	 * For example, in the case of raw video YUV planar formatted data,
	 * three planes may be used corresponding to the luminance and the two
	 * chrominances components respectively.
	 * In the case of non-planar raw data or compressed/encoded data, only
	 * the first plane may be used.
	 */
	const uint8_t *p_data[PROC_FRAME_NUM_DATA_POINTERS];
	/**
	 * Number of bytes per line for each plane. This is not the number of
	 * actual data bytes per line, but the stride in byte units
	 * (stride bytes >= data bytes per line).
	 * For example, in the case of raw video YUV planar formatted data, each
	 * line-size refer to the buffer line stride applicable to each data plane.
	 * In the case of non-planar raw data or compressed/encoded data, only one
	 * entry may be used and would specify the buffer size in bytes
	 * (note that buffer size >= actual data size)
	 * The number of actual data bytes per line is given by 'width' parameter
	 * for the corresponding plane.
	 */
	int linesize[PROC_FRAME_NUM_DATA_POINTERS];
	/**
	 * Frame width.
	 * In the case of audio, compressed/encoded data, or any 1-dimensional
	 * data, this value is used to specify the actual data size in bytes.
	 */
	size_t width[PROC_FRAME_NUM_DATA_POINTERS];
	/**
	 * Frame height.
	 * In the case of audio, compressed/encoded data, or any 1-dimensional
	 * data, this value should be set to 1.
	 */
	size_t height[PROC_FRAME_NUM_DATA_POINTERS];
	/**
	 * Processor samples format identifier.
	 * Type proc_sample_fmt_t enumerates the supported formats
	 * (table proc_sample_fmt_lut can also be used if literal description is
	 * preferred).
	 * Typically not used in the case of compressed/encoded data.
	 */
	int proc_sample_fmt;
	/**
	 * Processor data sampling rate.
	 * For example, for video, this value approximately represent the
	 * frames-per-second (FPS). For audio, the sampling-rate value in units
	 * of Hz. (e.g. 44100, ...)
	 */
	int proc_sampling_rate;
	/**
	 * Presentation time-stamp, in microseconds.
	 */
	int64_t pts;
	/**
	 * Decoding time-stamp, in microseconds.
	 * Not used in the case of raw data (only used at encoder output/decoder
	 * input, by encoders that use internal frame reordering).
	 */
	int64_t dts;
	/**
	 * Elementary stream identifier.
	 * Used, for example, in mutiplexion / demultiplexion.
	 */
	int es_id;
} proc_frame_ctx_t;

/**
 * Processor REST-response (in GET operation) format enumerator.
 * This definitions are used in the function callback 'proc_if_s::rest_get()'
 * to indicate the format desired of the response.
 */
typedef enum proc_if_rest_fmt_enum {
	PROC_IF_REST_FMT_CHAR,   ///< Character string response
	PROC_IF_REST_FMT_CJSON, ///< cJSON structure response
	PROC_IF_REST_FMT_ENUM_MAX
} proc_if_rest_fmt_t;

/**
 * PROC interface structure prototype.
 * Each PROC type will define a static and unambiguous interface of this
 * type.
 */
typedef struct proc_if_s {
	/**
	 * Unambiguous PROC identifier name (character string).
	 */
	const char *proc_name;
	/**
	 * Processor type: encoder, decoder, multiplexer, demultiplexer.
	 */
	const char *proc_type;
	/**
	 * Media type and sub-type (formerly known as MIME types and sub-type).
	 * See http://www.iana.org/assignments/media-types/media-types.xhtml.
	 * For muxers we use by default: "application/octet-stream"
	 */
	const char *proc_mime;
	/**
	 * Processor features flags.
	 */
	uint64_t flag_proc_features;
#define PROC_FEATURE_RD 1 //< Readable (implements 'proc_recv_frame()')
#define PROC_FEATURE_WR 2 //< Writable (implements 'proc_send_frame()')
#define PROC_FEATURE_IOSTATS 4 //< Implements input/output statistics.
#define PROC_FEATURE_IPUT_PTS 8 //< Implements input PTS statistics
#define PROC_FEATURE_LATSTATS 16 //< Implements latency statistics.
	/**
	 * Allocates specific processor (PROC) context structure, initializes,
	 * and launches processing thread.
	 * This callback is mandatory (cannot be NULL).
	 * @param proc_if Pointer to the processor interface structure (static
	 * and unambiguous interface of the type of processor we are opening).
	 * @param settings_str Character string containing initial settings for
	 * the processor. String format can be either a query-string or JSON.
	 * @param log_ctx Pointer to the LOG module context structure.
	 * @param arg Variable list of parameters defined by user.
	 * @return Pointer to the processor context structure on success, NULL if
	 * fails.
	 */
	proc_ctx_t* (*open)(const proc_if_t *proc_if, const char *settings_str,
			log_ctx_t *log_ctx, va_list arg);
	/**
	 * Ends processing thread, de-initialize and release the processor (PROC)
	 * context structure and all the related resources.
	 * This callback is mandatory (cannot be NULL).
	 * @param ref_proc_ctx Reference to the pointer to the processor (PROC)
	 * context structure to be release, that was obtained in a previous call
	 * to the 'open()' callback method. Pointer is set to NULL on return.
	 */
	void (*close)(proc_ctx_t **ref_proc_ctx);
	/**
	 * Put new processor (PROC) settings. Parameters can be passed either as a
	 * query-string or JSON.
	 * This method is asynchronous and thread safe.
	 * This callback is optional (can be set to NULL).
	 * @param proc_ctx Pointer to the processor (PROC) context structure
	 * obtained in a previous call to the 'open()' callback method.
	 * @param str Pointer to a character string containing new settings for
	 * the processor. String format can be either a query-string or JSON.
	 * @return Status code (STAT_SUCCESS code in case of success, for other
	 * code values please refer to .stat_codes.h).
	 */
	int (*rest_put)(proc_ctx_t *proc_ctx, const char *str);
	/**
	 * Get current processor (PROC) settings. Settings are returned by
	 * argument in a character string in JSON format.
	 * This method is asynchronous and thread safe.
	 * This callback is optional (can be set to NULL).
	 * @param proc_ctx Pointer to the processor (PROC) context structure
	 * obtained in a previous call to the 'open()' callback method.
	 * @param rest_fmt Indicates the format in which the response data is to
	 * be returned. Available formats are enumerated at 'proc_if_rest_fmt_t'.
	 * @param ref_reponse Reference to the pointer to a data structure
	 * returning the processor's representational state (including current
	 * settings). The returned data structure is formatted according to what is
	 * indicated in the parameter 'rest_fmt'.
	 * @return Status code (STAT_SUCCESS code in case of success, for other
	 * code values please refer to .stat_codes.h).
	 */
	int (*rest_get)(proc_ctx_t *proc_ctx, const proc_if_rest_fmt_t rest_fmt,
			void **ref_reponse);
	/**
	 * Process one frame of data. The frame is read from the input FIFO buffer
	 * and is completely processed. If an output frame is produced, is written
	 * to the output FIFO buffer.
	 * This callback is mandatory (cannot be NULL).
	 * @param proc_ctx Pointer to the processor (PROC) context structure
	 * obtained in a previous call to the 'open()' callback method.
	 * @param fifo_ctx_iput Pointer to the input FIFO buffer context structure.
	 * @param fifo_ctx_oput Pointer to the output FIFO buffer context structure.
	 * @return Status code (STAT_SUCCESS code in case of success, for other
	 * code values please refer to .stat_codes.h).
	 */
	int (*process_frame)(proc_ctx_t *proc_ctx, fifo_ctx_t *fifo_ctx_iput,
			fifo_ctx_t *fifo_ctx_oput);
	/**
	 * Request for specific processor options.
	 * This callback is optional (can be set to NULL).
	 * @param proc_ctx Pointer to the processor (PROC) context structure
	 * obtained in a previous call to the 'open()' callback method.
	 * @param tag Processor option tag, namely, option identifier string.
	 * Refer to the specific implementation of this function to see the
	 * available options.
	 * arg Variable list of parameters according to selected option. Refer to
	 * the specific implementation of this function to see the parameters
	 * corresponding to each available option.
	 * @return Status code (STAT_SUCCESS code in case of success, for other
	 * code values please refer to .stat_codes.h).
	 */
	int (*opt)(proc_ctx_t *proc_ctx, const char *tag, va_list arg);
	/**
	 * This callback is registered in the FIFO management API and is used to
	 * internally duplicate the input frame structure when it is pushed to the
	 * processor input FIFO. Typically, this callback is used to transform the
	 * input type proc_frame_ctx_t to another structure type that will be
	 * actually allocated in the FIFO.
	 * This callback is optional, and can be set to NULL. In that case, the
	 * processor input frame structure is assumed to be of type
	 * proc_frame_ctx_t (thus will be duplicated using function
	 * 'proc_frame_ctx_allocate()').
	 * @param proc_frame_ctx Processor frame context structure (see
	 * proc_frame_ctx_t).
	 * return Opaque structure (can be private format) duplicating the input
	 * frame. This structure will be pushed to the processor input FIFO.
	 */
	void* (*iput_fifo_elem_opaque_dup)(const proc_frame_ctx_t* proc_frame_ctx);
	/**
	 * This callback is registered in the FIFO management API and is
	 * complementary to the callback 'iput_fifo_elem_opaque_dup()': it is the
	 * function used to release the elements duplicated and stored in the
	 * processor input FIFO using 'iput_fifo_elem_opaque_dup()'.
	 * This callback is optional, and can be set to NULL. In that case, the
	 * processor input frame structure is assumed to be of type
	 * proc_frame_ctx_t (thus will be released using function
	 * 'proc_frame_ctx_release()').
	 * @param ref_t Opaque reference to a pointer to the structure type to be
	 * released.
	 */
	void (*iput_fifo_elem_opaque_release)(void **ref_t);
	/**
	 * This callback is registered in the FIFO management API and is used to
	 * internally duplicate the processor's output frame structure when it is
	 * pushed into the processor output FIFO. Typically, this callback is
	 * used to transform the processor's opaque output type to the type
	 * proc_frame_ctx_t that will be actually allocated in the FIFO.
	 * This callback is optional, and can be set to NULL. In that case, the
	 * processor output frame structure is assumed to be of type
	 * proc_frame_ctx_t (thus will be duplicated using function
	 * 'proc_frame_ctx_allocate()').
	 * @param t Opaque processor's output frame structure.
	 * return Processor frame context structure (see proc_frame_ctx_t).
	 * This structure will be pushed to the processor output FIFO.
	 */
	proc_frame_ctx_t* (*oput_fifo_elem_opaque_dup)(const void *t);
} proc_if_t;

/* **** Prototypes **** */

/**
 * Processor samples format look-up table.
 */
extern const proc_sample_fmt_lut_t proc_sample_fmt_lut[];

/**
 * Allocate an uninitialized input/ouput processor frame structure.
 * @return Pointer to the newly allocated input/output processor frame
 * structure; NULL if fails.
 */
proc_frame_ctx_t* proc_frame_ctx_allocate();

/**
 * Duplicate an input/output processor frame structure.
 * @param proc_frame_ctx_arg Pointer to the input/output processor frame
 * structure to be duplicated.
 * @return Pointer to the new allocated replica of the given input structure;
 * NULL if fails.
 */
proc_frame_ctx_t* proc_frame_ctx_dup(
		const proc_frame_ctx_t *proc_frame_ctx_arg);

/**
 * Release an input/output processor frame structure.
 * @param ref_proc_frame_ctx Reference to the pointer to the input/output
 * processor frame structure to be released. Pointer is set to NULL on return.
 */
void proc_frame_ctx_release(proc_frame_ctx_t **ref_proc_frame_ctx);

/**
 * Allocate an uninitialized processor interface context structure.
 * @return Pointer to the newly allocated processor interface context
 * structure; NULL if fails.
 */
proc_if_t* proc_if_allocate();

/**
 * Duplicate a processor interface context structure.
 * @param proc_if_arg Pointer to the processor interface context structure to
 * be duplicated.
 * @return Pointer to a new allocated replica of the given input structure;
 * NULL if fails.
 */
proc_if_t* proc_if_dup(const proc_if_t *proc_if_arg);

/**
 * Compares if given processor interfaces are the equal.
 * @param proc_if1 Pointer to the first processor interface context structure
 * to be compared.
 * @param proc_if2 Pointer to the second processor interface context structure
 * to be compared.
 * @return Value 0 if given contexts are equal, otherwise non-zero value is
 * returned.
 */
int proc_if_cmp(const proc_if_t* proc_if1, const proc_if_t* proc_if2);

/**
 * Release a processor interface context structure.
 * @param Reference to the pointer to the processor interface context structure
 * to be released. Pointer is set to NULL on return.
 */
void proc_if_release(proc_if_t **ref_proc_if);

#endif /* MEDIAPROCESSORS_SRC_PROC_IF_H_ */
