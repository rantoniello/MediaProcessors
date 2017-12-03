/*
 * Copyright (c) 2017, 2018 Rafael Antoniello
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


#include "log.h"

#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>

#include "stat_codes.h"
#include "check_utils.h"
#include "llist.h"

/* **** Definitions **** */

/**
 * LOG-file maximum size in bytes.
 */
#define LOG_MAX_FILESIZE (256* 1024)

#ifndef _INSTALL_DIR //HACK
#define _INSTALL_DIR "./"
#endif
#ifndef _PROCNAME //HACK
#define _PROCNAME "stdout"
#endif
#define LOG_FILEPATH 	_INSTALL_DIR"/var/log/"_PROCNAME
#define LOG_FILE 		LOG_FILEPATH"/"_PROCNAME".log"
#define LOG_FILE_OLD 	LOG_FILEPATH"/"_PROCNAME".old.log"

/** Highlight log type with colors or prefix code / strings */
#ifdef LOG_FORCE_USING_STDOUT
	/** Color codes for terminal */
	#define CNRM "\x1B[0m"
	#define CRED "\033[1;31m" // Bold-red
	#define CGRN "\x1B[32m"
	#define CYEL "\x1B[33m"
	#define CMAG "\x1B[35m"
	#define CBLU "\x1B[34m"

	#define LOG_VERBOSE_HIGHLIGHT 	CNRM
	#define LOG_DEBUG_HIGHLIGHT 	CNRM
	#define LOG_WARNING_HIGHLIGHT 	CYEL
	#define LOG_ERROR_HIGHLIGHT 	CRED
	#define LOG_RAW_HIGHLIGHT 		CNRM
	#define LOG_EVENT_HIGHLIGHT 	CGRN

	#define OPEN_FILE() \
		fileno(stdout)
	#define CLOSE_FILE(FD)
	#define UPDATE_USE_STDOUT_FLAG() \
		flag_use_stdout= 1
#else
	#define LOG_VERBOSE_HIGHLIGHT 	"VERBOSE: "
	#define LOG_DEBUG_HIGHLIGHT 	"DEBUG:   "
	#define LOG_WARNING_HIGHLIGHT 	"WARNING: "
	#define LOG_ERROR_HIGHLIGHT 	"ERROR:   "
	#define LOG_RAW_HIGHLIGHT 		""
	#define LOG_EVENT_HIGHLIGHT 	"EVENT: "

	#define OPEN_FILE() \
		open(LOG_FILE, O_RDWR| O_APPEND| O_TRUNC| O_CREAT, S_IRUSR| S_IWUSR)
	#define CLOSE_FILE(FD) \
		close(FD)
	#define UPDATE_USE_STDOUT_FLAG() \
		flag_use_stdout= 0
#endif

/** LOG module instance context structure */
typedef struct log_ctx_s {
	/**
	 * Private identifier.
	 * This integer is written as a "label" in the log trace, and can be used
	 * to identify a specific module, application block, name-space, etc.
	 */
	int id;
	/**
	 * LOG instance mutual-exclusion lock
	 */
	pthread_mutex_t mutex;
	/**
	 * LOG-traces list.
	 */
	llist_t *log_line_ctx_llist;
	/**
	 * Reference to the pointer to the last object of the LOG-traces list
	 * (reference to the tail of the list).
	 */
	llist_t **log_line_ctx_llist_tail_ref;
	/**
	 * Reference to the pointer to the oldest object of the LOG-traces list.
	 */
	llist_t **log_line_ctx_llist_oldest_ref;
	/**
	 * LOG-traces list length.
	 */
	int log_line_ctx_llist_len;
} log_ctx_t;

/* **** Prototypes **** */

static void log_trace_fd(log_level_t type, const char *filename, int line,
		const char *format, va_list arg);
static void log_trace_buf(log_level_t type, log_ctx_t *log_ctx,
		const char *filename, int line, const char *format, va_list arg);
static void log_module_fflush();

/* **** Implementations **** */

/** Module variables */
static int logfile_fd= -1;
static pthread_mutex_t logfile_mutex= PTHREAD_MUTEX_INITIALIZER;
static int flag_use_stdout= 0;

