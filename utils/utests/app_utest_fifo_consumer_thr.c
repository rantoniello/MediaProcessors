/*
 * Copyright (c) 2015, 2016, 2017, 2018 Rafael Antoniello
 *
 * This file is part of StreamProcessors.
 *
 * StreamProcessors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StreamProcessors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with StreamProcessors.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file app_utest_fifo_consumer_thr.c
 * @brief FIFO module application implementing a consumer thread/task for
 * unit-testing purposes.
 * @author Rafael Antoniello
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>

#include <libmediaprocsutils/fifo.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>

#include "utests_fifo.h"

#ifdef __cplusplus
} //extern "C"
#endif

#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#ifdef ENABLE_DEBUG_LOGS
	#define LOGD_CTX_INIT(CTX) LOG_CTX_INIT(CTX)
	#define LOGD(FORMAT, ...) LOG(FORMAT, ##__VA_ARGS__)
#else
	#define LOGD_CTX_INIT(CTX)
	#define LOGD(...)
#endif

/* Hack... declare consumer function */
static UTEST_FIFO_CONSUMER_THR_FXN();

int main(int argc, char *argv[], char *envp[])
{
	int end_code;
	char *fifo_file_name= argv[1];
	fifo_ctx_t *fifo_ctx= NULL;
	LOG_CTX_INIT(NULL);
	//SIMULATE_CRASH();

	log_module_open();

	/* Check arguments */
	if(argc!= 2) {
		printf("Usage: %s <fifo-name>", argv[0]);
		exit(EXIT_FAILURE);
	}
	CHECK_DO(fifo_file_name!= NULL && strlen(fifo_file_name)> 0,
			exit(EXIT_FAILURE));
	LOGD("argvc: %d; argv[0]: '%s'; argv[1]: '%s'\n", argc, argv[0],
			fifo_file_name); //comment-me

	fifo_ctx= fifo_shm_exec_open(FIFO_SIZE, 256, 0, fifo_file_name);
	CHECK_DO(fifo_ctx!= NULL, exit(EXIT_FAILURE));

	end_code= (int)(intptr_t)utest_fifo_consumer_thr(fifo_ctx);

	fifo_shm_exec_close(&fifo_ctx);
	log_module_close();
	return (end_code== STAT_SUCCESS)? EXIT_SUCCESS: EXIT_FAILURE;
}
