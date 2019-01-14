/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MIO_PRV_H_
#define _MIO_PRV_H_

#include "mio.h"

/*TODO: redefine and remove these */
#include <assert.h>
#include <string.h>
#include <stdio.h>
/*TODO: redefine these */
#define MIO_MEMSET(dst,byte,count) memset(dst,byte,count)
#define MIO_MEMCPY(dst,src,count) memcpy(dst,src,count)
#define MIO_MEMMOVE(dst,src,count) memmove(dst,src,count)
#define MIO_MEMCMP(dst,src,count) memcmp(dst,src,count)
#define MIO_ASSERT assert

typedef struct mio_mux_t mio_mux_t;

struct mio_t
{
	mio_mmgr_t* mmgr;
	mio_errnum_t errnum;
	mio_stopreq_t stopreq;  /* stop request to abort mio_loop() */

	struct
	{
		mio_dev_t* head;
		mio_dev_t* tail;
	} actdev; /* active devices */

	struct
	{
		mio_dev_t* head;
		mio_dev_t* tail;
	} hltdev; /* halted devices */

	struct
	{
		mio_dev_t* head;
		mio_dev_t* tail;
	} zmbdev; /* zombie devices */

	mio_uint8_t bigbuf[65535]; /* TODO: make this dynamic depending on devices added. device may indicate a buffer size required??? */

	unsigned int renew_watch: 1;
	unsigned int in_exec: 1;

	struct
	{
		mio_oow_t     capa;
		mio_oow_t     size;
		mio_tmrjob_t*  jobs;
	} tmr;

	mio_cwq_t cwq;
	mio_cwq_t* cwq_zdf; /* list of free cwq objects with 0-sized dstaddr */

	/* platform specific fields below */
#if defined(_WIN32)
	HANDLE iocp;
#else
	mio_mux_t* mux;
#endif
};


#define MIO_EPOCH_YEAR  (1970)
#define MIO_EPOCH_MON   (1)
#define MIO_EPOCH_DAY   (1)
#define MIO_EPOCH_WDAY  (4)

/* windows specific epoch time */
#define MIO_EPOCH_YEAR_WIN   (1601)
#define MIO_EPOCH_MON_WIN    (1)
#define MIO_EPOCH_DAY_WIN    (1)

#define MIO_DAYS_PER_WEEK  (7)
#define MIO_MONS_PER_YEAR  (12)
#define MIO_HOURS_PER_DAY  (24)
#define MIO_MINS_PER_HOUR  (60)
#define MIO_MINS_PER_DAY   (MIO_MINS_PER_HOUR*MIO_HOURS_PER_DAY)
#define MIO_SECS_PER_MIN   (60)
#define MIO_SECS_PER_HOUR  (MIO_SECS_PER_MIN*MIO_MINS_PER_HOUR)
#define MIO_SECS_PER_DAY   (MIO_SECS_PER_MIN*MIO_MINS_PER_DAY)

#ifdef __cplusplus
extern "C" {
#endif

int mio_makesyshndasync (
	mio_t*       mio,
	mio_syshnd_t hnd
);

mio_errnum_t mio_syserrtoerrnum (
	int no
);


mio_bch_t* mio_mbsdup (
	mio_t*             mio,
	const mio_bch_t* src
);

mio_oow_t mio_mbscpy (
	mio_bch_t*       buf,
	const mio_bch_t* str
);

int mio_mbsspltrn (
	mio_bch_t*       s,
	const mio_bch_t* delim,
	mio_bch_t        lquote,
	mio_bch_t        rquote, 
	mio_bch_t        escape,
	const mio_bch_t* trset
);

int mio_mbsspl (
	mio_bch_t*       s,
	const mio_bch_t* delim,
	mio_bch_t        lquote,
	mio_bch_t        rquote,
	mio_bch_t        escape
);

void mio_cleartmrjobs (
	mio_t* mio
);

void mio_firetmrjobs (
	mio_t*             mio,
	const mio_ntime_t* tmbase,
	mio_oow_t*        firecnt
);


int mio_gettmrtmout (
	mio_t*             mio,
	const mio_ntime_t* tmbase,
	mio_ntime_t*       tmout
);

#ifdef __cplusplus
}
#endif


#endif
