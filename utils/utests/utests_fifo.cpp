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
 * @file utests_fifo.cpp
 * @brief FIFO module unit-testing
 * @author Rafael Antoniello
 */

#include "utests_fifo.h"
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
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
}

/** Installation directory complete path */
#ifndef _INSTALL_DIR //HACK: "fake" path for IDE
#define _INSTALL_DIR "./"
#endif

#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#ifdef ENABLE_DEBUG_LOGS
	#define LOGD_CTX_INIT(CTX) LOG_CTX_INIT(CTX)
	#define LOGD(FORMAT, ...) LOG(FORMAT, ##__VA_ARGS__)
#else
	#define LOGD_CTX_INIT(CTX)
	#define LOGD(...)
#endif

SUITE(UTESTS_FIFO)
{

#define MAX_RUNNING_TIME_SECS 2

	/* Hack... declare consumer function */
	static UTEST_FIFO_CONSUMER_THR_FXN();

	static int producer(fifo_ctx_t *fifo_ctx)
	{
		const char *elem[]= {
				"Hello, world!.", "How are you?.", "Goodbye.", NULL
		};
		size_t msg_len[]= {
				strlen(elem[0]), strlen(elem[1]), strlen(elem[2])
		};
		LOG_CTX_INIT(NULL);

		CHECK_DO(fifo_ctx!= NULL, return STAT_ERROR);

		/* Put messages on FIFO */
		for(int i= 0; elem[i]!= NULL; i++) {
			int ret_val;
			LOGD("Putting on FIFO: '%s (size: %d)\n", elem[i], (int)msg_len[i]);

			struct timespec time_start;
			struct timespec time_stop;
			static uint64_t average_nsecs= 0;
			time_start.tv_nsec= time_stop.tv_nsec= 0;
			clock_gettime(CLOCK_MONOTONIC, &time_start);

	    	ret_val= fifo_put_dup(fifo_ctx, elem[i], msg_len[i]);

			clock_gettime(CLOCK_MONOTONIC, &time_stop);
			average_nsecs+= (time_stop.tv_nsec- time_start.tv_nsec);
			LOGD("Average time: %ld [nsecs]\n", average_nsecs);

			//LOGD("ret_code: %d\n", ret_val); // comment-me
	    	CHECK(ret_val== STAT_SUCCESS);
	    }

    	/* Let thread run for a few seconds */
		LOGD("Retain consumer thread/process for a moment\n");
   		usleep(MAX_RUNNING_TIME_SECS* 1000* 1000);
   		LOGD("O.K.\n");

   		return STAT_SUCCESS;
	}

	static void main_task(int flag_use_shm, int flag_use_thr1_proc0)
	{
		int ret_code;
		pthread_t consumer_thread;
		fifo_ctx_t *fifo_ctx= NULL;
		uint8_t *elem= NULL;
		size_t elem_size= -1;
		pid_t child_pid= -1; // process ID
		LOG_CTX_INIT(NULL);

		/* Open FIFO */
		if(!flag_use_shm) {
		    fifo_ctx= fifo_open(FIFO_SIZE, 0, 0, NULL);
		} else {
		    /* Note that GCC 5.4 only allows to specify name without '/'
		     * character. Unless mounting "shmfs" in other place, all SHM
		     * objects go to "/dev/shm/...".
		     */
		    fifo_ctx= fifo_shm_open(FIFO_SIZE, 256, 0, "/fifo_shm_utest");
		}

		if(flag_use_thr1_proc0) {
			/* Create consumer in "detach-able" thread */
			pthread_create(&consumer_thread, NULL, utest_fifo_consumer_thr,
					(void*)fifo_ctx);
		} else {
			/* Fork off the parent process */
		    child_pid= fork();
			if(child_pid< 0) {
				LOGE("Could not fork process to create daemon\n");
				exit(EXIT_FAILURE);
			} else if(child_pid== 0) {

				/* **** CHILD CODE ('child_pid'== 0) **** */
				char *const args[] = {(char *const)_INSTALL_DIR"/bin/"
						"mediaprocsutils_apps_app_utest_fifo_consumer_thr",
						(char *const)"/fifo_shm_utest", NULL};
				char *const envs[] = {
						(char *const)"LD_LIBRARY_PATH="_INSTALL_DIR"/lib",
						NULL};
				if((ret_code= execve(_INSTALL_DIR"/bin/"
						"mediaprocsutils_apps_app_utest_fifo_consumer_thr",
						args, envs))< 0) { // execve won't return if succeeded
LOGD("\n000 %d*************************************************************************************\n", ret_code);
					exit(ret_code);
				}
LOGD("\nOK OK OK*************************************************************************************\n");
				exit(EXIT_SUCCESS);
			}
			/* ... Continue main task as parent code... */
		}

	    /* Put messages on FIFO */
		producer(fifo_ctx);

   		/* Unblock FIFO before joining threads */
		LOGD("Unblock FIFO now...\n");
   		fifo_set_blocking_mode(fifo_ctx, 0);

   		if(flag_use_thr1_proc0) {
   			/* Join consumer thread */
   			LOGD("Joining consumer thread\n");
   			pthread_join(consumer_thread, NULL);
   			LOGD("OK\n");
   		} else {
   			int status;
   			pid_t w;

   			/* Wait consumer process */
   			LOGD("Waiting for consumer process\n");
   			w= waitpid(child_pid, &status, WUNTRACED);
   			if(w== -1) {
   				LOGD("Error detected while executing 'waitpid()'");
   				exit(EXIT_FAILURE);
   			}
   			if(WIFEXITED(status)) {
   				LOGD("exited, status=%d\n", WEXITSTATUS(status));
   			} else if(WIFSIGNALED(status)) {
   				LOGD("killed by signal %d\n", WTERMSIG(status));
   			} else if(WIFSTOPPED(status)) {
   				LOGD("stopped by signal %d\n", WSTOPSIG(status));
   			} else if(WIFCONTINUED(status)) {
   				LOGD("continued\n");
   			}
   			LOGD("OK\n");
   		}

   		/* Check time-out */
   		fifo_set_blocking_mode(fifo_ctx, 1); // block FIFO to use time-out
   		fifo_empty(fifo_ctx);
   		LOGD("Performing 'fifo_timedget()' to check 2 second time-out...\n");
   		ret_code= fifo_timedget(fifo_ctx, (void**)&elem, &elem_size,
   				2* 1000* 1000);
   		CHECK(ret_code== STAT_ETIMEDOUT);
   		LOGD("OK\n");

   		/* Write four messages and check overflow */
   		LOGD("Checking overflow (maximum FIFO size is: %d)\n", FIFO_SIZE);
	    /* Put messages on FIFO */
   		fifo_set_blocking_mode(fifo_ctx, 0); // unblock FIFO to overflow
	    for(int i= 0; i<= FIFO_SIZE; i++) {
	    	const char *msg= "Message to test FIFO overflow!.\0";
	    	size_t msg_len= strlen(msg);
	    	int ret_val;
	    	LOGD("Putting on FIFO: '%s (size: %d)\n", msg, (int)msg_len);
	    	ret_val= fifo_put_dup(fifo_ctx, msg, msg_len);
	    	if(i< FIFO_SIZE)
	    		CHECK(ret_val== STAT_SUCCESS);
	    	else {
	    	    LOGD("Return code: %d\n", (int)ret_val);
	    	    CHECK(ret_val== STAT_ENOMEM);
	    	}
	    }

    	/* Release FIFO */
    	fifo_close(&fifo_ctx);

    	LOGV("... passed O.K.\n");
	}

	TEST(FIFO_MULTI_THREADING)
	{
		LOG_CTX_INIT(NULL);
	    LOGV("\n\nExecuting UTESTS_FIFO::FIFO_MULTI_THREADING...\n");
	    main_task(0/*No-SHM*/, 1/*Thread*/);
	}

	TEST(FIFO_MULTI_THREADING_WITH_MMAP)
	{
		LOG_CTX_INIT(NULL);
	    LOGV("\n\nExecuting UTESTS_FIFO::FIFO_MULTI_THREADING_WITH_MMAP...\n");
	    main_task(1/*SHM*/, 1/*Thread*/);
	}

	TEST(FIFO_FORK_WITH_MMAP)
	{
		LOG_CTX_INIT(NULL);
	    LOGV("\n\nExecuting UTESTS_FIFO::FIFO_FORK_WITH_MMAP...\n");
	    main_task(1/*SHM*/, 0/*Process-forked*/);
	}

	TEST(FIFO_WITH_MMAP_OPEN_ERRORS1)
	{
		fifo_ctx_t *fifo_ctx;
		int ret_val;
		fifo_elem_alloc_fxn_t fifo_elem_alloc_fxn= {0};
		uint8_t elem[512]= {0xFF}, *elem2= NULL;
		LOG_CTX_INIT(NULL);

	    LOGV("\n\nExecuting UTESTS_FIFO::FIFO_WITH_MMAP_OPEN_ERRORS1...\n");

	    /* Erroneously open FIFO */
	    fifo_ctx= fifo_shm_open(FIFO_SIZE, 0, 0, "/fifo_shm_utest");
	    CHECK(fifo_ctx== NULL);

	    fifo_elem_alloc_fxn.elem_ctx_dup= (fifo_elem_ctx_dup_fxn_t*)
	    		4; // Any value just to check
	    fifo_ctx= fifo_open(FIFO_SIZE, 512, FIFO_PROCESS_SHARED,
	    		&fifo_elem_alloc_fxn);
	    CHECK(fifo_ctx== NULL);

    	/* Try releasing non-opened FIFO */
    	fifo_close(&fifo_ctx);

	    /* Open FIFO */
	    fifo_ctx= fifo_open(FIFO_SIZE, 256, FIFO_PROCESS_SHARED, NULL);

	    /* Perform some not-valid operations:
	     * - Exceed maximum chunk size;
	     * - Not valid put (not duplication with shared memory).
	     */
	    ret_val= fifo_put_dup(fifo_ctx, elem, sizeof(elem));
	    CHECK(ret_val== STAT_ERROR);
	    elem2= (uint8_t*)malloc(128);
	    ret_val= fifo_put(fifo_ctx, (void**)&elem2, 128);
	    CHECK(ret_val== STAT_ERROR);
	    free(elem2);
	    elem2= NULL;

    	/* Release FIFO */
    	fifo_close(&fifo_ctx);

	    /* Just open and release FIFO -this is OK- */
	    fifo_ctx= fifo_open(FIFO_SIZE, 256, FIFO_PROCESS_SHARED, NULL);
    	fifo_close(&fifo_ctx);

		LOGV("... passed O.K.\n");
	}
}
