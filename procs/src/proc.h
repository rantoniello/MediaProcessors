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
 * @file proc.h
 * @brief Generic processor (PROC) module.
 * @author Rafael Antoniello
 *
 * A typical application would use a generic processor as follows:<br>
 * 1) Application prologue: Open the processor using function 'proc_open()' and
 * obtain its context structure (or handler). Opening the processor is
 * performed only once at the beginning of the application.
 * Function 'proc_open()' will internally initialize and launch necessary
 * processing threads.<br>
 * 2) Application cyclic: Launch a producer thread and a consumer thread for
 * processing data. Use a control thread to manage processor run-time options.
 * The producer should use 'proc_send_frame()' function to put new frames of
 * data into the processor's input FIFO buffer.
 * The consumer should use 'proc_recv_frame()' to obtain processed frames from
 * the processor's output FIFO buffer.
 * The control thread may use the function 'proc_opt()' to manage processor
 * options.<br>
 * 3) Application epilogue: Close the processor using the function
 * 'proc_close()'. Function 'proc_close()' internally joins the processing
 * threads and release all related resources.<br>
 * <br>
 * Concurrency: The processor (PROC) module is thread-safe, thus run-time
 * functions can be executed concurrently (at the exception of 'proc_open()'
 * and 'proc_close()' functions).
 */

#ifndef MEDIAPROCESSORS_SRC_PROC_H_
#define MEDIAPROCESSORS_SRC_PROC_H_

#include <stdarg.h>
#include <pthread.h>
#include <libmediaprocsutils/mem_utils.h>

/* **** Definitions **** */

/* Forward definitions */
typedef struct proc_if_s proc_if_t;
typedef struct log_ctx_s log_ctx_t;
typedef struct fifo_ctx_s fifo_ctx_t;
typedef struct fair_lock_s fair_lock_t;
typedef struct proc_frame_ctx_s proc_frame_ctx_t;

/**
 * Processor input-output type enumerator.
 */
typedef enum {
    PROC_IPUT= 0,
	PROC_OPUT= 1,
	PROC_IO_NUM= 2
} proc_io_t;

/**
 * Generic processor (PROC) context structure.
 */
typedef struct proc_ctx_s {
	/**
	 * PROC interface structure.
	 */
	const proc_if_t *proc_if;
	/**
	 * Each PROC instance is registered in an instance array with a specific
	 * index. The idea behind using an array is to fetch as fast as possible
	 * the PROC instance to perform i/o operations.
	 */
	int proc_instance_index;
	/**
	 * Processor API mutual exclusion lock.
	 */
	pthread_mutex_t api_mutex;
	/**
	 * External LOG module context structure instance.
	 */
	 log_ctx_t *log_ctx;
	/**
	 * Input/output FIFO buffers.
	 */
	fifo_ctx_t *fifo_ctx_array[PROC_IO_NUM];
	/**
	 * Input/output mutual exclusion locks.
	 */
	fair_lock_t *fair_lock_io_array[PROC_IO_NUM];
	/**
	 * Input/output bitrate statistics [bits per second]
	 */
	volatile uint32_t bitrate[PROC_IO_NUM];
	/**
	 * Accumulated bits at input/output interface. These variables are used
	 * internally to compute the input and output bitrate statistics
	 * periodically.
	 */
	volatile uint32_t acc_io_bits[PROC_IO_NUM];
	/**
	 * Critical region to acquire or modify 'acc_io_bits[]' variable field.
	 */
	pthread_mutex_t acc_io_bits_mutex[PROC_IO_NUM];
	/**
	 * Processing thread exit indicator.
	 * Set to non-zero to signal processing to abort immediately.
	 */
	volatile int flag_exit;
	/**
	 * Processing thread.
	 */
	pthread_t proc_thread;
	/**
	 * Processing thread function reference.
	 */
	const void*(*start_routine)(void *);
} proc_ctx_t;

/* **** Prototypes **** */

/**
 * Allocates generic processor (PROC) context structure, initializes,
 * and launches processing thread.
 * @param proc_if Pointer to the processor interface structure (static
 * and unambiguous interface of the type of processor we are opening).
 * @param settings_str Character string containing initial settings for
 * the processor. String format can be either a query-string or JSON.
 * @param proc_instance_index Each PROC instance is registered in an instance
 * array with a specific index (managed and assigned from outside this module).
 * The idea behind using an array is to fetch as fast as possible the PROC
 * instance to perform i/o operations.
 * @param fifo_ctx_maxsize Maximum size, in number of queued elements, for the
 * input and output FIFOs of the processor.
 * @param log_ctx Pointer to the LOG module context structure.
 * @param arg Variable list of parameters defined by user.
 * @return Pointer to the generic processor context structure on success,
 * NULL if fails.
 */
