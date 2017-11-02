/*
 * Copyright (c) 2017 Rafael Antoniello
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
 * @file stat_codes.h
 * @brief General status codes enumeration.
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_STAT_CODES_H_
#define UTILS_SRC_STAT_CODES_H_

typedef enum stat_codes_ctx_s {
    STAT_SUCCESS= 0,
	STAT_NOTMODIFIED, //< Resource requested found but not modified
	STAT_ERROR,
	STAT_ENOTFOUND, //< Resource requested not found
	STAT_EAGAIN, //< Resource temporarily unavailable (call again)
	STAT_EOF, //< End of file
	STAT_ENOMEM, //< Not enough space
	STAT_EINVAL, //< Invalid argument
	STAT_ECONFLICT, //< Conflict with the current state of the target resource
	STAT_EBAVFORMAT, //< Bad or not supported audio/video format
	STAT_EBMUXFORMAT, //< Bad or not supported multiplex format
	STAT_ETIMEDOUT, //< Operation timed out
	STAT_EINTR, //< Operation interrupted
	STAT_CODES_MAX
} stat_codes_ctx_t;

const char* stat_codes_get_description(stat_codes_ctx_t code);

#endif /* UTILS_SRC_STAT_CODES_H_ */
