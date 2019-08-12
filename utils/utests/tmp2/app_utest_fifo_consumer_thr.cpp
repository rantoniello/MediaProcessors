
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

#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>

#include "utests_fifo.h"

#ifdef __cplusplus
} //extern "C"
#endif

#include "SHM_FIFO.h"

int main(int argc, char *argv[], char *envp[])
{
    utils_logs_ctx_t *utils_logs_ctx = NULL;
    char *fifo_name = argv[1];
    shm_fifo_ctx_t *shm_fifo_ctx = NULL;
    uint8_t *elem = NULL;
    size_t elem_size = 0;
    LOG_CTX_INIT(NULL);
    int message_cnt = 0;

    utils_logs_ctx = utils_logs_open(NULL, NULL); // just log to stdout
    CHECK_DO(utils_logs_ctx != NULL, exit(EXIT_FAILURE));
    LOG_CTX_SET(utils_logs_ctx);

    // Check arguments
    if(argc != 2)
    {
        LOGW("Usage: %s <fifo-name>", argv[0]);
        exit(EXIT_FAILURE);
    }
    CHECK_DO(fifo_name != NULL && strlen(fifo_name) > 0, exit(EXIT_FAILURE));
    LOGD("argvc: %d; argv[0]: '%s'; argv[1]: '%s'\n", argc, argv[0], fifo_name); //comment-me

    while(1)
    {
        int ret_code;

        if(elem != NULL)
        {
            free(elem);
            elem = NULL;
        }

        // Open and close FIFO -this is the way we do it e.g. in Nginx workers-
        shm_fifo_ctx = shm_fifo_open(fifo_name, LOG_CTX_GET());
        CHECK_DO(shm_fifo_ctx != NULL, exit(EXIT_FAILURE));

        ret_code = shm_fifo_pull(shm_fifo_ctx, (void**)&elem, &elem_size, -1, LOG_CTX_GET());

        shm_fifo_close(&shm_fifo_ctx, LOG_CTX_GET());

        if(ret_code != 0 || elem == NULL || elem_size == 0)
        {
            if(ret_code == EAGAIN)
            {
                LOGD("FIFO unlocked, exiting consumer task\n");
                break;
            }
            else if(ret_code == ETIMEDOUT)
            {
                LOGD("FIFO timed-out, exiting consumer task\n");
                break;
            }
            else
            {
                CHECK(0); // should never occur
                exit(EXIT_FAILURE);
            }
        }

        LOGD("Consumer get %d characters from FIFO: '%s'\n", (int)elem_size, elem);

        CHECK_DO(strcmp((const char*)elem, messages_list_1[message_cnt++]) == 0, exit(EXIT_FAILURE));

        // Check if we read all the list
        if(messages_list_1[message_cnt] == NULL)
        {
            LOGD("List completed!, exiting consumer task\n");
            break;
        }
    }

    if(elem!= NULL)
        free(elem);
    utils_logs_close(&utils_logs_ctx);
    return EXIT_SUCCESS;
}

