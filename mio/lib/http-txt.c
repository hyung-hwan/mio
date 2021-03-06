/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

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
#include "http-prv.h"
#include <mio-fmt.h>
#include <mio-chr.h>

#define TXT_OVER_READ_FROM_CLIENT (1 << 0)
#define TXT_OVER_WRITE_TO_CLIENT  (1 << 1)
#define TXT_OVER_ALL (TXT_OVER_READ_FROM_CLIENT | TXT_OVER_WRITE_TO_CLIENT)

struct txt_t
{
	MIO_SVC_HTTS_RSRC_HEADER;

	mio_oow_t num_pending_writes_to_client;
	mio_svc_htts_cli_t* client;
	mio_http_version_t req_version; /* client request */

	unsigned int over: 2; /* must be large enough to accomodate TXT_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int client_disconnected: 1;
	unsigned int client_htrd_recbs_changed: 1;
	mio_oow_t req_content_length; /* client request content length */

	mio_dev_sck_on_read_t client_org_on_read;
	mio_dev_sck_on_write_t client_org_on_write;
	mio_dev_sck_on_disconnect_t client_org_on_disconnect;
	mio_htrd_recbs_t client_htrd_org_recbs;
};
typedef struct txt_t txt_t;

static void txt_halt_participating_devices (txt_t* txt)
{
	MIO_ASSERT (txt->client->htts->mio, txt->client != MIO_NULL);
	MIO_ASSERT (txt->client->htts->mio, txt->client->sck != MIO_NULL);
	MIO_DEBUG3 (txt->client->htts->mio, "HTTS(%p) - Halting participating devices in txt state %p(client=%p)\n", txt->client->htts, txt, txt->client->sck);
	mio_dev_sck_halt (txt->client->sck);
}

static int txt_write_to_client (txt_t* txt, const void* data, mio_iolen_t dlen)
{
	txt->num_pending_writes_to_client++;
	if (mio_dev_sck_write(txt->client->sck, data, dlen, MIO_NULL, MIO_NULL) <= -1) 
	{
		txt->num_pending_writes_to_client--;
		return -1;
	}
	return 0;
}

#if 0
static int txt_writev_to_client (txt_t* txt, mio_iovec_t* iov, mio_iolen_t iovcnt)
{
	txt->num_pending_writes_to_client++;
	if (mio_dev_sck_writev(txt->client->sck, iov, iovcnt, MIO_NULL, MIO_NULL) <= -1) 
	{
		txt->num_pending_writes_to_client--;
		return -1;
	}
	return 0;
}
#endif

static int txt_send_final_status_to_client (txt_t* txt, int status_code, const char* content_type, const char* content_text, int force_close)
{
	mio_svc_htts_cli_t* cli = txt->client;
	mio_bch_t dtbuf[64];
	mio_oow_t content_text_len = 0;

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (!force_close) force_close = !txt->keep_alive;

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: %hs\r\n",
		txt->req_version.major, txt->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf,
		(force_close? "close": "keep-alive"),
		(content_text? mio_count_bcstr(content_text): 0), (content_text? content_text: "")) == (mio_oow_t)-1) return -1;

	if (content_text)
	{
		content_text_len = mio_count_bcstr(content_text);
		if (content_type && mio_becs_fcat(cli->sbuf, "Content-Type: %hs\r\n", content_type) == (mio_oow_t)-1) return -1;
	}
	if (mio_becs_fcat(cli->sbuf, "Content-Length: %zu\r\n\r\n", content_text_len) == (mio_oow_t)-1) return -1;

	return (txt_write_to_client(txt, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        (content_text && txt_write_to_client(txt, content_text, content_text_len) <= -1) ||
	        (force_close && txt_write_to_client(txt, MIO_NULL, 0) <= -1))? -1: 0;
}

