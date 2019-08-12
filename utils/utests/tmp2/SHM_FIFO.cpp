
#include "SHM_FIFO.h"

extern "C" {
#include <sys/mman.h>
#include <sys/stat.h>

#include <log.h>
#include <fifo.h>
}


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
#define ENABLE_DEBUG_LOGS //uncomment to trace logs
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
}

/** Installation directory complete path */
#ifndef _INSTALL_DIR //HACK: "fake" path for IDE
#define _INSTALL_DIR "./"
#endif


SUITE(SHM_FIFO_Test)
{
    int status;
    pid_t w;
    utils_logs_ctx_t *utils_logs_ctx = NULL;
    shm_fifo_ctx_t *shm_fifo_ctx= NULL;
    const char *shm_fifo_name = "/fifo_shm_utest";
    const size_t max_data_size = UNIT_TESTS_SHM_FIFO_MESSAGE_MAX_LEN, shm_pool_size = sizeof(ssize_t) + max_data_size;
    pid_t child_pid = -1; // process ID
    char *read_buf = NULL;
    size_t read_buf_size = 0;
    LOG_CTX_INIT(NULL);

    // Open logger (just log to stdout)
    utils_logs_ctx = utils_logs_open(NULL, NULL);
    ASSERT_TRUE(utils_logs_ctx != NULL);
    LOG_CTX_SET(utils_logs_ctx);

    // Make sure FIFO name does not already exist
    if(access((std::string("/dev/shm") + shm_fifo_name).c_str(), F_OK) == 0)
        ASSERT_TRUE(shm_unlink(shm_fifo_name) == 0);

    // Create SHM-FIFO
    shm_fifo_ctx = shm_fifo_create(shm_fifo_name, shm_pool_size, 0, LOG_CTX_GET());
    ASSERT_TRUE(shm_fifo_ctx != NULL);

    // Fork off the parent process
    if((child_pid = fork()) < 0)
    {
        LOGE("Could not fork process to create daemon\n");
        ASSERT_TRUE(false); // force fail
    }
    else if(child_pid == 0)
    {
        // **** CHILD CODE  ****
        char *const args[] =
        {
                (char *const)(std::string(_CURRENT_BINARY_DIR) + "/SHMFIFOUnitTest").c_str(),
                (char *const)shm_fifo_name,
                NULL
        };
        char *const envs[] =
        {
                NULL
        };
        execve(_CURRENT_BINARY_DIR "/SHMFIFOUnitTest", args, envs);
        // execve won't return if succeeded
        ASSERT_TRUE(false);
    }
    // ... Continue main task as parent code...

    // Push some messages; note that pool size is 'shm_pool_size' and messages should fit!
    for(int i = 0; messages_list_1[i] != NULL; i++)
        ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, messages_list_1[i], strlen(messages_list_1[i]) + 1, LOG_CTX_GET())
                == 0);

    // Push invalid messages
    for(int i = 0; messages_list_2[i] != NULL; i++)
        ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, messages_list_2[i], strlen(messages_list_2[i]) + 1, LOG_CTX_GET()) ==
                -1);
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, messages_list_2[0], 0, LOG_CTX_GET()) == -1);

    // Wait for consumer process to terminate
    LOGD("Waiting for consumer process to terminate...\n");
    w = waitpid(child_pid, &status, WUNTRACED);
    if(w == -1) {
        LOGD("Error detected while executing 'waitpid()'");
        ASSERT_TRUE(false); // force fail
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
    ASSERT_TRUE(status == 0);

    // Unblock FIFO before joining
    LOGD("\nUnblocking SHM-FIFO now...\n");
    shm_fifo_set_blocking_mode(shm_fifo_ctx, 0, LOG_CTX_GET());

    // Test overflow (unlocked)
    LOGD("\nTest unlocked FIFO overflow...\n");
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, "abcdefghijklmno\0", 16, LOG_CTX_GET()) == 0);
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, "fail\0", 5, LOG_CTX_GET()) == ENOMEM);

    // Test underrun (unlocked)
    LOGD("\nTest unlocked FIFO underrun...\n");
    shm_fifo_empty(shm_fifo_ctx, LOG_CTX_GET());
    ASSERT_TRUE(shm_fifo_pull(shm_fifo_ctx, (void**)&read_buf, &read_buf_size, -1, LOG_CTX_GET()) == EAGAIN);
    if(read_buf != NULL)
    {
        free(read_buf);
        read_buf = NULL;
    }

    // Test timeout (unlocked)
    LOGD("\nTest timeout...\n");
    shm_fifo_set_blocking_mode(shm_fifo_ctx, 1, LOG_CTX_GET()); // Set blocking mode
    shm_fifo_empty(shm_fifo_ctx, LOG_CTX_GET());
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, "abcdefghijklmno\0", 16, LOG_CTX_GET()) == 0);
    ASSERT_TRUE(shm_fifo_pull(shm_fifo_ctx, (void**)&read_buf, &read_buf_size, 1000*1000 /*1 second*/, LOG_CTX_GET())
            == 0);
    if(read_buf != NULL)
    {
        free(read_buf);
        read_buf = NULL;
    }
    ASSERT_TRUE(shm_fifo_pull(shm_fifo_ctx, (void**)&read_buf, &read_buf_size, 1000*1000 /*1 second*/, LOG_CTX_GET())
            == ETIMEDOUT);

    // Test 'shm_fifo_get_buffer_level()'
    LOGD("\nTest 'shm_fifo_get_buffer_level()'...\n");
    shm_fifo_set_blocking_mode(shm_fifo_ctx, 1, LOG_CTX_GET()); // Set blocking mode
    shm_fifo_empty(shm_fifo_ctx, LOG_CTX_GET());
    ASSERT_TRUE(shm_fifo_get_buffer_level(shm_fifo_ctx, LOG_CTX_GET()) == 0);
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, "abcdefghijklmno\0", 16, LOG_CTX_GET()) == 0);
    ASSERT_TRUE(shm_fifo_get_buffer_level(shm_fifo_ctx, LOG_CTX_GET()) == (16 + sizeof(ssize_t)));
    shm_fifo_empty(shm_fifo_ctx, LOG_CTX_GET());

    // Test rest of bad arguments... (add coverage).
    // Note that 'read_buf' is not freed as calls to 'shm_fifo_pull' always fail.
    LOGD("\nTest bad arguments at 'shm_fifo_pull()'...\n");
    ASSERT_TRUE(shm_fifo_pull(NULL, (void**)&read_buf, &read_buf_size, -1, LOG_CTX_GET()) == -1);
    ASSERT_TRUE(shm_fifo_pull(shm_fifo_ctx, (void**)NULL, &read_buf_size, -1, LOG_CTX_GET()) == -1);
    ASSERT_TRUE(shm_fifo_pull(shm_fifo_ctx, (void**)&read_buf, NULL, -1, LOG_CTX_GET()) == -1);

    LOGD("\nTest bad arguments at 'shm_fifo_push()'...\n");
    ASSERT_TRUE(shm_fifo_push(NULL, "abcdefghijklmno\0", 16, LOG_CTX_GET()) == -1);
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, NULL, 16, LOG_CTX_GET()) == -1);
    ASSERT_TRUE(shm_fifo_push(shm_fifo_ctx, "abcdefghijklmno\0", 0, LOG_CTX_GET()) == -1);

    LOGD("\nTest bad arguments at 'fifo_empty()'...\n");
    shm_fifo_empty(NULL, LOG_CTX_GET());

    LOGD("\nTest bad arguments at 'shm_fifo_get_buffer_level()'...\n");
    ASSERT_TRUE(shm_fifo_get_buffer_level(NULL, LOG_CTX_GET()) == -1);

    LOGD("\nTest bad arguments at 'fifo_set_blocking_mode()'...\n");
    shm_fifo_set_blocking_mode(NULL, 0, LOG_CTX_GET());

    LOGD("\nTest bad arguments at 'shm_fifo_open()'...\n");
    ASSERT_TRUE(shm_fifo_open(NULL, LOG_CTX_GET()) == NULL);

    LOGD("\nTest bad arguments at 'shm_fifo_close()'...\n");
    shm_fifo_close(NULL, LOG_CTX_GET());
    shm_fifo_ctx_t *fifo_ctx_bad = NULL;
    shm_fifo_close(&fifo_ctx_bad, LOG_CTX_GET());

    LOGD("\nTest bad arguments at 'shm_fifo_create()'...\n");
    ASSERT_TRUE(shm_fifo_create(NULL, shm_pool_size, 0, LOG_CTX_GET()) == NULL);
    ASSERT_TRUE(shm_fifo_create("/anyname", 0, 0, LOG_CTX_GET()) == NULL);
    char bad_length_name[1024];
    memset(bad_length_name, 'F', sizeof(bad_length_name) - 1);
    bad_length_name[sizeof(bad_length_name) - 1] = '\0';
    ASSERT_TRUE(shm_fifo_create(bad_length_name, shm_pool_size, 0, LOG_CTX_GET()) == NULL);

    shm_fifo_release(&shm_fifo_ctx, LOG_CTX_GET());

    utils_logs_close(&utils_logs_ctx);
}