int log_module_open()
{
	int ret_code, end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Open log-file */
	logfile_fd= OPEN_FILE();
	if(logfile_fd< 0) {
		printf("Could not open LOG file '%s'\n", LOG_FILE);
		goto end;
	}

	/* Initialize module MUTEX for log-file */
	ret_code= pthread_mutex_init(&logfile_mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);

	/* Update "use standard out" flag */
	UPDATE_USE_STDOUT_FLAG();

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		log_module_close();
	return end_code;
}

void log_module_close()
{
	int ret_code;
	LOG_CTX_INIT(NULL);

	/* Close log-file */
	CLOSE_FILE(logfile_fd);
	logfile_fd= -1;

	/* Release module MUTEX for log-file */
	ret_code= pthread_mutex_destroy(&logfile_mutex);
	ASSERT(ret_code== 0);

	/* Erase "use standard out" flag */
	flag_use_stdout= 0;
}

log_ctx_t* log_open(int id)
{
	int ret_code, end_code= STAT_ERROR;
	log_ctx_t *log_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Allocate LOG module instance context structure */
	log_ctx= (log_ctx_t*)calloc(1, sizeof(log_ctx_t));
	CHECK_DO(log_ctx!= NULL, goto end);

	/* Initialize context structure */
	log_ctx->id= id;
	ret_code= pthread_mutex_init(&log_ctx->mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);
	log_ctx->log_line_ctx_llist= NULL;
	log_ctx->log_line_ctx_llist_tail_ref= &log_ctx->log_line_ctx_llist;
	log_ctx->log_line_ctx_llist_oldest_ref= &log_ctx->log_line_ctx_llist;
	log_ctx->log_line_ctx_llist_len= 0;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		log_close(&log_ctx);
	return log_ctx;
}

void log_close(log_ctx_t **ref_log_ctx)
{
	log_ctx_t *log_ctx;

	if(ref_log_ctx== NULL)
		return;

	if((log_ctx= *ref_log_ctx)!= NULL) {
		int ret_code;
		LOG_CTX_INIT(NULL);

		/* Release instance MUTEX */
		ret_code= pthread_mutex_destroy(&log_ctx->mutex);
		ASSERT(ret_code== 0);

		/* Release list of log-line context structures */
	    while(log_ctx->log_line_ctx_llist!= NULL) {
	    	log_line_ctx_t *log_line_ctx= llist_pop(
	    			&log_ctx->log_line_ctx_llist);
	    	log_line_ctx_release(&log_line_ctx);
	    }

		free(log_ctx);
		*ref_log_ctx= NULL;
	}
}

void log_trace(log_level_t type, log_ctx_t *log_ctx, const char *filename,
		int line, const char *format, ...)
{
	va_list arg, arg_cpy;

	/* Check arguments */
	if(type>= LOG_TYPE_MAX) return;
	if(format== NULL) return;

	/* Check module initialization: file-descriptor for file or STDOUT */
	if(logfile_fd< 0) {
		printf("'LOG' module should be initialized previously\n");
		return;
	}

	va_start(arg, format);
	va_copy(arg_cpy, arg);

	if(flag_use_stdout== 0 && log_ctx!= NULL) {
		/* Print to instance-specific trace function */
		log_trace_buf(type, log_ctx, filename, line, format, arg_cpy);
	} else {
		log_trace_fd(type, filename, line, format, arg_cpy);
	}

	va_end(arg);
	return;
}

const llist_t* log_get(log_ctx_t *log_ctx)
{
	llist_t *curr_node, *prev_node;
	llist_t *log_line_ctx_llist= NULL; //LOG-traces list replica to be returned

	/* Check arguments */
	if(log_ctx== NULL)
		return NULL;

	pthread_mutex_lock(&log_ctx->mutex);

	/* Duplicate LOG-traces list */
	curr_node= log_ctx->log_line_ctx_llist;
	prev_node= NULL;
	while(curr_node!= NULL) {
		log_line_ctx_t *log_line_ctx2;
		llist_t *new_node;

		/* Get a copy of LOG-line context structure */
		log_line_ctx2= log_line_ctx_dup((const log_line_ctx_t*)curr_node->data);
		if(log_line_ctx2== NULL)
			continue;

		/* Push copy of LOG-line structure to list replica */
		new_node= (llist_t*)malloc(sizeof(llist_t));
		if(new_node== NULL)
			continue;
		new_node->data= (void*)log_line_ctx2;
		new_node->next= NULL;

		/* Link current node to previous node if applicable */
		if(prev_node!= NULL)
			prev_node->next= new_node;
		else
			log_line_ctx_llist= new_node;

		/* Update for next iteration */
		prev_node= new_node;
		curr_node= curr_node->next;
	}

	pthread_mutex_unlock(&log_ctx->mutex);
	return log_line_ctx_llist;
}

