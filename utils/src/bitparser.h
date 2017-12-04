/*
 * Copyright (c) 2015, 2016, 2017, 2018 Rafael Antoniello
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
 * @file bitparser.h
 * @brief Bit parsing module utility.
 * @author Rafael Antoniello
 */

#ifndef SPUTIL_SRC_BITPARSER_H_
#define SPUTIL_SRC_BITPARSER_H_

#include <sys/types.h>
#include <inttypes.h>
#include "mem_utils.h"

/* **** Definitions **** */

/**
 * Endianess: define either little (un-comment definition below) or
 * big endian (comment definition).
 */
#define LITTLEENDIAN

typedef struct bitparser_ctx_s {
	/**
	 * Pointer to parsing buffer.
	 */
	WORD_T *buf;
	/**
	 * Parsing buffer size in bytes.
	 */
	size_t buf_size;
	/**
	 * Bit position within parsing buffer (bit-counter).
	 */
	WORD_T bcnt;
	/**
	 * Word-aligned 32/64-bit window, corresponding to bit-counter position.
	 */
	WORD_T word0;
	/**
	 * Next word to 'word0'.
	 */
	WORD_T word1;
	/**
	 * Top 32/64 bits of bitstream (bit-aligned 32/64-bit window,
	 * corresponding to bit-counter position).
	 */
	WORD_T top;
} bitparser_ctx_t;

/* **** Prototypes **** */

/**
 * //FIXME!!
 * Initializes the bit-parser context attributes, getting ready for parsing.
 * @param bitparser_ctx Pointer to bit-parser context structure
 * @param buf Pointer to buffer to be parsed.
 * @param buf_size Parsing buffer size in bytes. Value MUST be a multiple of 8.
 * @return Status code (refer to 'stat_codes_ctx_t' type).
 * @see stat_codes_ctx_t
 */
bitparser_ctx_t* bitparser_open(void *buf, size_t buf_size);

/**
 * // FIXME!!
 */
void bitparser_close(bitparser_ctx_t **ref_bitparser_ctx);

/**
 * Flush next n bits out of buffer (Advance internal bit-counter by 'n' bits).
 * @param bitparser_ctx Pointer to bit-parser context structure
 * @param n number of bits to flush.
 */
void bitparser_flush(bitparser_ctx_t* bitparser_ctx, size_t n);

/**
 * Get next n bits right aligned in an unsigned 32/64-bit word. Bits are
 * "flushed" from internal buffer, incrementing internal bit counter.
 * @param bitparser_ctx Pointer to bit-parser context structure
 * @param n Number of bits to get (internal bit counter is incremented by 'n')
 * @return Unsigned 32/64-bit word showing next n bits right aligned (stream
 * bits are ordered from left to right).
 */
WORD_T bitparser_get(bitparser_ctx_t* bitparser_ctx, size_t n);

/**
 * Show next n bits right aligned in an unsigned 32/64-bit word. Internal
 * buffer bit counter is not modified (namely, consecutive identical calls to
 * this function will return the same value).
 * @param bitparser_ctx Pointer to bit-parser context structure
 * @param n Number of bits to show
 * @return Unsigned 32/64-bit word showing next n bits right aligned (stream
 * bits are ordered from left to right).
 */
WORD_T bitparser_show(bitparser_ctx_t* bitparser_ctx, size_t n);

/**
 * Copy 'cnt' bytes from bit-parser internal buffer to a dynamically
 * (heap) allocated memory. Important: note that this function requires copy
 * operation to be byte aligned; thus, this function copies the byte block
 * corresponding to the current value of the internal bit-counter.
 * @param bitparser_ctx Pointer to bit-parser context structure
 * @param cnt Number of bytes to copy
 * @return Pointer to the copy allocation. The allocation should be freed later
 * by the caller.
 */
void* bitparser_copy_bytes(bitparser_ctx_t* bitparser_ctx, size_t cnt);

/**
 * Align bit parser to the next byte boundary
 * @param bitparser_ctx Pointer to bit-parser context structure.
 */
void bitparser_align_2byte(bitparser_ctx_t* bitparser_ctx);

#endif /* SPUTIL_SRC_BITPARSER_H_ */
