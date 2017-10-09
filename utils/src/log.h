/*
 * Copyright (c) 2017 Rafael Antoniello
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UTILS_SRC_LOG_H_
#define UTILS_SRC_LOG_H_


#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>

/* **** Definitions **** */

/**
 * Force log-traces to use standard out system.
 */
#define LOG_FORCE_USING_STDOUT

/** LOG module level type */
typedef enum {
	LOG_VERBOSE= 0,
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
	LOG_RAW,
	LOG_EVENT,
	LOG_TYPE_MAX
} log_level_t;

/** LOG module verbose levels */
typedef enum log_verbose_level_enum {
	LOG_SILENT= 0,
	LOG_INFORMATIVE,
	LOG_VERBOSE_DBG,
	LOG_VERBOSE_LEVEL_MAX
} log_verbose_level_t;

/**
 * Trace/log maximum line size.
 */
#define LOG_LINE_SIZE 1024
#define LOG_DATE_SIZE 64

/**
 * Maximum size allowed for log-trace list.
 */
#define LOG_BUF_LINES_NUM 15

typedef struct log_line_ctx_s {
	char code[LOG_LINE_SIZE];
	char desc[LOG_LINE_SIZE];
	char date[LOG_DATE_SIZE];
	uint64_t ts; // Monotonic time-stamp in seconds (for the sake of comparison)
	uint64_t count; // zero means not initialized / not valid line
} log_line_ctx_t;

/* Forward declarations */
typedef struct log_ctx_s log_ctx_t;
typedef struct llist_s llist_t;

/** Source code file-name without path */
#define __FILENAME__ strrchr("/" __FILE__, '/') + 1

#ifdef LOG_CTX_DEFULT // To be defined specifically in source files
	#define _LOG(TYPE, FORMAT, ...) \
		log_trace(TYPE, NULL, __FILENAME__, __LINE__, FORMAT, ##__VA_ARGS__)
#else
	#define LOG_CTX_INIT(CTX) \
		log_ctx_t *__log_ctx= CTX
	#define LOG_CTX_SET(CTX) \
		__log_ctx= CTX
	#define LOG_CTX_GET() __log_ctx
	#define _LOG(TYPE, FORMAT, ...) \
		log_trace(TYPE, __log_ctx, __FILENAME__, __LINE__, FORMAT, \
				##__VA_ARGS__)
#endif

#define LOG(FORMAT, ...)  _LOG(LOG_RAW, FORMAT, ##__VA_ARGS__)
#define LOGV(FORMAT, ...) _LOG(LOG_VERBOSE, FORMAT, ##__VA_ARGS__)
#define LOGW(FORMAT, ...) _LOG(LOG_WARNING, FORMAT, ##__VA_ARGS__)
#define LOGE(FORMAT, ...) _LOG(LOG_ERROR, FORMAT, ##__VA_ARGS__)
#define LOGEV(FORMAT, ...) _LOG(LOG_EVENT, FORMAT, ##__VA_ARGS__)

/* **** Prototypes **** */

/**
 * //TODO
 */
int log_module_open();

/**
 * //TODO
 */
void log_module_close();

/**
 * //TODO
 */
log_ctx_t* log_open(int id);

/**
 * //TODO
 */
void log_close(log_ctx_t **ref_log_ctx);

/**
 * //TODO
 */
void log_trace(log_level_t type, log_ctx_t *log_ctx, const char *filename,
		int line, const char *format, ...);

/**
 * //TODO
 */
const llist_t* log_get(log_ctx_t *log_ctx);

/**
 * //TODO
 */
void log_clear(log_ctx_t *log_ctx);

/**
 * //TODO
 */
log_line_ctx_t* log_line_ctx_allocate();

/**
 * //TODO
 */
log_line_ctx_t* log_line_ctx_dup(const log_line_ctx_t* log_line_ctx);

/**
 * //TODO
 */
void log_line_ctx_release(log_line_ctx_t **ref_log_line_ctx);

/**
 * Trace a block of data bytes.
 * @param label Label to show in trace; appears as a "name" above the block.
 * @param file Source file name from which the trace is requested.
 * @param line Source line number in which the trace is requested.
 * @param data Pointer to block of data.
 * @param len Data block length.
 * @param xsize Size of the rows in which the block is divided for tracing.
 */
#define LOG_TRACE_BYTE_TABLE(LABEL, DATA, LEN, XSIZE) \
	log_trace_byte_table(LABEL, __FILENAME__, __LINE__, DATA, LEN, XSIZE);
void log_trace_byte_table(const char *label, const char *file, int line,
		uint8_t *data, size_t len, size_t xsize);

#endif /* UTILS_SRC_LOG_H_ */
