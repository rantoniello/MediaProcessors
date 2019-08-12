/*
 * Copyright (c) 2017, 2018, 2019, 2020 Rafael Antoniello
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
 * @file utests_fifo.cpp
 * @brief FIFO module testing
 * @author Rafael Antoniello
 */

#include "utests_fifo_common.h"
#include <UnitTest++/UnitTest++.h>

extern "C" {
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <libmediaprocsutils/fifo.h>
#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
}

#include <iostream>
#include <thread>

/** Installation directory complete path */
#ifndef _INSTALL_DIR //HACK: "fake" path for IDE
#define _INSTALL_DIR "./"
#endif


extern char **environ;


static int producer_to_forked_consumer(
		int (*producer_cb_fx)(fifo_ctx_t*, log_ctx_t*), log_ctx_t *log_ctx)
{
	int status, end_code = STAT_ERROR;
	fifo_ctx_t *fifo_ctx = NULL;
	const size_t max_data_size = UTESTS_FIFO_MESSAGE_MAX_LEN,
			pool_size = sizeof(ssize_t) + max_data_size;
	pid_t child_pid = -1; // process ID
	const fifo_extend_param_ctx_t fifo_extend_param_ctx = {
			NULL, NULL, NULL, .shm_fifo_name = UTESTS_FIFO_SHM_FILENAME
	};
	LOG_CTX_INIT(log_ctx);

	/* Make sure FIFO name does not already exist */
	if(access("/dev/shm" UTESTS_FIFO_SHM_FILENAME, F_OK) == 0)
		CHECK_DO(shm_unlink(UTESTS_FIFO_SHM_FILENAME) == 0, goto end);

	/* Create FIFO in shared memory */
	fifo_ctx = fifo_open(pool_size, FIFO_PROCESS_SHARED,
			&fifo_extend_param_ctx, LOG_CTX_GET());
	CHECK_DO(fifo_ctx != NULL, goto end);

	if((child_pid = fork()) < 0) {
		LOGE("Could not fork process to create daemon\n");
		goto end;
	} else if(child_pid == 0) {
		// **** CHILD CODE  ****
		char *const args[] = {
				(char *const)_INSTALL_DIR
				"/mediaprocsutils_apps_app_utest_fifo_consumer_thr",
				(char *const)UTESTS_FIFO_SHM_FILENAME,
				NULL
		};
		execve(_INSTALL_DIR
				"/bin/mediaprocsutils_apps_app_utest_fifo_consumer_thr",
				args, environ); // execve won't return if succeeded
		exit(EXIT_FAILURE);
	}
	/* ... Continue main task as parent code... */

	if(producer_cb_fx(fifo_ctx, log_ctx) != STAT_SUCCESS)
		goto end;

	end_code = STAT_SUCCESS;
end:
	/* Wait for consumer process to terminate */
	fifo_set_blocking_mode(fifo_ctx, 0/*unblock*/, LOG_CTX_GET());
	LOGD("Waiting for consumer process to terminate...\n");
	if(waitpid(child_pid, &status, WUNTRACED) == -1) {
		LOGD("Error detected while executing 'waitpid()'");
		end_code = STAT_ERROR; // set to error again!
	}
	if(WIFEXITED(status))
		LOGD("exited, status=%d\n", WEXITSTATUS(status));
	else if(WIFSIGNALED(status))
		LOGD("killed by signal %d\n", WTERMSIG(status));
	else if(WIFSTOPPED(status))
		LOGD("stopped by signal %d\n", WSTOPSIG(status));
	else if(WIFCONTINUED(status))
		LOGD("continued\n");
	LOGD("OK\n");
	CHECK_DO(status == STAT_SUCCESS, end_code = STAT_ERROR/*set error*/);

	fifo_close(&fifo_ctx, LOG_CTX_GET());

	return end_code;
}


