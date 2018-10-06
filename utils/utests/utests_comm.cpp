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

#include <UnitTest++/UnitTest++.h>

extern "C" {
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>

#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/comm.h>
#include <libmediaprocsutils/comm_udp.h>
}

SUITE(UTESTS_UDP_COMM)
{
	TEST(UDP_COMM_SIMPLE_TEST)
	{
		int ret_code;
		comm_ctx_t *comm_ctx_oput= NULL, *comm_ctx_iput= NULL;
		const char *msg= "Hello, world!!.\0";
		char *input_msg= NULL, input_msg_buf[128]= {0};
		size_t input_msg_size= 0;
		char *from= NULL;
		LOGD_CTX_INIT(NULL);

		LOGD("\n\nExecuting UTESTS_UDP_COMM::UDP_COMM_SIMPLE_TEST...\n");

	    /* Open COMM module */
	    ret_code= comm_module_open(NULL);
	    CHECK(ret_code== STAT_SUCCESS);
	    if(ret_code!= STAT_SUCCESS)
	    	goto end;

	    /* Register UDP protocol */
	    ret_code= comm_module_opt("COMM_REGISTER_PROTO", &comm_if_udp);
	    CHECK(ret_code== STAT_SUCCESS);
	    if(ret_code!= STAT_SUCCESS)
	    	goto end;

	    /* Open UPD protocol module instances for input/output */
	    comm_ctx_oput= comm_open("udp://127.0.0.1:2000", NULL, COMM_MODE_OPUT,
	    		NULL);
	    CHECK(comm_ctx_oput!= NULL);
	    if(comm_ctx_oput== NULL)
	    	goto end;
	    comm_ctx_iput= comm_open("udp://127.0.0.1:2000", NULL, COMM_MODE_IPUT,
	    		NULL);
	    CHECK(comm_ctx_iput!= NULL);
	    if(comm_ctx_iput== NULL)
	    	goto end;

	    /* Send a UDP packet with raw text */
	    ret_code= comm_send(comm_ctx_oput, msg, strlen(msg), NULL);
	    LOGD("comm_send ret. code: %d\n", ret_code);

	    /* Receive the UDP packet with raw text */
	    ret_code= comm_recv(comm_ctx_iput, (void**)&input_msg, &input_msg_size,
	    		&from, NULL);
	    CHECK(input_msg!= NULL && from!= NULL);
	    if(input_msg== NULL || from== NULL)
	    	goto end;
	    // Copy just to be able to add a NULL character at the end to print
	    // the message
	    memcpy(input_msg_buf, input_msg, input_msg_size);
	    LOGD("recv ret. code: %d\n", ret_code);
	    LOGD("recv ret. message: %s (size: %u)\n", input_msg_buf,
	    		input_msg_size);
	    LOGD("recv ret. received from: %s\n", from);

	    LOGD("... passed O.K.\n");
end:
		if(input_msg!= NULL)
			free(input_msg);
		if(from!= NULL)
			free(from);
		comm_close(&comm_ctx_oput);
		comm_close(&comm_ctx_iput);
    	comm_module_close();
	}

	static void* consumer_thr(void *t)
	{
		int ret_code;
		comm_ctx_t *comm_ctx_iput= (comm_ctx_t*)t;
		char *input_msg= NULL;
		size_t input_msg_size= 0;
		char *from= NULL;
		LOGD_CTX_INIT(NULL);

		CHECK(comm_ctx_iput!= NULL);
		if(comm_ctx_iput== NULL)
			return NULL;

	    /* Listen to empty input */
	    ret_code= comm_recv(comm_ctx_iput, (void**)&input_msg, &input_msg_size,
	    		&from, NULL);
	    LOGD("recv ret. code: %d\n", ret_code);
	    CHECK(ret_code== STAT_EOF);

		if(input_msg!= NULL)
			free(input_msg);
		if(from!= NULL)
			free(from);
		return NULL;
	}

	TEST(UDP_COMM_TEST_UNBLOCK)
	{
		int ret_code;
		pthread_t consumer_thread;
		comm_ctx_t *comm_ctx_iput= NULL;
		LOGD_CTX_INIT(NULL);

		LOGD("\n\nExecuting UTESTS_UDP_COMM::UDP_COMM_SIMPLE_TEST...\n");

	    /* Open COMM module */
	    ret_code= comm_module_open(NULL);
	    CHECK(ret_code== STAT_SUCCESS);
	    if(ret_code!= STAT_SUCCESS)
	    	goto end;

	    /* Register UDP protocol */
	    ret_code= comm_module_opt("COMM_REGISTER_PROTO", &comm_if_udp);
	    CHECK(ret_code== STAT_SUCCESS);
	    if(ret_code!= STAT_SUCCESS)
	    	goto end;

	    /* Open UPD protocol module instances for input */
	    comm_ctx_iput= comm_open("udp://127.0.0.1:2000", NULL, COMM_MODE_IPUT,
	    		NULL);
	    CHECK(comm_ctx_iput!= NULL);
	    if(comm_ctx_iput== NULL)
	    	goto end;

		/* Create consumer in "detach-able" thread */
	    LOGD("Launching thread to undifinetely wait for UDP message...\n");
		pthread_create(&consumer_thread, NULL, consumer_thr,
				(void*)comm_ctx_iput);

		LOGD("Wait for 3 seconds...\n");
		usleep(1000*1000);
		LOGD("Wait for 2 seconds...\n");
		usleep(1000*1000);
		LOGD("Wait for 1 seconds...\n");
		usleep(1000*1000);
		LOGD("Unlocking UDP communication module...\n");
		ret_code= comm_unblock(comm_ctx_iput);
	    CHECK(ret_code== STAT_SUCCESS);
	    if(ret_code!= STAT_SUCCESS)
	    	goto end;

    	/* Join consumer thread */
   		LOGD("Joining consumer thread\n");
   		pthread_join(consumer_thread, NULL);
   		LOGD("OK\n");

	    LOGD("... passed O.K.\n");
end:
		comm_close(&comm_ctx_iput);
    	comm_module_close();
	}
}
