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

/**
 * @file llist.c
 * @author Rafael Antoniello
 */

#include "llist.h"

#include <stdlib.h>
#include <string.h>

#include "check_utils.h"
#include "log.h"
#include "stat_codes.h"

/* **** Definitions **** */

/* **** Prototypes **** */

/* **** Implementations **** */

/*
 * Notes:
 * Is understood that new data is always pushed as the first element;
 * If the list is not yet initialized (namely, is "empty"), the push
 * operation works as well as initializer as long 'ref_llist_head' is set
 * originally to NULL.
 */
int llist_push(llist_t** ref_llist_head, void *data)
{
	llist_t *new_node;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(ref_llist_head!= NULL, return STAT_EINVAL);
	CHECK_DO(data!= NULL, return STAT_EINVAL);

	new_node= (llist_t*)malloc(sizeof(llist_t));
	CHECK_DO(new_node!= NULL, return STAT_ENOMEM);

	new_node->data= data;
	new_node->next= *ref_llist_head;
	*ref_llist_head= new_node;
	return STAT_SUCCESS;
}

void* llist_pop(llist_t** ref_llist_head)
{
	llist_t *head;
	void *data;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(ref_llist_head!= NULL, return NULL);

	/* If list is empty we are done */
	if((head= *ref_llist_head)== NULL)
		return NULL;

	data= head->data; // get data
	*ref_llist_head= head->next; // update head (unlink popped node)
	free(head);

	return data;
}

int llist_len(const llist_t *llist_head)
{
	int cnt= 0;
	llist_t *curr_node;
	//LOG_CTX_INIT(NULL);

	/* Check arguments.
	 * Note: argument 'llist_head' is allowed to be NULL.
	 */

	curr_node= (llist_t*)llist_head;
	while(curr_node!= NULL) {
		cnt++;
		curr_node= curr_node->next;
	}
	return cnt;
}

void* llist_get_nth(const llist_t *llist_head, int index)
{
	int cnt= 0;
	llist_t *curr_node;
	LOG_CTX_INIT(NULL);

	/* Check arguments.
	 * Note: argument 'llist_head' is allowed to be NULL.
	 */
	if(llist_head== NULL)
		return NULL; // element not found (empty list)
	CHECK_DO(index>= 0, return NULL);

	curr_node= (llist_t*)llist_head;

	// the index of the node we're currently looking at
	while(curr_node!= NULL) {
		if(cnt++== index)
			return curr_node->data;
		curr_node= curr_node->next;
	}
	return NULL; // element not found
}

int llist_insert_nth(llist_t **ref_llist_head, int index, void *data)
{
	llist_t **curr_node;
	int i;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(ref_llist_head!= NULL, return STAT_ERROR);
	CHECK_DO(index>= 0, return STAT_ERROR);
	CHECK_DO(data!= NULL, return STAT_ERROR);

	if(index== 0) // Position 0 is a special case
		return llist_push(ref_llist_head, data);

	/* Find node at given index */
	curr_node= ref_llist_head;
	for(i= 0; i< index- 1; i++) {
		if(*curr_node== NULL) // index was too big
			return llist_push(curr_node, data); // Append at the end
		curr_node= &(*curr_node)->next;
	}
	if(*curr_node== NULL) // index was too big
		return llist_push(curr_node, data); // Append at the end

	/* Push new node at given index */
	return llist_push(&(*curr_node)->next, data);
}
