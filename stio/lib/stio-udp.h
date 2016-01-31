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


#ifndef _STIO_UDP_H_
#define _STIO_UDP_H_

#include <stio.h>

typedef struct stio_dev_udp_t stio_dev_udp_t;

typedef int (*stio_dev_udp_on_recv_t) (stio_dev_udp_t* dev, const void* data, stio_len_t len);
typedef int (*stio_dev_udp_on_sent_t) (stio_dev_udp_t* dev, void* sendctx);

typedef struct stio_dev_udp_make_t stio_dev_udp_make_t;
struct stio_dev_udp_make_t
{
	/* TODO: options: REUSEADDR for binding?? */
	stio_sckadr_t addr; /* binding address. */
	stio_dev_udp_on_sent_t on_sent;
	stio_dev_udp_on_recv_t on_recv;
};

struct stio_dev_udp_t
{
	STIO_DEV_HEADERS;
	stio_sckhnd_t sck;
	stio_sckadr_t peer;

	stio_dev_udp_on_sent_t on_sent;
	stio_dev_udp_on_recv_t on_recv;
};

#ifdef __cplusplus
extern "C" {
#endif


STIO_EXPORT stio_dev_udp_t* stio_dev_udp_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_udp_make_t* data
);

STIO_EXPORT void stio_dev_udp_kill (
	stio_dev_udp_t* udp
);

#ifdef __cplusplus
}
#endif

#endif
