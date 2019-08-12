//
// Copyright (c) Telefonica I+D. All rights reserved.
//

#ifndef SRC_UNIT_TESTS_SHM_FIFO_H_
#define SRC_UNIT_TESTS_SHM_FIFO_H_

#include <cstddef>

#define UNIT_TESTS_SHM_FIFO_MESSAGE_MAX_LEN 16

const char *messages_list_1[]=
{
        "Hello, world!.\0",
        "How are you?.\0",
        "abcdefghijklmno\0", // Test maximum length message (UNIT_TESTS_SHM_FIFO_MESSAGE_MAX_LEN)
        "123456789\0",
        "__ABCD__1234_\0",
        "_            _\0",
        "_/)=:;.\"·#{+]\0",
        "{\"key\":\"val\"}\0",
        "Goodbye.\0",
        NULL
};

const char *messages_list_2[]=
{
        "IIIIIIIIIIIIIIIII\0", // Exceed maximum lenght -fail to push-
        NULL
};

#endif /* SRC_UNIT_TESTS_SHM_FIFO_H_ */
