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
 * @file bitparser.c
 * @author Rafael Antoniello
 */

#include "bitparser.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <endian.h>

#include "log.h"
#include "stat_codes.h"
#include "check_utils.h"

/* **** Definitions **** */

//#define ENABLE_LOGS //uncomment to trace logs
#ifndef ENABLE_LOGS
#undef LOG
#define LOG(...) // none
#endif

#define SHL(x, y)	( ((x) << (y)) )
#define SHR(x, y)	( ((y) & DPATHW) ? 0 : ((x) >> (y)) )

#define GLBIC_ENDIAN

#ifdef LITTLEENDIAN
#ifdef GLBIC_ENDIAN
#define SWAP2(x)	be16toh(x)
#define SWAP4(x)	be32toh(x)
#define SWAP8(x)	be64toh(x)
#else
#define SWAP2(x)	( (((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8) )
#define SWAP4(x)	(((x) >> 24)|(((x) & 0x00ff0000) >> 8)| \
                    (((x) & 0x0000ff00) << 8)|(((x) & 0x000000ff) << 24))
#define SWAP8(x) 	something... //TODO
#endif
#else
#define SWAP2(x) 	(x)
#define SWAP4(x) 	(x)
#define SWAP8(x) 	(x)
#endif

#define IS_POW_OF_2(x) (\
		((x)!= 0)&& (((x)& ((x)- 1))== 0)\
		)

/* **** Implementations **** */

bitparser_ctx_t* bitparser_open(void *buf, size_t buf_size)
{
	register WORD_T word0;
	int end_code= STAT_ERROR;
	bitparser_ctx_t *bitparser_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments:
	 * - Buffer size MUST be multiple of 8 bytes
	 */
	CHECK_DO(buf!= NULL, return NULL);
	CHECK_DO(buf_size> 0 && SIZE_IS_MULTIPLE(buf_size, sizeof(WORD_T)),
			return NULL);

	/* Allocate context structure */
	bitparser_ctx= (bitparser_ctx_t*)calloc(1, sizeof(bitparser_ctx_t));
	CHECK_DO(bitparser_ctx!= NULL, goto end);

	/* Initialize context structure members */
	bitparser_ctx->buf= (WORD_T*)buf;
	bitparser_ctx->buf_size= buf_size;
	bitparser_ctx->bcnt= 0;
	bitparser_ctx->word0= word0= SWAPW(((WORD_T*)buf)[0]);
	bitparser_ctx->word1= SWAPW(((WORD_T*)buf)[1]);
	bitparser_ctx->top= word0;

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		bitparser_close(&bitparser_ctx);
	return bitparser_ctx;
}

void bitparser_close(bitparser_ctx_t **ref_bitparser_ctx)
{
	bitparser_ctx_t *bitparser_ctx;
	//LOG_CTX_INIT(NULL);

	if(ref_bitparser_ctx== NULL)
		return;

	if((bitparser_ctx= *ref_bitparser_ctx)!= NULL) {
		free(bitparser_ctx);
		*ref_bitparser_ctx= NULL;
	}
}

void bitparser_flush(bitparser_ctx_t* bitparser_ctx, size_t n)
{
	register WORD_T buf_size, bcnt, bcnt_new, wcnt, wcnt_new, word0, word1,
		wcnt_max;
	register const WORD_T *buf;
	LOG_CTX_INIT(NULL);

	CHECK_DO(bitparser_ctx!= NULL, return);

	buf= bitparser_ctx->buf;
	buf_size= bitparser_ctx->buf_size;
	bcnt= bitparser_ctx->bcnt;
	wcnt= bcnt>> DPATHW_SHIFTb;
	bcnt_new= bcnt+ n;
	bitparser_ctx->bcnt= bcnt_new;
	wcnt_new= bcnt_new>> DPATHW_SHIFTb;
	word0= bitparser_ctx->word0;
	word1= bitparser_ctx->word1;
	wcnt_max= buf_size>> DPATHW_SHIFTB;
	CHECK_DO(wcnt_new<= wcnt_max, return);

	if(wcnt_new!= wcnt) {
		bitparser_ctx->word0= word0= SWAPW(buf[wcnt_new% wcnt_max]);
		bitparser_ctx->word1= word1= SWAPW(buf[(wcnt_new+ 1)% wcnt_max]);
	}

	/* Notes:
	 * - Endianess requires "swapping" word0/1;
	 * - Buffer may read up to two words out-of-bounds circularly (harmless).
	 */
	bitparser_ctx->top= SHL(word0, bcnt_new&(DPATHW-1))+
			SHR(word1, DPATHW- (bcnt_new&(DPATHW-1)));
}

WORD_T bitparser_get(bitparser_ctx_t* bitparser_ctx, size_t n)
{
	WORD_T bits_value;
	LOG_CTX_INIT(NULL);

	CHECK_DO(bitparser_ctx!= NULL, return 0);

	bits_value= (bitparser_ctx->top>> (DPATHW-n));
	bitparser_flush(bitparser_ctx, n);
	return bits_value;
}

WORD_T bitparser_show(bitparser_ctx_t* bitparser_ctx, size_t n)
{
	WORD_T bits_value;
	LOG_CTX_INIT(NULL);

	CHECK_DO(bitparser_ctx!= NULL, return 0);

	bits_value= (bitparser_ctx->top>> (DPATHW-n));
	return bits_value;
}

void* bitparser_copy_bytes(bitparser_ctx_t* bitparser_ctx, size_t cnt)
{
	register WORD_T bytecnt, buf_size, avail_size;
	const uint8_t *pbuf= (uint8_t*)bitparser_ctx->buf;
	void *p_ret= NULL;
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	CHECK_DO(bitparser_ctx!= NULL, return NULL);
	CHECK_DO(cnt> 0, return NULL);

	bytecnt= bitparser_ctx->bcnt>> 3;
	buf_size= bitparser_ctx->buf_size;
	CHECK_DO(bytecnt< buf_size, goto end);
	avail_size= buf_size- bytecnt;
	CHECK_DO(cnt<= avail_size, goto end);

	p_ret= malloc(EXTEND_SIZE_TO_MULTIPLE(cnt, sizeof(WORD_T)));
	CHECK_DO(p_ret!= NULL, goto end);

	memcpy(p_ret, pbuf+ bytecnt, cnt);
	bitparser_flush(bitparser_ctx, cnt<< 3);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		if(p_ret!= NULL)
			free(p_ret);
	return p_ret;
}

void bitparser_align_2byte(bitparser_ctx_t* bitparser_ctx)
{
	register WORD_T bits_unaligned, bits2flush;
	LOG_CTX_INIT(NULL);

	CHECK_DO(bitparser_ctx!= NULL, return);

	bits_unaligned= bitparser_ctx->bcnt& 7;
	bits2flush= bits_unaligned? 8- bits_unaligned: 0;

	if(bits_unaligned> 0)
		bitparser_flush(bitparser_ctx, bits2flush);
}
