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
 * @file mem_utils.h
 * @author Rafael Antoniello
 */

#ifndef UTILS_SRC_MEM_UTILS_H_
#define UTILS_SRC_MEM_UTILS_H_

#include <sys/types.h>
#include <inttypes.h>

/* **** Definitions **** */

/**
 * Byte-size alignment constant used for accessing structures and
 * sub-structures.
 */
#define CTX_S_BASE_ALIGN sizeof(uint64_t)

/**
 * Extend SIZE to a multiple of MULTIPLE.
 */
#define EXTEND_SIZE_TO_MULTIPLE(SIZE, MULTIPLE) \
	(\
		(SIZE)% MULTIPLE? (SIZE)+ (MULTIPLE- ((SIZE)% MULTIPLE)): (SIZE)\
	)

/**
 * Boolean: is SIZE multiple of MULTIPLE?
 */
#define SIZE_IS_MULTIPLE(SIZE, MULTIPLE) (((SIZE)% MULTIPLE)== 0)

#endif /* UTILS_SRC_MEM_UTILS_H_ */