void log_clear(log_ctx_t *log_ctx)
{
	/* Check arguments */
	if(log_ctx== NULL)
		return;

	pthread_mutex_lock(&log_ctx->mutex);

	/* Release list of log-line context structures */
    while(log_ctx->log_line_ctx_llist!= NULL) {
    	log_line_ctx_t *log_line_ctx= llist_pop(
    			&log_ctx->log_line_ctx_llist);
    	log_line_ctx_release(&log_line_ctx);
    }
	log_ctx->log_line_ctx_llist_tail_ref= &log_ctx->log_line_ctx_llist;
	log_ctx->log_line_ctx_llist_oldest_ref= &log_ctx->log_line_ctx_llist;
	log_ctx->log_line_ctx_llist_len= 0;

	pthread_mutex_unlock(&log_ctx->mutex);
}

log_line_ctx_t* log_line_ctx_allocate()
{
	return (log_line_ctx_t*)calloc(1, sizeof(log_line_ctx_t));
}

log_line_ctx_t* log_line_ctx_dup(const log_line_ctx_t* log_line_ctx_arg)
{
	log_line_ctx_t *log_line_ctx= NULL;

	/* Check arguments */
	if(log_line_ctx_arg== NULL)
		return NULL;

	/* Allocate LOG-line context structure */
	log_line_ctx= log_line_ctx_allocate();
	if(log_line_ctx== NULL)
		return NULL;

	/* Copy structure */
	strncpy(log_line_ctx->code, log_line_ctx_arg->code, LOG_LINE_SIZE);
	strncpy(log_line_ctx->desc, log_line_ctx_arg->desc, LOG_LINE_SIZE);
	strncpy(log_line_ctx->date, log_line_ctx_arg->date, LOG_DATE_SIZE);
	log_line_ctx->ts= log_line_ctx_arg->ts;
	log_line_ctx->count= log_line_ctx_arg->count;

	return log_line_ctx;
}

void log_line_ctx_release(log_line_ctx_t **ref_log_line_ctx)
{
	log_line_ctx_t *log_line_ctx;

	if(ref_log_line_ctx== NULL)
		return;

	if((log_line_ctx= *ref_log_line_ctx)!= NULL) {
		free(log_line_ctx);
		*ref_log_line_ctx= NULL;
	}
}

void log_trace_byte_table(const char *label, const char *file, int line,
		uint8_t *data, size_t len, size_t xsize)
{
	int i, j;
	uint8_t *p= data;

	/* Check arguments */
	if(file== NULL || data== NULL) return;
	if(xsize& 3) {
		log_trace(LOG_ERROR, NULL, __FILENAME__, __LINE__,
				"Parameter 'xsize' MUST be a multiple of 4.\n");
		return;
	}

	log_trace(LOG_RAW, NULL, __FILENAME__, __LINE__,
			"%s %d: \n> ======== %s ========\n", file, line,
			label!= NULL? label: "");
	for(i= 0; i< len; i+= xsize) {
		log_trace(LOG_RAW, NULL, __FILENAME__, __LINE__, "> ");
		for(j= 0; j< xsize; j++) {
			if(i+ j>= len)
				break;
			log_trace(LOG_RAW, NULL, __FILENAME__, __LINE__, "%02x", p[i+j]);
			if((j& 3)== 0)
				log_trace(LOG_RAW, NULL, __FILENAME__, __LINE__, " ");
		}
		log_trace(LOG_RAW, NULL, __FILENAME__, __LINE__, "\n");
	}
	log_trace(LOG_RAW, NULL, __FILENAME__, __LINE__, ">\n\n");
	log_module_fflush();
}

