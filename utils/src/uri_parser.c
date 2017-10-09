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

/**
 * @file uri_parser.c
 * @author Rafael Antoniello
 */

#include "uri_parser.h"

#include <stdlib.h>
#include <string.h>

#include "uriparser/Uri.h"
#include "log.h"
#include "stat_codes.h"
#include "check_utils.h"

char* uri_parser_get_uri_part(const char *uri, uri_parser_uri_parts_t part)
{
	UriParserStateA state;
	UriUriA uri_uri_a;
	int part_str_size= 0;
	char *part_str= NULL, *ret_val= NULL; // value to return
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(uri!= NULL, return NULL);

	state.uri = &uri_uri_a;
	if(uriParseUriA(&state, uri)!= URI_SUCCESS)
		goto end;

	switch(part) {
	case SCHEME:
		part_str= (char*)uri_uri_a.scheme.first;
		part_str_size= uri_uri_a.scheme.afterLast- uri_uri_a.scheme.first;
		break;
	case HOSTTEXT:
		part_str= (char*)uri_uri_a.hostText.first;
		part_str_size= uri_uri_a.hostText.afterLast- uri_uri_a.hostText.first;
		break;
	case PORTTEXT:
		part_str= (char*)uri_uri_a.portText.first;
		part_str_size= uri_uri_a.portText.afterLast- uri_uri_a.portText.first;
		break;
	default:
		break;
	}

	if(part_str!= NULL && part_str_size> 0)
		ret_val= strndup(part_str, part_str_size);

end:
	uriFreeUriMembersA(&uri_uri_a);
	return ret_val;
}

char* uri_parser_query_str_get_value(const char *key, const char *query_str)
{
	UriQueryListA *queryList, *nextNode;
	int itemCount;
	size_t query_str_size;
	char *ret_val= NULL; // value to return, corresponding to 'key' parameter
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(key!= NULL, return NULL);
	CHECK_DO(query_str!= NULL, return NULL);

	query_str_size= strlen(query_str);
	if(query_str_size<= 0)
		return NULL; // nothing to parse

	if(uriDissectQueryMallocA(&queryList, &itemCount, query_str,
			&query_str[query_str_size])!= URI_SUCCESS)
		goto end;

	nextNode= queryList;
	while(nextNode!= NULL) {
		if(strncmp(key, nextNode->key, strlen(key))== 0) {
			/* Key found! */
			if(nextNode->value!= NULL)
				ret_val= strdup(nextNode->value);
			break;
		}
		nextNode= nextNode->next;
	}

end:
	uriFreeQueryListA(queryList);
	return ret_val;
}

int uri_parser_get_id_from_rest_url(const char *url, const char *needle,
		long long *ref_id)
{
	int end_code= STAT_ENOTFOUND;
	long long id= -1; // In case of 'NOT FOUND' error
	char *id_str_start= NULL, *id_str_stop= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(url!= NULL && strlen(url)> 0, return STAT_ERROR);
	CHECK_DO(needle!= NULL && strlen(needle)> 0, return STAT_ERROR);
	CHECK_DO(ref_id!= NULL, return STAT_ERROR);

	if((id_str_start= strstr(url, needle))!= NULL) {
		id_str_start+= strlen(needle);
		id= strtoll(id_str_start, &id_str_stop, 10);
		//LOGV("'strtoll()' returns: %ll\n", id); // comment-me
	}

	/* ID. can be followed by '/' or '.'; the latter refers to string
	 * '.json' that appears at the end of a URL.
	 */
	if(id_str_stop!= NULL && (*id_str_stop== '/' || *id_str_stop== '.')) {
		*ref_id= id;
		end_code= STAT_SUCCESS;
	} else {
		*ref_id= -1;
		end_code= STAT_ENOTFOUND;
	}
	return end_code;
}
