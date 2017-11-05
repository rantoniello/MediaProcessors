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
 * @file procs.h
 * @brief Generic processors (PROC) module.
 * @author Rafael Antoniello
 */

#ifndef MEDIAPROCESSORSS_SRC_PROCS_H_
#define MEDIAPROCESSORSS_SRC_PROCS_H_

#include <sys/types.h>
#include <inttypes.h>
#include <stdarg.h>

/* Forward declarations */
typedef struct log_ctx_s log_ctx_t;
typedef struct procs_ctx_s procs_ctx_t;
typedef struct proc_frame_ctx_s proc_frame_ctx_t;

/* **** Prototypes **** */

/**
 * Open PROCS module. This is a global function and should be called only once
 * at the very beginning and during the life of the application.
 * @param log_ctx Pointer to a externally defined LOG module context structure.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int procs_module_open(log_ctx_t *log_ctx);

/**
 * Close PROCS module. This is a global function and should be called only once
 * at the end of the life of the application.
 */
void procs_module_close();

/**
 * Processors module options.
 * This function represents the API of the PROCS module.
 * This function is thread-safe and can be called concurrently.
 *
 * @param tag Processors option tag, namely, option identifier string.
 * The following options are available:
 *     -# "PROCS_REGISTER_TYPE"
 *     -# "PROCS_UNREGISTER_TYPE"
 *     .
 * @param ... Variable list of parameters according to selected option. Refer
 * to <b>Tags description</b> below to see the different additional parameters
 * corresponding to  each option tag.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 *
 * ### Tags description (additional variable arguments per tag)
 * <ul>
 * <li> <b>Tag "PROCS_REGISTER_TYPE":</b><br>
 * Register interface of an specific processor type.<br>
 * Additional variable arguments for function procs_module_opt() are:<br>
 * @param proc_if Pointer to the processor interface structure (static
 * and unambiguous interface of the type of processor we are registering).
 * Code example:
 * @code
 * ...
 * const proc_if_t proc_if_bypass_proc= {
 *     "bypass_processor",
 *     bypass_proc_open,
 *     bypass_proc_close,
 *     bypass_proc_rest_put,
 *     bypass_proc_rest_get,
 *     bypass_proc_process_frame,
 *     bypass_proc_opt,
 *     NULL, NULL, NULL
 * };
 * ...
 * ret_code= procs_module_opt("PROCS_REGISTER_TYPE", &proc_if_bypass_proc);
 * @endcode
 *
 * <li> <b>Tag "PROCS_UNREGISTER_TYPE":</b><br>
 * Unregister interface of an specific processor type.<br>
 * Additional variable arguments for function procs_module_opt() are:<br>
 * @param proc_name Pointer to a character string with the unambiguous
 * processor type name.
 * Code example:
 * @code
 * ret_code= procs_module_opt("PROCS_UNREGISTER_TYPE", "bypass_processor");
 * @endcode
 * </ul>
 *
 * <li> <b>Tag "PROCS_GET_TYPE":</b><br>
 * Get a copy of the interface of an specific processor type.<br>
 * Additional variable arguments for function procs_module_opt() are:<br>
 * @param proc_name Pointer to a character string with the unambiguous
 * processor type name.
 * @param ref_proc_if_cpy Reference to the pointer to the copy of the
 * requested processor interface context structure. If the requested processor
 * exist, a copy of the context structure will be returned by this argument;
 * otherwise, reference content will be set to NULL.
 * Code example:
 * @code
 * proc_if_t *proc_if_cpy= NULL;
 * ...
 * ret_code= procs_module_opt("PROCS_GET_TYPE", "bypass_processor",
 * 		&proc_if_cpy);
 * @endcode
 * </ul>
 */
int procs_module_opt(const char *tag, ...);

/**
 * Allocates and initializes processors (PROCS) module instance context
 * structure.
 * @param log_ctx Pointer to the LOG module context structure.
 * @return Pointer to the processors context structure on success, NULL if
 * fails.
 */
procs_ctx_t* procs_open(log_ctx_t *log_ctx);

/**
 * De-initialize and release the processors (PROCS) module instance context
 * structure.
 * @param ref_procs_ctx Reference to the pointer to the processors (PROCS)
 * module instance context structure to be release, obtained in a previous call
 * to the 'procs_open()' function. Pointer is set to NULL on return.
 */
void procs_close(procs_ctx_t **ref_procs_ctx);

