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
 * @file llist.h
 * @brief Simple linked-list utility implementation
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_LLIST_H_
#define UTILS_SRC_LLIST_H_

#include <sys/types.h>
#include <inttypes.h>

/* **** Definitions **** */

typedef struct llist_s llist_t;

/**
 * Linked list type structure.
 */
typedef struct llist_s {
	void *data;
	llist_t *next;
} llist_t;

/* **** Prototypes **** */

/**
 * Creates a new link node with the given data and pushes it onto the front of
 * the list.
 * @param ref_llist_head Reference to the pointer to the so-called "head" of
 * the list. If the list is empty, the head pointer should be NULL. The head
 * pointer will be updated to the newly created node.
 * @param data Pointer to a new data element to be inserted in the list.
 * @return Status code (refer to 'stat_codes_ctx_t' type).
 * @see stat_codes_ctx_t
 */
int llist_push(llist_t** ref_llist_head, void *data);

/**
 * Extract the data from the head node of the list, delete the node, and
 * advance the head pointer to point at the next node list.
 * @param ref_llist_head Reference to the pointer to the so-called "head" of
 * the list. The head pointer will be updated to point to the next node of the
 * list.
 * @return Data element popped, or NULL in the case of error due to wrong
 * parameter or empty list.
 */
void* llist_pop(llist_t** ref_llist_head);

/**
 * Duplicate entire linked list (MACRO).
 * @param ref_llist_dst Reference to the pointer to the so-called "head" of
 * the destination list (where the nodes are to be copied).
 * @param Pointer to the "head" of the list to be copied.
 * @param node_dup_fxn Pointer to the function to be applied to copy each of
 * the node-elements of the source list. The prototype of this function is:
 * 'node_dup_fxn_t*(*node_dup_fxn)(const node_dup_fxn_t*)'.
 * @param node_dup_fxn_t Data type used in function 'node_dup_fxn'.
 * @param ret_code Status code set as result (refer to 'stat_codes_ctx_t' type).
 * @see stat_codes_ctx_t
 */
#define LLIST_DUPLICATE(ref_llist_dst, llist_src, node_dup_fxn, \
		node_dup_fxn_t, ret_code) \
do {\
	int i;\
	llist_t **_ref_llist_dst= ref_llist_dst;\
	const llist_t *_llist_src= llist_src;\
	node_dup_fxn_t*(*_node_dup_fxn)(const node_dup_fxn_t*)= node_dup_fxn;\
\
	ret_code= ERROR;\
\
	ASSERT_DO(_ref_llist_dst!= NULL, break);\
	ASSERT_DO(_node_dup_fxn!= NULL, break);\
	if(_llist_src== NULL) {\
		ret_code= SUCCESS;\
		break;\
    }\
\
	for(i= 0;; i++) {\
		void *node, *node_copy;\
\
		node= llist_get_nth(_llist_src, i);\
		if(node== NULL) {\
			ret_code= SUCCESS;\
			break;\
		}\
\
		node_copy= _node_dup_fxn((const node_dup_fxn_t*)node);\
		ASSERT_DO(node_copy!= NULL, break);\
\
		ASSERT_DO(\
			llist_insert_nth(_ref_llist_dst, i, node_copy)== SUCCESS,\
			break);\
	}\
} while(0);

/**
 * Release the entire linked list (MACRO).
 * @param ref_llist_head Reference to the pointer to the so-called "head" of
 * the list to be released.
 * @param node_release_fxn Pointer to the function to be applied to release
 * each of the node-elements of the given list. The prototype of this function
 * is: 'void (*node_release_fxn)(node_type**)'.
 * @param node_type Data type used in function 'node_release_fxn'.
 */
#define LLIST_RELEASE(ref_llist_head, node_release_fxn, node_type) \
while((*ref_llist_head)!= NULL) {\
	void (*_node_release_fxn)(node_type**)= node_release_fxn;\
	node_type *node= (node_type*)llist_pop(ref_llist_head);\
	if(node!= NULL) _node_release_fxn(&node);\
	ASSERT(node== NULL);\
}

#endif /* UTILS_SRC_LLIST_H_ */