static void log_trace_fd(log_level_t type, const char *filename, int line,
		const char *format, va_list arg)
{
	char str[LOG_LINE_SIZE], *highlight_prefix;
	size_t str_size= 0;
	ssize_t written= -1;

	pthread_mutex_lock(&logfile_mutex);

	/* Print color code to terminal or prefix for LOG-file */
	switch(type) {
	case LOG_VERBOSE:
		highlight_prefix= LOG_VERBOSE_HIGHLIGHT;
		break;
	case LOG_DEBUG:
		highlight_prefix= LOG_DEBUG_HIGHLIGHT;
		break;
	case LOG_WARNING:
		highlight_prefix= LOG_WARNING_HIGHLIGHT;
		break;
	case LOG_ERROR:
		highlight_prefix= LOG_ERROR_HIGHLIGHT;
		break;
	case LOG_RAW:
		highlight_prefix= LOG_RAW_HIGHLIGHT;
		break;
	case LOG_EVENT:
		highlight_prefix= LOG_EVENT_HIGHLIGHT;
		break;
	default:
		highlight_prefix= "";
		break;
	}
	str_size+= snprintf(&str[str_size], sizeof(str)- str_size, "%s",
			highlight_prefix);

	/* Print source-code file-name and file-line */
	if(filename!= NULL && type!= LOG_RAW) {
		if(str_size>= sizeof(str)) goto end;
		str_size+= snprintf(&str[str_size], sizeof(str)- str_size, "%s %d ",
				filename, line);
	}

	/* Print rest of the formatted string */
	if(str_size>= sizeof(str)) goto end;
	str_size+= vsnprintf(&str[str_size], sizeof(str)- str_size, format, arg);

	/* Write (create/truncate) to LOG-file */
	if(str_size>= sizeof(str)) str_size= sizeof(str);
	written= write(logfile_fd, str, str_size);
	// Hack just to ignore compilation warnings
	if(written!= str_size) written= -1;

    /* Flush file traces */
	log_module_fflush();

end:
	pthread_mutex_unlock(&logfile_mutex);
	va_end(arg);
	return;
}