static int producer_to_thread_consumer(
		int (*producer_cb_fx)(fifo_ctx_t*, log_ctx_t*), log_ctx_t *log_ctx)
{
	std::thread consumerThr;
	int end_code = STAT_ERROR, end_code_thr = STAT_ERROR;
	fifo_ctx_t *fifo_ctx = NULL;
	const size_t max_data_size = UTESTS_FIFO_MESSAGE_MAX_LEN,
			pool_size = sizeof(ssize_t) + max_data_size;
	fifo_extend_param_ctx_t fifo_extend_param_ctx = {
			NULL, NULL, NULL, NULL
	};
	LOG_CTX_INIT(log_ctx);

	/* Create FIFO */
	fifo_ctx = fifo_open(pool_size, 0, &fifo_extend_param_ctx, LOG_CTX_GET());
	CHECK_DO(fifo_ctx != NULL, goto end);

	/* Launch consumer thread */
	consumerThr = std::thread([&]() {
		uint8_t *elem = NULL;
		size_t elem_size = 0;
		int message_cnt = 0;
		const int64_t fifo_tout_usecs = 10 * 1000 * 1000;

		/* Consumer loop */
		while(1) {
			register int ret_code;

			if(elem != NULL) {
				free(elem);
				elem = NULL;
			}

			ret_code = fifo_pull(fifo_ctx, (void**)&elem, &elem_size,
					fifo_tout_usecs, LOG_CTX_GET());

			if(ret_code != STAT_SUCCESS || elem == NULL || elem_size == 0) {
				if(ret_code == STAT_EAGAIN) {
					LOGD("FIFO unlocked, exiting consumer task\n");
					break;
				} else if(ret_code == STAT_ETIMEDOUT) {
					LOGD("FIFO timed-out, exiting consumer task\n");
					break;
				} else {
					ASSERT(0); // should never occur
					goto end_thr;
				}
			}

			LOGD("Consumer get %zu characters from FIFO: '%s\\0'\n", elem_size,
					elem);
			CHECK_DO(strcmp((const char*)elem,
					utests_fifo_messages_list_1[message_cnt++])== 0,
					goto end_thr);

			/* Check if we read all the list */
			if(utests_fifo_messages_list_1[message_cnt] == NULL) {
				LOGD("List completed!, exiting consumer task\n");
				break;
			}
		}

	end_code_thr = STAT_SUCCESS;
end_thr:
		if(elem!= NULL)
			free(elem);
	});

	if(producer_cb_fx(fifo_ctx, LOG_CTX_GET()) != STAT_SUCCESS)
		goto end;

	end_code = STAT_SUCCESS;
end:
	/* Wait for consumer thread to terminate */
	fifo_set_blocking_mode(fifo_ctx, 0/*unblock*/, LOG_CTX_GET());
	LOGD("Waiting for consumer thread to terminate...\n");
	consumerThr.join();
	LOGD("OK\n");
	CHECK_DO(end_code_thr == STAT_SUCCESS, end_code = STAT_ERROR/*set error*/);

	fifo_close(&fifo_ctx, LOG_CTX_GET());

	return end_code;
}


/*
 * Test messages happy-path.
 */
static int prod_cb1(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx)
{
	int i;
	LOG_CTX_INIT(log_ctx);

    /* Push some messages */
    for(i = 0; utests_fifo_messages_list_1[i] != NULL; i++) {
        CHECK_DO(fifo_push(fifo_ctx, (void**)&utests_fifo_messages_list_1[i],
        		strlen(utests_fifo_messages_list_1[i]) + 1, LOG_CTX_GET()) ==
        		STAT_SUCCESS, LOGE("----------------- %d\n", __LINE__); return STAT_ERROR);
        LOGW("----------------- %d\n", __LINE__); //FIXME!!
    }

    return STAT_SUCCESS;
}


/*
 * Test invalid messages (e.j. exceed length) should fail.
 */
static int prod_cb2(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx)
{
	int i;
	LOG_CTX_INIT(log_ctx);

    for(i = 0; utests_fifo_messages_list_2[i] != NULL; i++)
        CHECK_DO(fifo_push(fifo_ctx, (void**)&utests_fifo_messages_list_2[i],
        		strlen(utests_fifo_messages_list_2[i]) + 1, LOG_CTX_GET()) ==
        		STAT_ENOMEM, return STAT_ERROR);

    return STAT_SUCCESS;
}


/*
 * Test invalid arguments should fail.
 */
static int prod_cb3(fifo_ctx_t *fifo_ctx, log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

    CHECK_DO(fifo_push(fifo_ctx,
    		(void**)&utests_fifo_messages_list_2[0], 0, LOG_CTX_GET()) ==
    				STAT_ERROR, return STAT_ERROR);

    return STAT_SUCCESS;
}


SUITE(UTESTS_FIFO)
{
	TEST(FIFO_FORK_WITH_MMAP)
	{
		log_ctx_t *log_ctx = NULL;

		/* Open logger */
		if(log_module_open() != STAT_SUCCESS ||
				(log_ctx = log_open(0 /*set to any Id.*/)) == NULL) {
			CHECK(0);
			return;
		}

		CHECK(producer_to_forked_consumer(prod_cb1, log_ctx) == STAT_SUCCESS);
		//CHECK(producer_to_forked_consumer(prod_cb2, log_ctx) == STAT_SUCCESS);
		//CHECK(producer_to_forked_consumer(prod_cb3, log_ctx) == STAT_SUCCESS);

		log_close(&log_ctx);
	}

	TEST(FIFO_WITH_THREAD)
	{
		log_ctx_t *log_ctx = NULL;

		/* Open logger */
		if(log_module_open() != STAT_SUCCESS ||
				(log_ctx = log_open(0 /*set to any Id.*/)) == NULL) {
			CHECK(0);
			return;
		}

		CHECK(producer_to_thread_consumer(prod_cb1, log_ctx) == STAT_SUCCESS);
		//CHECK(producer_to_thread_consumer(prod_cb2, log_ctx) == STAT_SUCCESS);
		//CHECK(producer_to_thread_consumer(prod_cb3, log_ctx) == STAT_SUCCESS);

		log_close(&log_ctx);
	}
}
