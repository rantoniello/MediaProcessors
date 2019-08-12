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
 * @file utests_fifo_common.h
 */

#ifndef UTILS_UTESTS_UTESTS_FIFO_COMMON_H_
#define UTILS_UTESTS_UTESTS_FIFO_COMMON_H_

#include <cstddef>

#define UTESTS_FIFO_SHM_FILENAME "/fifo_shm_utest"
#define UTESTS_FIFO_MESSAGE_MAX_LEN 17

const char *utests_fifo_messages_list_1[]=
{
        "Hello, world!.\0",
        "How are you?.\0",
        "abcdefghijklmnop\0", /* Test max length UTESTS_FIFO_MESSAGE_MAX_LEN */
        "123456789\0",
        "__ABCD__1234_\0",
        "_            _\0",
        "_/)=:;.\"·#{+]\0",
        "{\"key\":\"val\"}\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"\0",
		"#\0",
		"##\0",
		"###\0",
		"####\0",
		"#####\0",
		"######\0",
		"#######\0",
		"########\0",
		"#########\0",
		"##########\0",
		"###########\0",
		"############\0",
		"#############\0",
		"##############\0",
		"###############\0",
		"################\0", /* Test max length UTESTS_FIFO_MESSAGE_MAX_LEN */
        "Goodbye.\0",
        NULL
};

const char *utests_fifo_messages_list_2[]=
{
        "IIIIIIIIIIIIIIIIIIIIIIIII\0", // Exceed maximum length -fail to push-
        NULL
};

#endif /* UTILS_UTESTS_UTESTS_FIFO_COMMON_H_ */
