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
 * @file procs_api_http.c
 * @author Rafael Antoniello
 */

#include "procs_api_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/uri_parser.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>

#include "procs.h"

/* **** Definitions **** */

/**
 * JSON wrapped response format; is as follows:
 * {
 *     "code":number,
 *     "status":string,
 *     "message":string,
 *     "data": {...} // object returned by PROCS if any or null.
 * }
 */
#define RESPONSE_FMT "{\"code\":%d,\"status\":%s,\"message\":%s,\"data\":%s}"

/**
 * Returns non-zero if given URL string contains 'needle' sub-string.
 */
#define URL_HAS(NEEDLE) \
		(url!= NULL && strstr(url, NEEDLE)!= NULL)

/**
 * Compares given required method string description.
 */
#define URL_METHOD_IS(TAG) \
		(request_method!= NULL && \
		strncmp(request_method, TAG, strlen(TAG))== 0)

/**
 * HTTP status textual
 */
#define HTTP_OK 		"\"OK\""
#define HTTP_CREATED 	"\"Created\""
#define HTTP_NOCONTENT 	"\"No Content\""
#define HTTP_NOTFOUND 	"\"Not Found\""
#define HTTP_NOTMODIF 	"\"Not Modified\""
#define HTTP_CREATED 	"\"Created\""
#define HTTP_CONFLICT 	"\"Conflict\""

/* **** Prototypes **** */

/* **** Implementations **** */

int procs_api_http_req_handler(procs_ctx_t *procs_ctx, const char *url,
		const char *query_string, const char *request_method, char *content,
		size_t content_len, char **ref_str_response)
{
	int proc_id, ret_code, end_code= STAT_ERROR;
	int64_t aux_id= -1;
	char *proc_name_str= NULL, *data_obj_str= NULL, *response_str= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments.
	 * NOTE: Arguments 'query_string' and 'content' are allowed to be NULL.
	 */
	CHECK_DO(url!= NULL, return STAT_ERROR);
	CHECK_DO(request_method!= NULL, return STAT_ERROR);
	CHECK_DO(ref_str_response!= NULL, return STAT_ERROR);

	//LOGV("\n//----------------------------------------------------------//\n"
	//		"HTTP REQUEST: url: '%s?%s'; request_method: %s\n"
	//		"message body (%d): '%s'\n", url, query_string? query_string: "",
	//				request_method, (int)content_len, content); //comment-me

	*ref_str_response= NULL;

	/* Check 'PROCS' URL tree root existence */
	if(!URL_HAS("/procs")) {
		end_code= STAT_ENOTFOUND;
		goto end;
	}

	if(URL_HAS("/procs.json")) {

		/* **** Handle PROCS list related requests **** */

		if(0/*URL_METHOD_IS("POST")*/) {

			/* Among query-string parameters it is mandatory to find the
			 * processor type name (key "proc_name") to be instantiated.
			 * Note also that parameters that do not correspond to the
			 * settings of the instantiated processor will be just ignored,
			 * thus we can pass the full query-string for the settings.
			 */
			if(query_string== NULL ||
			   (proc_name_str= uri_parser_query_str_get_value("proc_name",
					   query_string))== NULL) {
				end_code= STAT_EINVAL;
				goto end;
			}
			end_code= procs_opt(procs_ctx, "PROCS_POST", proc_name_str,
					query_string, &data_obj_str);

		} else if(URL_METHOD_IS("GET")) {
			end_code= procs_opt(procs_ctx, "PROCS_GET", &data_obj_str, NULL);
		} else {
			end_code= STAT_ENOTFOUND;
		}
	} else if(URL_HAS("/procs/")) {

		/* **** Handle PROC instance related requests **** */

		/* We need processor identifier for these requests */
		ret_code= uri_parser_get_id_from_rest_url(url, "/procs/",
				(long long*)&aux_id);
		proc_id= (int)aux_id;
		if(proc_id< -1 || ret_code!= STAT_SUCCESS) {
			end_code= STAT_ENOTFOUND;
			goto end;
		}

		if(URL_METHOD_IS("PUT"))
			end_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id,
					query_string);
		else if (URL_METHOD_IS("GET"))
			end_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id,
					&data_obj_str);
		//else if(URL_METHOD_IS("DELETE"))
		//	end_code= procs_opt(procs_ctx, "PROCS_ID_DELETE", proc_id);
		else
			end_code= STAT_ENOTFOUND;
	} else {
		end_code= STAT_ENOTFOUND;
	}

	response_str= wrap_response(end_code, data_obj_str, request_method,
			LOG_CTX_GET());
	*ref_str_response= response_str;
	response_str= NULL; // Avoid double referencing