/**
 * Processors module instance options.
 * This function represents the API of the PROCS module instance, and exposes
 * all the available options to operate the different processors instances.
 * This function is thread-safe and can be called concurrently.
 *
 * @param procs_ctx Pointer to the processors (PROCS) module instance context
 * structure.
 * @param tag Processors option tag, namely, option identifier string.
 * The following options are available:
 *     -# "PROCS_POST"
 *     -# "PROCS_GET"
 *     -# "PROCS_ID_DELETE"
 *     -# "PROCS_ID_GET"
 *     -# "PROCS_ID_PUT"
 *     .
 * @param ... Variable list of parameters according to selected option. Refer
 * to <b>Tags description</b> below to see the different additional parameters
 * corresponding to  each option tag.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 *
 * ### Tags description (additional variable arguments per tag)
 * <ul>
 * <li> <b>Tag "PROCS_POST":</b><br>
 * Instantiate and register new processor.<br>
 * Additional variable arguments for function procs_opt() are:<br>
 * @param proc_name Pointer to a character string with the unambiguous
 * processor type name.
 * @param settings_str Character string containing initial settings for
 * the processor. String format can be either a query-string or JSON.
 * @param rest_str Reference to the pointer to a character string
 * returning the processor identifier in JSON format as follows:
 * '{"proc_id":id_number}'
 * Code example:
 * @code
 * char *rest_str= NULL;
 * ...
 * ret_code= procs_opt(procs_ctx, "PROCS_POST", "bypass_processor",
 *     "setting1=100", &rest_str);
 * @endcode
 *
 * <li> <b>Tag "PROCS_GET":</b><br>
 * Get the representational state of the processors instances list.<br>
 * Additional variable arguments for function procs_opt() are:<br>
 * @param ref_str Reference to the pointer to a character string
 * returning the processors list representational state.
 * Code example:
 * @code
 * char *rest_str= NULL;
 * ...
 * ret_code= procs_opt(procs_ctx, "PROCS_GET", &rest_str);
 * @endcode
 *
 * <li> <b>Tag "PROCS_ID_DELETE":</b><br>
 * Unregister and release a processor instance.<br>
 * Additional variable arguments for function procs_opt() are:<br>
 * @param proc_id Processor instance unambiguous Id.
 * Code example:
 * @code
 * ret_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", proc_id);
 * @endcode
 *
 * <li> <b>Tag "PROCS_ID_GET":</b><br>
 * Get the representational state of a processor instance (including
 * current settings).<br>
 * Additional variable arguments for function procs_opt() are:<br>
 * @param proc_id Processor instance unambiguous Id.
 * @param ref_str Reference to the pointer to a character string
 * returning the processor's representational state.
 * Code example:
 * @code
 * char *rest_str= NULL;
 * ...
 * ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id, &rest_str);
 * @endcode
 *
 * <li> <b>Tag "PROCS_ID_PUT":</b><br>
 * Put (pass) new settings to a processor instance.<br>
 * Additional variable arguments for function procs_opt() are:<br>
 * @param proc_id Processor instance unambiguous Id.
 * @param str Pointer to a character string containing new settings for
 * the processor instance. String format can be either a query-string or
 * JSON.
 * Code example:
 * @code
 * ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id, "setting1=100");
 * @endcode
 */
int procs_opt(procs_ctx_t *procs_ctx, const char *tag, ...);

/**
 * Put new frame of data to be processed in the indicated processor's input
 * FIFO buffer (the processor instance is indicated by the processor Id.).
 * This function blocks until a slot is available to be able to push the new
 * frame into the processor's input FIFO. To unblock this function, the
 * corresponding processor instance should be deleted.
 * This function is thread-safe and can be called concurrently.
 * @param procs_ctx Pointer to the processors (PROCS) module instance context
 * structure.
 * @param proc_id Processor instance unambiguous Id.
 * @param proc_frame_ctx Pointer to the structure characterizing the input
 * frame to be processed. The frame is duplicated and inserted in the FIFO
 * buffer.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int procs_send_frame(procs_ctx_t *procs_ctx, int proc_id,
		const proc_frame_ctx_t *proc_frame_ctx);

/**
 * Get new processed frame of data from the indicated processor's output
 * FIFO buffer (the processor instance is indicated by the processor Id.).
 * This function blocks until a new frame is available to be read from the
 * processor's output FIFO. To unblock this function, the corresponding
 * processor instance should be deleted.
 * This function is thread-safe and can be called concurrently.
 * @param procs_ctx Pointer to the processors (PROCS) module instance context
 * structure.
 * @param proc_id Processor instance unambiguous Id.
 * @param ref_proc_frame_ctx Reference to the pointer to a structure
 * characterizing the output processed frame. This function will return the
 * processed frame passing its structure pointer by argument.
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
int procs_recv_frame(procs_ctx_t *procs_ctx, int proc_id,
		proc_frame_ctx_t **ref_proc_frame_ctx);

#endif /* MEDIAPROCESSORSS_SRC_PROCS_H_ */