static void log_trace_buf(log_level_t type, log_ctx_t *log_ctx,
		const char *filename, int line, const char *format, va_list arg)
{
	char code[LOG_LINE_SIZE], *extp;
	llist_t **ref_curr_node= NULL;
	log_line_ctx_t *new_log_line_ctx= NULL;
	llist_t *new_node= NULL;
	uint64_t oldest_ts= 0;
	struct timespec monotime_curr= {0};
	time_t t= time(NULL);
	struct tm *tm= localtime(&t);

	/* Check arguments */
	if(log_ctx== NULL)
		return;

	/* Get current time */
    if(clock_gettime(CLOCK_MONOTONIC, &monotime_curr)!= 0)
    	goto end;

	/* Print log-line */
	snprintf(code, LOG_LINE_SIZE, "%d%s", line, filename);
	extp= strstr(code, ".c");
	if(extp!= NULL)
		*extp= 0; // erase extension '.c*' from code nomenclature

	pthread_mutex_lock(&log_ctx->mutex);

	/* Check if this specific log trace (identified by unambiguous code) is
	 * available in log-list.
	 * Recycle list if code is already listed; otherwise add new trace.
	 */
	ref_curr_node= &log_ctx->log_line_ctx_llist;
	log_ctx->log_line_ctx_llist_oldest_ref= &log_ctx->log_line_ctx_llist;
	while(*ref_curr_node!= NULL) {
		uint64_t ts_ith;
		log_line_ctx_t *log_line_ctx_ith;
		llist_t *curr_node= *ref_curr_node;

		/* Get node data */
		log_line_ctx_ith= (log_line_ctx_t*)curr_node->data;
		if(log_line_ctx_ith== NULL)
			continue;

		/* Check if log-trace code is available */
		if(strncmp(code, log_line_ctx_ith->code, strlen(code))== 0) {
			/* Update node data */
			vsnprintf(log_line_ctx_ith->desc, LOG_LINE_SIZE, format, arg);
			log_line_ctx_ith->count++;
			log_line_ctx_ith->ts= (uint64_t)monotime_curr.tv_sec;
			if(tm!= NULL) {
				snprintf(log_line_ctx_ith->date, LOG_DATE_SIZE,
						"%d:%d:%d %d-%d-%d", tm->tm_hour, tm->tm_min,
						tm->tm_sec, tm->tm_year + 1900, tm->tm_mon + 1,
						tm->tm_mday);
			} else {
				memset(log_line_ctx_ith->date, 0, LOG_DATE_SIZE);
			}
			goto end;
		}

		/* Register the reference of the oldest node of the list */
		if((ts_ith= log_line_ctx_ith->ts)< oldest_ts) {
			log_ctx->log_line_ctx_llist_oldest_ref= ref_curr_node;
			oldest_ts= ts_ith;
		}

		/* Register the reference of the last node of the list */
		if((*ref_curr_node)->next== NULL)
			log_ctx->log_line_ctx_llist_tail_ref= ref_curr_node;

		/* Update for next iteration */
		ref_curr_node= &(*ref_curr_node)->next;
	}

	/* **** If this point is reached, log-trace is new and should be added to
	 * the list **** */

	/* Create new LOG-line structure instance */
	new_log_line_ctx= log_line_ctx_allocate();
	if(new_log_line_ctx== NULL)
		goto end; // Error
	strncpy(new_log_line_ctx->code, code, LOG_LINE_SIZE);
	vsnprintf(new_log_line_ctx->desc, LOG_LINE_SIZE, format, arg);
	new_log_line_ctx->count= 1;
	new_log_line_ctx->ts= (uint64_t)monotime_curr.tv_sec;
	if(tm!= NULL) {
		snprintf(new_log_line_ctx->date, LOG_DATE_SIZE, "%d:%d:%d %d-%d-%d",
				tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900,
				tm->tm_mon + 1, tm->tm_mday);
	} else {
		memset(new_log_line_ctx->date, 0, LOG_DATE_SIZE);
	}

	/* Create new LOG-list node */
	new_node= (llist_t*)malloc(sizeof(llist_t));
	if(new_node== NULL)
		goto end; // Error
	new_node->data= (void*)new_log_line_ctx;
	new_log_line_ctx= NULL; // avoid freeing at the end of function
	new_node->next= NULL; // set node 'next'

	/* Link new node to tail */
	if(*log_ctx->log_line_ctx_llist_tail_ref== NULL)
		*log_ctx->log_line_ctx_llist_tail_ref= new_node;
	else
		(*log_ctx->log_line_ctx_llist_tail_ref)->next= new_node;
	new_node= NULL; // avoid freeing at the end of function

	/* Control log-list maximum size (truncate list if applicable) */
	if(log_ctx->log_line_ctx_llist_len>= LOG_BUF_LINES_NUM) {
		llist_t *log_line_ctx_llist_oldest=
				*log_ctx->log_line_ctx_llist_oldest_ref;
		log_line_ctx_t *log_line_ctx_oldest= (log_line_ctx_t*)
				log_line_ctx_llist_oldest->data;
		if(log_line_ctx_oldest!= NULL)
			log_line_ctx_release(&log_line_ctx_oldest);

		/* Unlink and release least recent log-trace in list */
		*log_ctx->log_line_ctx_llist_oldest_ref=
				log_line_ctx_llist_oldest->next;
		free(log_line_ctx_llist_oldest);
	} else
		log_ctx->log_line_ctx_llist_len++;

end:
#if 0 //RAL: comment-me
	do {
		llist_t *log_line_ctx_llist= log_ctx->log_line_ctx_llist;
		printf("LOG-traces list length (id= %d): %d; "
				"last log-trace code: %s\n",
				log_ctx->id, log_ctx->log_line_ctx_llist_len,
				log_line_ctx_llist!= NULL?
						((log_line_ctx_t*)(log_line_ctx_llist->data))->code:
						"null");
		fflush(stdout);
	} while(0);
#endif
	pthread_mutex_unlock(&log_ctx->mutex);
	if(new_log_line_ctx!= NULL)
		log_line_ctx_release(&new_log_line_ctx);
	if(new_node!= NULL)
		free(new_node);
	va_end(arg);
	return;
}

#ifdef LOG_FORCE_USING_STDOUT

static void log_module_fflush()
{
	/* Flush to standard-out */
	fflush(stdout);
}

#else

static void log_module_fflush()
{
	off_t logfile_off;

    /* Check LOG-file size and limit and flush if applicable */
    logfile_off= lseek(logfile_fd, 0, SEEK_CUR);
    if(logfile_off> LOG_MAX_FILESIZE) {
    	CLOSE_FILE(logfile_fd);
    	unlink(LOG_FILE_OLD);
    	rename(LOG_FILE, LOG_FILE_OLD);
    	logfile_fd= OPEN_FILE();
        if(logfile_fd< 0)
        	printf("Error: Unable to create log-file '%s'.\n", LOG_FILE);
    }
}

#endif