end:
	if(proc_name_str!= NULL)
		free(proc_name_str);
	if(data_obj_str!= NULL)
		free(data_obj_str);
	if(response_str!= NULL)
		free(response_str);
return end_code;
}

char* wrap_response(int stat_code, const char *data_obj_str,
		const char *request_method, log_ctx_t *log_ctx)
{
	int http_code;
	const char *http_status_str, *msg_str;
	size_t response_size;
	char *response_str= NULL;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	// TODO

	/* Translate end-code to HTTP status code and get related description.
	 * The translation table is as follows:
	 *
	 * HTTP method | HTTP status code
	 * ------------+------------------------------------------------
	 * GET         | 200 (OK), 404 (Not Found), 304 (Not Modified)
	 * POST        | 201 (Created), 404 (Not Found), 409 (Conflict)
	 * PUT         | 200 (OK), 204 (No Content), 404 (Not Found)
	 * DELETE      | 200 (OK), 404 (Not Found)
	 *
	 * Any other combination is assumed to be 404- "Not Found".
	 */
	switch(stat_code) {
	case STAT_SUCCESS:
		if(URL_METHOD_IS("POST")) {
			http_code= 201; http_status_str= HTTP_CREATED;
		} else {
			http_code= 200; http_status_str= HTTP_OK;
		}
		break;
	case STAT_ENOTFOUND:
		if(URL_METHOD_IS("PUT")) {
			http_code= 204; http_status_str= HTTP_NOCONTENT;
		} else {
			http_code= 404; http_status_str= HTTP_NOTFOUND;
		}
		break;
	case STAT_NOTMODIFIED:
	case STAT_EAGAIN:
		if(URL_METHOD_IS("GET")) {
			http_code= 304; http_status_str= HTTP_NOTMODIF;
		} else if(URL_METHOD_IS("PUT")) {
			http_code= 204; http_status_str= HTTP_NOCONTENT;
		} else if (URL_METHOD_IS("POST")) {
			http_code= 409; http_status_str= HTTP_CONFLICT;
		} else {
			http_code= 404; http_status_str= HTTP_NOTFOUND;
		}
		break;
	case STAT_ERROR:
	default:
		http_code= 404; http_status_str= HTTP_NOTFOUND;
		break;
	}

	/* Compose response */
	msg_str= stat_codes_get_description(stat_code); // Do not release (static)
	response_size= strlen(RESPONSE_FMT)+ sizeof(http_code);
	response_size+= http_status_str!= NULL? strlen(http_status_str): 4/*null*/;
	response_size+= msg_str!= NULL? strlen(msg_str): 4/*null*/;
	response_size+= data_obj_str!= NULL? strlen(data_obj_str): 4/*null*/;
	CHECK_DO((response_str= (char*)malloc(response_size))!= NULL, goto end);
	snprintf(response_str, response_size, RESPONSE_FMT, http_code,
			http_status_str!= NULL? http_status_str: "null",
			msg_str!= NULL && strlen(msg_str)> 0? msg_str: "null",
			data_obj_str!= NULL? data_obj_str: "null");

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS) {
		if(response_str!= NULL) {
			free(response_str);
			response_str= NULL;
		}
	}
	LOGD("Response: '%s'\n", response_str!= NULL? response_str: "NULL");
	return response_str;
}
