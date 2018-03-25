/*
 * Copyright (c) 2017, 2018 Rafael Antoniello
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
 * @file utests_fifo.h
 * @brief FIFO module unit-testing
 * @author Rafael Antoniello
 */

#ifndef UTILS_UTESTS_UTESTS_FIFO_H_
#define UTILS_UTESTS_UTESTS_FIFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#define FIFO_SIZE 3

#define UTEST_FIFO_CONSUMER_THR_FXN() \
void* utest_fifo_consumer_thr(void *t)\
{\
	fifo_ctx_t *fifo_ctx= (fifo_ctx_t*)t;\
	LOGD_CTX_INIT(NULL);\
\
	while(1) {\
		int ret_code;\
		uint8_t *elem= NULL;\
		size_t elem_size= -1;\
\
		LOGD("Trying to read from FIFO... \n");\
		ret_code= fifo_get(fifo_ctx, (void**)&elem, &elem_size);\
		if(ret_code!= STAT_SUCCESS || elem== NULL || elem_size<= 0) {\
			if(ret_code== STAT_EAGAIN) {\
				LOGD("Exiting reading thread/task\n");\
				break;\
			} else { \
				ASSERT(0);\
				exit(EXIT_FAILURE);\
			}\
		}\
		LOGD("Consumer get %d characters from FIFO: '", (int)elem_size);\
		for(int c= 0; c< (int)elem_size; c++) {\
			LOGD("%c", elem[c]);\
		}\
		LOGD("'\n");\
\
		if(elem!= NULL) {\
			free(elem);\
			elem= NULL;\
		}\
	}\
	return ((void*)(intptr_t)(STAT_SUCCESS));\
}

#ifdef __cplusplus
} //extern "C"
#endif

#endif /* UTILS_UTESTS_UTESTS_FIFO_H_ */
