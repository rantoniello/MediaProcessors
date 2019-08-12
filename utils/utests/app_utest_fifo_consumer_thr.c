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
 * @file app_utest_fifo_consumer_thr.c
 * @brief Application implementing a consumer task for the purpose of testing
 * the FIFO module.
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
#include <unistd.h>

#include <libmediaprocsutils/fifo.h>

#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>

#include "utests_fifo_common.h"

#ifdef __cplusplus
} //extern "C"
#endif

int main(int argc, char *argv[], char *envp[])
{
    log_ctx_t *log_ctx = NULL;
    char *fifo_name = argv[1];
    uint8_t *elem = NULL;
    size_t elem_size = 0;
    int message_cnt = 0, end_code = STAT_ERROR;
    const int64_t fifo_tout_usecs = 10 * 1000 * 1000;
    const fifo_extend_param_ctx_t fifo_extend_param_ctx = {
            NULL, NULL, NULL, .shm_fifo_name = UTESTS_FIFO_SHM_FILENAME
    };
    LOG_CTX_INIT(NULL);

    if(log_module_open() != STAT_SUCCESS ||
            (log_ctx = log_open(0 /*set to any Id.*/)) == NULL) {
        goto end;
    }
    LOG_CTX_SET(log_ctx);

    /* Check arguments */
    if(argc != 2) {
        LOGW("Usage: %s <fifo-name>", argv[0]);
        goto end;
    }
    CHECK_DO(fifo_name != NULL && strlen(fifo_name) > 0, goto end);
    LOGD("argvc: %d; argv[0]: '%s'; argv[1]: '%s'\n", argc, argv[0],
            fifo_name);

    /* Consumer loop */
    while(1) {
        register int ret_code;
        fifo_ctx_t *fifo_ctx = NULL;

        if(elem != NULL) {
            free(elem);
            elem = NULL;
        }

        /* Open and close FIFO intensively, in every loop (actually not
         * necessary, for the sake of testing).
         */
        CHECK_DO((fifo_ctx = fifo_open_shm(&fifo_extend_param_ctx,
                LOG_CTX_GET())) != NULL, goto end);

        ret_code = fifo_pull(fifo_ctx, (void**)&elem, &elem_size,
                fifo_tout_usecs, LOG_CTX_GET());

        fifo_close_shm(&fifo_ctx, LOG_CTX_GET());

        if(ret_code != STAT_SUCCESS || elem == NULL || elem_size == 0) {
            if(ret_code == STAT_EAGAIN) {
                LOGD("FIFO unlocked, exiting consumer task\n");
                break;
            } else if(ret_code == STAT_ETIMEDOUT) {
                LOGD("FIFO timed-out, exiting consumer task\n");
                break;
            } else {
                ASSERT(0); // should never occur
                goto end;
            }
        }

        LOGD("Consumer get %zu characters from FIFO: '%s\\0'\n", elem_size,
                elem);
        CHECK_DO(strcmp((const char*)elem,
                utests_fifo_messages_list_1[message_cnt++])== 0, goto end);

        /* Check if we read all the list */
        if(utests_fifo_messages_list_1[message_cnt] == NULL) {
            LOGD("List completed!, exiting consumer task\n");
            break;
        }
    }

    end_code = STAT_SUCCESS;
end:
    if(elem!= NULL)
        free(elem);
    log_close(&log_ctx);
    exit(end_code);
}