static MIO_INLINE void txt_mark_over (txt_t* txt, int over_bits)
{
	unsigned int old_over;

	old_over = txt->over;
	txt->over |= over_bits;

	MIO_DEBUG4 (txt->htts->mio, "HTTS(%p) - client=%p new-bits=%x over=%x\n", txt->htts, txt->client->sck, (int)over_bits, (int)txt->over);

	if (!(old_over & TXT_OVER_READ_FROM_CLIENT) && (txt->over & TXT_OVER_READ_FROM_CLIENT))
	{
		if (mio_dev_sck_read(txt->client->sck, 0) <= -1) 
		{
			MIO_DEBUG2 (txt->htts->mio, "HTTS(%p) - halting client(%p) for failure to disable input watching\n", txt->htts, txt->client->sck);
			mio_dev_sck_halt (txt->client->sck);
		}
	}

	if (old_over != TXT_OVER_ALL && txt->over == TXT_OVER_ALL)
	{
		/* ready to stop */
		if (txt->keep_alive) 
		{
			/* how to arrange to delete this txt object and put the socket back to the normal waiting state??? */
			MIO_ASSERT (txt->htts->mio, txt->client->rsrc == (mio_svc_htts_rsrc_t*)txt);

printf ("DETACHING FROM THE MAIN CLIENT RSRC... state -> %p\n", txt->client->rsrc);
			MIO_SVC_HTTS_RSRC_DETACH (txt->client->rsrc);
			/* txt must not be access from here down as it could have been destroyed */
		}
		else
		{
			MIO_DEBUG2 (txt->htts->mio, "HTTS(%p) - halting client(%p) for no keep-alive\n", txt->htts, txt->client->sck);
			mio_dev_sck_shutdown (txt->client->sck, MIO_DEV_SCK_SHUTDOWN_WRITE);
			mio_dev_sck_halt (txt->client->sck);
		}
	}
}

static void txt_on_kill (txt_t* txt)
{
	mio_t* mio = txt->htts->mio;

	MIO_DEBUG2 (mio, "HTTS(%p) - killing txt client(%p)\n", txt->htts, txt->client->sck);

	if (txt->client_org_on_read)
	{
		txt->client->sck->on_read = txt->client_org_on_read;
		txt->client_org_on_read = MIO_NULL;
	}

	if (txt->client_org_on_write)
	{
		txt->client->sck->on_write = txt->client_org_on_write;
		txt->client_org_on_write = MIO_NULL;
	}

	if (txt->client_org_on_disconnect)
	{
		txt->client->sck->on_disconnect = txt->client_org_on_disconnect;
		txt->client_org_on_disconnect = MIO_NULL;
	}

	if (txt->client_htrd_recbs_changed)
	{
		/* restore the callbacks */
		mio_htrd_setrecbs (txt->client->htrd, &txt->client_htrd_org_recbs); 
	}

	if (!txt->client_disconnected)
	{
/*printf ("ENABLING INPUT WATCHING on CLIENT %p. \n", txt->client->sck);*/
		if (!txt->keep_alive || mio_dev_sck_read(txt->client->sck, 1) <= -1)
		{
			MIO_DEBUG2 (mio, "HTTS(%p) - halting client(%p) for failure to enable input watching\n", txt->htts, txt->client->sck);
			mio_dev_sck_halt (txt->client->sck);
		}
	}

/*printf ("**** TXT_ON_KILL DONE\n");*/
}

static int txt_client_htrd_poke (mio_htrd_t* htrd, mio_htre_t* req)
{
	/* client request got completed */
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn = (mio_svc_htts_cli_htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->rsrc;

printf (">> CLIENT REQUEST COMPLETED\n");

	txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
	return 0;
}

static int txt_client_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	/* discard all contents */
	return 0;
}

static mio_htrd_recbs_t txt_client_htrd_recbs =
{
	MIO_NULL,
	txt_client_htrd_poke,
	txt_client_htrd_push_content
};

static void txt_client_on_disconnect (mio_dev_sck_t* sck)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->rsrc;
	txt->client_disconnected = 1;
	txt->client_org_on_disconnect (sck);
}

