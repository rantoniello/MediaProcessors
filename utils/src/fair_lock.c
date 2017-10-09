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
 * @file fair_lock.c
 */

#include "fair_lock.h"

#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "check_utils.h"
#include "log.h"
#include "stat_codes.h"

/* **** Definitions **** */

typedef struct fair_lock_s {
	volatile unsigned long head, tail;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} fair_lock_t;

/* **** Prototypes **** */

/* **** Implementations **** */

fair_lock_t* fair_lock_open()
{
	int ret_code, end_code= STAT_ERROR;
	fair_lock_t *fair_lock= NULL;
	LOG_CTX_INIT(NULL);

	/* Allocate context structure */
	fair_lock= (fair_lock_t*)calloc(1, sizeof(fair_lock_t));
	CHECK_DO(fair_lock!= NULL, goto end);

	/* Initialize context structure */
	ret_code= pthread_mutex_init(&fair_lock->mutex, NULL);
	CHECK_DO(ret_code== 0, goto end);
	ret_code= pthread_cond_init(&fair_lock->cond, NULL);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		fair_lock_close(&fair_lock);
	return fair_lock;
}

void fair_lock_close(fair_lock_t **ref_fair_lock)
{
	fair_lock_t *fair_lock;

	if(ref_fair_lock== NULL)
		return;

	if((fair_lock= *ref_fair_lock)!= NULL) {
		pthread_mutex_destroy(&fair_lock->mutex);
		pthread_cond_destroy(&fair_lock->cond);

		free(fair_lock);
		*ref_fair_lock= NULL;
	}
}

void fair_lock(fair_lock_t *fair_lock)
{
#ifndef USE_PTHREAD_MUTEX_ONLY
    unsigned long queue;
#endif
	LOG_CTX_INIT(NULL);

    /* Check arguments */
    CHECK_DO(fair_lock!= NULL, return);

#ifndef USE_PTHREAD_MUTEX_ONLY
    pthread_mutex_lock(&fair_lock->mutex);
    queue= fair_lock->tail++;
    while(queue!= fair_lock->head)
    	pthread_cond_wait(&fair_lock->cond, &fair_lock->mutex);
    pthread_mutex_unlock(&fair_lock->mutex);
#else
    pthread_mutex_lock(&fair_lock->mutex);
#endif
}

void fair_unlock(fair_lock_t *fair_lock)
{
	LOG_CTX_INIT(NULL);

    /* Check arguments */
    CHECK_DO(fair_lock!= NULL, return);

#ifndef USE_PTHREAD_MUTEX_ONLY
    pthread_mutex_lock(&fair_lock->mutex);
    fair_lock->head++;
    pthread_cond_broadcast(&fair_lock->cond);
    pthread_mutex_unlock(&fair_lock->mutex);
#else
    pthread_mutex_unlock(&fair_lock->mutex);
#endif
}