proc_ctx_t* proc_open(const proc_if_t *proc_if, const char *settings_str,
		int proc_instance_index, uint32_t fifo_ctx_maxsize[PROC_IO_NUM],
		log_ctx_t *log_ctx, va_list arg);

/**
 * Ends processing thread, de-initialize and release the generic processor
 * (PROC) context structure and all the related resources.
 * @param ref_proc_ctx Reference to the pointer to the processor (PROC)
 * context structure to be release, that was obtained in a previous call
 * to the 'proc_open()' function. Pointer is set to NULL on return.
 */
void proc_close(proc_ctx_t **ref_proc_ctx);

/**
 * Put new frame of data to be processed in the processor's input FIFO buffer.
 * Unless unblocked (see processor options 'proc_opt()'), this function blocks
 * until a slot is available to be able to push the new frame into the
 * processor's input FIFO.
 * This function is thread-safe and can be called concurrently.
 * @param proc_ctx Pointer to the processor (PROC) context structure obtained
 * in a previous call to the 'proc_open()' function.
 * @param proc_frame_ctx Pointer to the structure characterizing the input
 * frame to be processed. The frame is duplicated and inserted in the FIFO
 * buffer.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int proc_send_frame(proc_ctx_t *proc_ctx,
		const proc_frame_ctx_t *proc_frame_ctx);

/**
 * Get new processed frame of data from the processor's output FIFO buffer.
 * Unless unblocked (see processor options 'proc_opt()'), this function blocks
 * until a new frame is available to be read from the processor's output FIFO.
 * This function is thread-safe and can be called concurrently.
 * @param proc_ctx Pointer to the processor (PROC) context structure obtained
 * in a previous call to the 'proc_open()' function.
 * @param ref_proc_frame_ctx Reference to the pointer to a structure
 * characterizing the output processed frame. This function will return the
 * processed frame passing its structure pointer by argument.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int proc_recv_frame(proc_ctx_t *proc_ctx,
		proc_frame_ctx_t **ref_proc_frame_ctx);

/**
 * Processor options.
 * This function is thread-safe and can be called concurrently.
 *
 * @param proc_ctx Pointer to the processor (PROC) context structure obtained
 * in a previous call to the 'proc_open()' function.
 * @param tag Processor option tag, namely, option identifier string.
 * The following options are available:
 *     -# PROC_UNBLOCK
 *     -# PROC_GET
 *     -# PROC_PUT
 *     .
 * @param ... Variable list of parameters according to selected option. Refer
 * to <b>Tags description</b> below to see the different additional parameters
 * corresponding to  each option tag.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 *
 * ### Tags description (additional variable arguments per tag)
 * <ul>
 * <li> <b>Tag "PROC_UNBLOCK":</b> <br>
 * Unblock processor input/output FIFO buffers.<br>
 * No additional variable arguments are needed for calling function
 * proc_opt() with this tag.
 *
 * Tag "PROC_GET":</b> <br>
 * Get processor representational state (including current settings).<br>
 * Additional variable arguments for function proc_opt() are:<br>
 * @param ref_str Reference to the pointer to a character string
 * returning the processor's representational state.
 *
 * Tag "PROC_PUT":</b> <br>
 * Put (pass) new settings to processor.<br>
 * Additional variable arguments for function proc_opt() are:<br>
 * @param str Pointer to a character string containing new settings for
 * the processor. String format can be either a query-string or JSON.
 */
int proc_opt(proc_ctx_t *proc_ctx, const char *tag, ...);

/**
 * The function 'proc_vopt()' is the same as proc_opt() except that it is
 * called with a va_list instead of a variable number of arguments.
 * This function does not call the va_end macro. Because they invoke the
 * va_arg macro, the value of the argument pointer is undefined after the call.
 */
int proc_vopt(proc_ctx_t *proc_ctx, const char *tag, va_list arg);

#endif /* MEDIAPROCESSORS_SRC_PROC_H_ */