static int txt_client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->rsrc;

	MIO_ASSERT (mio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		MIO_DEBUG2 (cli->htts->mio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", txt->client->htts, sck, (int)sck->hnd);

		if (!(txt->over & TXT_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without txt_client_htrd_poke() */
		{
			txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		mio_oow_t rem;

		MIO_ASSERT (mio, !(txt->over & TXT_OVER_READ_FROM_CLIENT));

		if (mio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
printf ("UUUUUUUUUUUUUUUUUUUUUUUUUUGGGGGHHHHHHHHHHHH .......... TXT CLIENT GIVING EXCESSIVE DATA AFTER CONTENTS...\n");
		}
	}

	return 0;

oops:
	txt_halt_participating_devices (txt);
	return 0;
}

static int txt_client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	txt_t* txt = (txt_t*)cli->rsrc;

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->mio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		txt->num_pending_writes_to_client--;
		MIO_ASSERT (mio, txt->num_pending_writes_to_client == 0);
		MIO_DEBUG3 (mio, "HTTS(%p) - indicated EOF to client %p(%d)\n", txt->client->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		txt_mark_over (txt, TXT_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		MIO_ASSERT (mio, txt->num_pending_writes_to_client > 0);
		txt->num_pending_writes_to_client--;
		if (txt->num_pending_writes_to_client <= 0)
		{
			txt_mark_over (txt, TXT_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	txt_halt_participating_devices (txt);
	return 0;
}

int mio_svc_htts_dotxt (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, int status_code, const mio_bch_t* content_type, const mio_bch_t* content_text)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	txt_t* txt = MIO_NULL;

	/* ensure that you call this function before any contents is received */
	MIO_ASSERT (mio, mio_htre_getcontentlen(req) == 0);

	txt = (txt_t*)mio_svc_htts_rsrc_make(htts, MIO_SIZEOF(*txt), txt_on_kill);
	if (MIO_UNLIKELY(!txt)) goto oops;

	txt->client = cli;
	/*txt->num_pending_writes_to_client = 0;*/
	txt->req_version = *mio_htre_getversion(req);
	txt->req_content_length_unlimited = mio_htre_getreqcontentlen(req, &txt->req_content_length);

	txt->client_org_on_read = csck->on_read;
	txt->client_org_on_write = csck->on_write;
	txt->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = txt_client_on_read;
	csck->on_write = txt_client_on_write;
	csck->on_disconnect = txt_client_on_disconnect;

	MIO_ASSERT (mio, cli->rsrc == MIO_NULL);
	MIO_SVC_HTTS_RSRC_ATTACH (txt, cli->rsrc);

	if (req->flags & MIO_HTRE_ATTR_EXPECT100)
	{
		/* don't send 100-Continue. If the client posts data regardless, ignore them later */
	}
	else if (req->flags & MIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		txt_send_final_status_to_client(txt, 417, MIO_NULL, MIO_NULL, 1);
		goto oops;
	}

	if (txt->req_content_length_unlimited || txt->req_content_length > 0)
	{
		/* change the callbacks to subscribe to contents to be uploaded */
		txt->client_htrd_org_recbs = *mio_htrd_getrecbs(txt->client->htrd);
		txt_client_htrd_recbs.peek = txt->client_htrd_org_recbs.peek;
		mio_htrd_setrecbs (txt->client->htrd, &txt_client_htrd_recbs);
		txt->client_htrd_recbs_changed = 1;
	}
	else
	{
		/* no content to be uploaded from the client */
		/* indicate EOF to the peer and disable input wathching from the client */
		txt_mark_over (txt, TXT_OVER_READ_FROM_CLIENT);
	}

	/* this may change later if Content-Length is included in the txt output */
	txt->keep_alive = !!(req->flags & MIO_HTRE_ATTR_KEEPALIVE);

	/* TODO: store current input watching state and use it when destroying the txt data */
	if (mio_dev_sck_read(csck, !(txt->over & TXT_OVER_READ_FROM_CLIENT)) <= -1) goto oops;

	if (txt_send_final_status_to_client(txt, status_code, content_type, content_text, 0) <= -1) goto oops;
	return 0;

oops:
	MIO_DEBUG2 (mio, "HTTS(%p) - FAILURE in dotxt - socket(%p)\n", htts, csck);
	if (txt) txt_halt_participating_devices (txt);
	return -1;
}
