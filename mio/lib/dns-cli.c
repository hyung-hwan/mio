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

#include <mio-dns.h>
#include <mio-sck.h>
#include "mio-prv.h"

#include <netinet/in.h>

struct mio_svc_dns_t
{
	MIO_SVC_HEADER;
	/*MIO_DNS_SVC_HEADER;*/
};

struct mio_svc_dnc_t
{
	MIO_SVC_HEADER;
	/*MIO_DNS_SVC_HEADER;*/

	mio_dev_sck_t* udp_sck;
	mio_dev_sck_t* tcp_sck;
	mio_skad_t serv_addr;

	mio_ntime_t send_tmout; 
	mio_ntime_t reply_tmout; /* default reply timeout */

	/* For a question sent out, it may wait for a corresponding answer.
	 * if max_tries is greater than 0, sending and waiting is limited
	 * to this value over udp and to 1 over tcp. if max_tries is 0,
	 * it sends out the question but never waits for a response.
	 * For a non-question message sent out, it never waits for a response
	 * regardless of max_tries. */ 
	mio_oow_t max_tries; 

	mio_dns_cookie_t cookie;

	mio_oow_t seq;
	mio_dns_msg_t* pending_req;
};

struct dnc_sck_xtn_t
{
	mio_svc_dnc_t* dnc;

	struct
	{
		mio_uint8_t* ptr;
		mio_oow_t  len;
		mio_oow_t  capa;
	} rbuf; /* used by tcp socket */
};
typedef struct dnc_sck_xtn_t dnc_sck_xtn_t;

/* ----------------------------------------------------------------------- */

struct dnc_dns_msg_xtn_t
{
	mio_dev_sck_t* dev;
	mio_tmridx_t   rtmridx;
	mio_dns_msg_t* prev;
	mio_dns_msg_t* next;
	mio_skad_t     servaddr;
	mio_svc_dnc_on_done_t on_done;
	mio_ntime_t    wtmout;
	mio_ntime_t    rtmout;
	int            rmaxtries; /* maximum number of tries to receive a reply */
	int            rtries; /* number of tries made so far */
	int            pending;
};
typedef struct dnc_dns_msg_xtn_t dnc_dns_msg_xtn_t;

#if defined(MIO_HAVE_INLINE)
	static MIO_INLINE dnc_dns_msg_xtn_t* dnc_dns_msg_getxtn(mio_dns_msg_t* msg) { return (dnc_dns_msg_xtn_t*)((mio_uint8_t*)mio_dns_msg_to_pkt(msg) + msg->pktalilen); }
#else
#	define dnc_dns_msg_getxtn(msg) ((dnc_dns_msg_xtn_t*)((mio_uint8_t*)mio_dns_msg_to_pkt(msg) + msg->pktalilen))
#endif

static MIO_INLINE void chain_pending_dns_reqmsg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	if (dnc->pending_req)
	{
		dnc_dns_msg_getxtn(dnc->pending_req)->prev = msg;
		dnc_dns_msg_getxtn(msg)->next = dnc->pending_req;
	}
	dnc->pending_req = msg;
	dnc_dns_msg_getxtn(msg)->pending = 1;
}

static MIO_INLINE void unchain_pending_dns_reqmsg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	if (msgxtn->next) dnc_dns_msg_getxtn(msgxtn->next)->prev = msgxtn->prev;
	if (msgxtn->prev) dnc_dns_msg_getxtn(msgxtn->prev)->next = msgxtn->next;
	else dnc->pending_req = msgxtn->next;
	dnc_dns_msg_getxtn(msg)->pending = 0;
}

static mio_dns_msg_t* make_dns_msg (mio_svc_dnc_t* dnc, mio_dns_bhdr_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, mio_svc_dnc_on_done_t on_done, mio_oow_t xtnsize)
{
	mio_dns_msg_t* msg;
	dnc_dns_msg_xtn_t* msgxtn;

	msg = mio_dns_make_msg(dnc->mio, bdns, qr, qr_count, rr, rr_count, edns, MIO_SIZEOF(*msgxtn) + xtnsize);
	if (MIO_UNLIKELY(!msg)) return MIO_NULL;

	if (bdns->id < 0)
	{
		mio_dns_pkt_t* pkt = mio_dns_msg_to_pkt(msg);
		pkt->id = mio_hton16(dnc->seq);
		dnc->seq++;
	}

	msgxtn = dnc_dns_msg_getxtn(msg);
	msgxtn->dev = dnc->udp_sck;
	msgxtn->rtmridx = MIO_TMRIDX_INVALID;
	msgxtn->on_done = on_done;
	msgxtn->wtmout = dnc->send_tmout;
	msgxtn->rtmout = dnc->reply_tmout;
	msgxtn->rmaxtries = dnc->max_tries; 
	msgxtn->rtries = 0;
	msgxtn->servaddr = dnc->serv_addr;

	return msg;
}

static void release_dns_msg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	mio_t* mio = dnc->mio;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);

MIO_DEBUG1 (mio, "DNC - releasing dns message - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));

	if (msg == dnc->pending_req || msgxtn->next || msgxtn->prev)
	{
		/* it's chained in the pending request. unchain it */
		unchain_pending_dns_reqmsg (dnc, msg);
	}

	if (msgxtn->rtmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, msgxtn->rtmridx);
		MIO_ASSERT (mio, msgxtn->rtmridx == MIO_TMRIDX_INVALID);
	}

/* TODO: add it to the free msg list instead of just freeing it. */
	mio_dns_free_msg (dnc->mio, msg);
}
/* ----------------------------------------------------------------------- */

static int handle_tcp_packet (mio_dev_sck_t* dev, mio_dns_pkt_t* pkt, mio_uint16_t pktlen)
{
	mio_t* mio = dev->mio;
	dnc_sck_xtn_t* sckxtn = mio_dev_sck_getxtn(dev);
	mio_svc_dnc_t* dnc = sckxtn->dnc;
	mio_uint16_t id;
	mio_dns_msg_t* reqmsg;

	if (!pkt->qr) 
	{
		MIO_DEBUG0 (mio, "DNC - dropping dns request received over tcp ...\n"); /* TODO: add source info */
		return 0; /* drop request. nothing to do */
	}

	id = mio_ntoh16(pkt->id);

MIO_DEBUG1 (mio, "DNC - got dns response over tcp - msgid:%d\n", id);

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		mio_dns_pkt_t* reqpkt = mio_dns_msg_to_pkt(reqmsg);
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);

		if (dev == (mio_dev_sck_t*)reqmsgxtn->dev && pkt->id == reqpkt->id)
		{
			if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, MIO_ENOERR, pkt, pktlen);
			release_dns_msg (dnc, reqmsg);
			return 0;
		}

		reqmsg = reqmsgxtn->next;
	}

MIO_DEBUG1 (mio, "DNC - unknown dns response over tcp... msgid:%d\n", id); /* TODO: add source info */
	return 0;
}

static MIO_INLINE int copy_data_to_sck_rbuf (mio_dev_sck_t* dev, const void* data, mio_uint16_t dlen)
{
	dnc_sck_xtn_t* sckxtn = mio_dev_sck_getxtn(dev);

	if (sckxtn->rbuf.capa - sckxtn->rbuf.len < dlen)
	{
		mio_uint16_t newcapa;
		mio_uint8_t* tmp;

		newcapa = sckxtn->rbuf.len + dlen;
		newcapa = MIO_ALIGN_POW2(newcapa, 512);
		tmp = mio_reallocmem(dev->mio, sckxtn->rbuf.ptr, newcapa);
		if (!tmp) return -1;

		sckxtn->rbuf.capa = newcapa;
		sckxtn->rbuf.ptr = tmp;
	}

	MIO_MEMCPY (&sckxtn->rbuf.ptr[sckxtn->rbuf.len], data, dlen);
	sckxtn->rbuf.len += dlen;
	return 0;
}

static int on_tcp_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_skad_t* srcaddr)
{
	mio_t* mio = dev->mio;
	dnc_sck_xtn_t* sckxtn = mio_dev_sck_getxtn(dev);
	mio_uint16_t pktlen;
	mio_uint8_t* dptr;
	mio_iolen_t rem;

	if (MIO_UNLIKELY(dlen <= -1))
	{
		MIO_DEBUG1 (mio, "DNC - dns tcp read error ....%js\n", mio_geterrmsg(mio)); /* TODO: add source packet */
		goto oops;
	}
	else if (MIO_UNLIKELY(dlen == 0))
	{
		MIO_DEBUG0 (mio, "DNC - dns tcp read error ...premature tcp socket end\n"); /* TODO: add source packet */
		goto oops;
	}

	dptr = (mio_uint8_t*)data;
	rem = dlen;
	do
	{
		if (MIO_UNLIKELY(sckxtn->rbuf.len == 1))
		{
			pktlen = ((mio_uint16_t)sckxtn->rbuf.ptr[0] << 8) | *(mio_uint8_t*)dptr;
			if (MIO_UNLIKELY((rem - 1) < pktlen)) goto incomplete_data;
			dptr += 1; rem -= 1; sckxtn->rbuf.len = 0;
			handle_tcp_packet (dev, (mio_dns_pkt_t*)dptr, pktlen);
			dptr += pktlen; rem -= pktlen;
		}
		else if (MIO_UNLIKELY(sckxtn->rbuf.len > 1))
		{
			mio_uint16_t cplen;

			pktlen = ((mio_uint16_t)sckxtn->rbuf.ptr[0] << 8) | sckxtn->rbuf.ptr[1];
			if (MIO_UNLIKELY(sckxtn->rbuf.len - 2 + rem < pktlen)) goto incomplete_data;

			cplen = pktlen - (sckxtn->rbuf.len - 2);
			if (copy_data_to_sck_rbuf(dev, dptr, cplen) <= -1) goto oops;

			dptr += cplen; rem -= cplen; sckxtn->rbuf.len = 0;
			handle_tcp_packet (dev, (mio_dns_pkt_t*)&sckxtn->rbuf.ptr[2], pktlen);
		}
		else
		{
			if (MIO_LIKELY(rem >= 2)) 
			{
				pktlen = ((mio_uint16_t)*(mio_uint8_t*)dptr << 8) | *((mio_uint8_t*)dptr + 1);
				dptr += 2; rem -= 2;
				if (MIO_UNLIKELY(rem < pktlen)) goto incomplete_data;
				handle_tcp_packet (dev, (mio_dns_pkt_t*)dptr, pktlen);
				dptr += pktlen; rem -= pktlen;
			}
			else
			{
			incomplete_data:
				if (copy_data_to_sck_rbuf(dev, dptr, rem) <= -1) goto oops;
				rem = 0;
			}
		}
	}
	while (rem > 0);

	return 0;

oops:
	mio_dev_sck_halt (dev);
	return 0;
}

static void on_tcp_reply_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dns_msg_t* reqmsg = (mio_dns_msg_t*)job->ctx;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	mio_dev_sck_t* dev = reqmsgxtn->dev;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;

	MIO_ASSERT (mio, reqmsgxtn->rtmridx == MIO_TMRIDX_INVALID);
	MIO_ASSERT (mio, dev == dnc->tcp_sck);

MIO_DEBUG1 (mio, "DNC - unable to receive dns response in time over TCP - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(reqmsg)->id));

	if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, MIO_ETMOUT, MIO_NULL, 0);
	release_dns_msg (dnc, reqmsg);
}

static int on_tcp_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dns_msg_t* msg = (mio_dns_msg_t*)wrctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_errnum_t status;

	if (wrlen <= -1)
	{
		/* send failure */
		status = mio_geterrnum(mio);
		goto finalize;
	}
	else if (mio_dns_msg_to_pkt(msg)->qr == 0 && msgxtn->rmaxtries > 0)
	{
		/* question. schedule to wait for response */
		mio_tmrjob_t tmrjob;

		MIO_DEBUG1 (mio, "DNC - sent dns question over tcp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = msg;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &msgxtn->rtmout);
		tmrjob.handler = on_tcp_reply_timeout;
		tmrjob.idxptr = &msgxtn->rtmridx;
		msgxtn->rtmridx = mio_instmrjob(mio, &tmrjob);
		if (msgxtn->rtmridx == MIO_TMRIDX_INVALID)
		{
			/* call the callback to indicate this operation failure in the middle of transaction */
			status = mio_geterrnum(mio);
			MIO_DEBUG1 (mio, "DNC - unable to schedule tcp timeout - msgid: %d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));
			goto finalize;
		}

		MIO_ASSERT (mio, msgxtn->pending != 0);
	}
	else
	{
		/* no error. successfuly sent a message. no reply is expected */
		MIO_DEBUG1 (mio, "DNC - sent dns message over tcp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));
		status = MIO_ENOERR;
		goto finalize;
	}

	return 0;

finalize:
	if (MIO_LIKELY(msgxtn->on_done)) msgxtn->on_done (dnc, msg, status, MIO_NULL, 0);
	release_dns_msg (dnc, msg);
	return 0;
}

static int write_dns_msg_over_tcp (mio_dev_sck_t* dev, mio_dns_msg_t* msg)
{
	mio_t* mio = dev->mio;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	mio_uint16_t pktlen;
	mio_iovec_t iov[2];

	MIO_DEBUG1 (mio, "DNC - sending dns message over tcp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));

	pktlen = mio_hton16(msg->pktlen);

	MIO_ASSERT (mio, msgxtn->rtries == 0);
	msgxtn->rtries = 1;

	/* TODO: Is it better to create 2 byte space when sending UDP and use it here instead of iov? */
	iov[0].iov_ptr = &pktlen;
	iov[0].iov_len = MIO_SIZEOF(pktlen);
	iov[1].iov_ptr = mio_dns_msg_to_pkt(msg);
	iov[1].iov_len = msg->pktlen;
	return mio_dev_sck_timedwritev(dev, iov, MIO_COUNTOF(iov), &msgxtn->rtmout, msg, MIO_NULL);
}

static void on_tcp_connect (mio_dev_sck_t* dev)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_msg_t* reqmsg;

	MIO_ASSERT (mio, dev == dnc->tcp_sck);

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
		mio_dns_msg_t* nextreqmsg = reqmsgxtn->next;

		if (reqmsgxtn->dev == dev && reqmsgxtn->rtries == 0) 
		{
			if (write_dns_msg_over_tcp(dev, reqmsg) <= -1)
			{
				if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, mio_geterrnum(mio), MIO_NULL, 0);
				release_dns_msg (dnc, reqmsg);
			}
		}
		reqmsg = nextreqmsg;
	}
}

static void on_tcp_disconnect (mio_dev_sck_t* dev)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_msg_t* reqmsg;
	int status;

	/* UNABLE TO CONNECT or CONNECT TIMED OUT */
	status = mio_geterrnum(mio);

	if (status == MIO_ENOERR)
	{
		MIO_DEBUG0 (mio, "DNC - TCP DISCONNECTED\n");
	}
	else
	{
		MIO_DEBUG2 (mio, "DNC - TCP UNABLE TO CONNECT  %d -> %js\n", status, mio_errnum_to_errstr(status));
	}

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
		mio_dns_msg_t* nextreqmsg = reqmsgxtn->next;

		if (reqmsgxtn->dev == dev)
		{
			if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, MIO_ENORSP, MIO_NULL, 0);
			release_dns_msg (dnc, reqmsg);
		}

		reqmsg = nextreqmsg;
	}

	/* let's forget about the tcp socket */
	dnc->tcp_sck = MIO_NULL; 
}

static int switch_reqmsg_transport_to_tcp (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg)
{
	mio_t* mio = dnc->mio;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	dnc_sck_xtn_t* sckxtn;

	mio_dev_sck_make_t mkinfo;
	mio_dev_sck_connect_t cinfo;

/* TODO: more reliable way to check if connection is ok.
 * even if tcp_sck is not null, the connection could have been torn down... */
	if (!dnc->tcp_sck)
	{
		MIO_MEMSET (&mkinfo, 0, MIO_SIZEOF(mkinfo));
		switch (mio_skad_family(&reqmsgxtn->servaddr))
		{
			case MIO_AF_INET:
				mkinfo.type = MIO_DEV_SCK_TCP4;
				break;

			case MIO_AF_INET6:
				mkinfo.type = MIO_DEV_SCK_TCP6;
				break;

			default:
				mio_seterrnum (mio, MIO_EINTERN);
				return -1;
		}

		mkinfo.on_write = on_tcp_write;
		mkinfo.on_read = on_tcp_read;
		mkinfo.on_connect = on_tcp_connect;
		mkinfo.on_disconnect = on_tcp_disconnect;
		dnc->tcp_sck = mio_dev_sck_make(mio, MIO_SIZEOF(*sckxtn), &mkinfo);
		if (!dnc->tcp_sck) return -1;

		sckxtn = (dnc_sck_xtn_t*)mio_dev_sck_getxtn(dnc->tcp_sck);
		sckxtn->dnc = dnc;

		MIO_MEMSET (&cinfo, 0, MIO_SIZEOF(cinfo));
		cinfo.remoteaddr = reqmsgxtn->servaddr;
		cinfo.connect_tmout = reqmsgxtn->rtmout; /* TOOD: create a separate connect timeout or treate rtmout as a whole transaction time and calculate the remaining time from the transaction start, and use it */

		if (mio_dev_sck_connect(dnc->tcp_sck, &cinfo) <= -1) 
		{
			mio_dev_sck_kill (dnc->tcp_sck);
			dnc->tcp_sck = MIO_NULL;
			return -1; /* the connect request hasn't been honored. */
		}
	}
	
	/* switch the belonging device to the tcp socket since the connect request has been acknowledged. */
	MIO_ASSERT (mio, reqmsgxtn->rtmridx == MIO_TMRIDX_INVALID); /* ensure no timer job scheduled at this moment */
	reqmsgxtn->dev = dnc->tcp_sck;
	reqmsgxtn->rtries = 0;
	if (!reqmsgxtn->pending && mio_dns_msg_to_pkt(reqmsg)->qr == 0) chain_pending_dns_reqmsg (dnc, reqmsg);

MIO_DEBUG6 (mio, "DNC - switched transport to tcp - msgid:%d %p %p %p %p %p\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(reqmsg)->id), reqmsg, reqmsgxtn, reqmsgxtn->dev, dnc->udp_sck, dnc->tcp_sck);

	if (MIO_DEV_SCK_GET_PROGRESS(dnc->tcp_sck) & MIO_DEV_SCK_CONNECTED)
	{
		if (write_dns_msg_over_tcp(reqmsgxtn->dev, reqmsg) <= -1)
		{
			/* the caller must not use reqmsg from now because it's freed here */
			if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, mio_geterrnum(mio), MIO_NULL, 0);
			release_dns_msg (dnc, reqmsg);
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int on_udp_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_skad_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_pkt_t* pkt;
	mio_dns_msg_t* reqmsg;
	mio_uint16_t id;

	if (MIO_UNLIKELY(dlen <= -1))
	{
		MIO_DEBUG1 (mio, "DNC - dns read error ....%js\n", mio_geterrmsg(mio)); /* TODO: add source packet */
		return 0;
	}

	if (MIO_UNLIKELY(dlen < MIO_SIZEOF(*pkt))) 
	{
		MIO_DEBUG0 (mio, "DNC - dns packet too small from ....\n"); /* TODO: add source packet */
		return 0; /* drop */
	}
	pkt = (mio_dns_pkt_t*)data;
	if (!pkt->qr) 
	{
		MIO_DEBUG0 (mio, "DNC - dropping dns request received ...\n"); /* TODO: add source info */
		return 0; /* drop request */
	}

	id = mio_ntoh16(pkt->id);

	/* if id doesn't match one of the pending requests sent,  drop it */

/* TODO: improve performance of dns response matching*/
	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		mio_dns_pkt_t* reqpkt = mio_dns_msg_to_pkt(reqmsg);
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);

		if (reqmsgxtn->dev == dev && pkt->id == reqpkt->id && mio_equal_skads(&reqmsgxtn->servaddr, srcaddr, 0))
		{
			if (reqmsgxtn->rtmridx != MIO_TMRIDX_INVALID)
			{
				/* unschedule a timer job if any */
				mio_deltmrjob (mio, reqmsgxtn->rtmridx);
				MIO_ASSERT (mio, reqmsgxtn->rtmridx == MIO_TMRIDX_INVALID);
			}

////////////////////////
// for simple testing without actual truncated dns response
//pkt->tc = 1;
////////////////////////
			if (MIO_UNLIKELY(pkt->tc))
			{
				/* TODO: add an option for this behavior */
				if (switch_reqmsg_transport_to_tcp(dnc, reqmsg) >= 0) return 0;
				/* TODO: add an option to call an error callback with TRUNCATION error code instead of fallback to received UDP truncated message */
			}

MIO_DEBUG1 (mio, "DNC - received dns response over udp for msgid:%d\n", id);
			if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, MIO_ENOERR, data, dlen);
			release_dns_msg (dnc, reqmsg);
			return 0;
		}

		reqmsg = reqmsgxtn->next;
	}

	/* the response id didn't match the ID of pending requests - need to wait longer? */
MIO_DEBUG1 (mio, "DNC - unknown dns response over udp... msgid:%d\n", id); /* TODO: add source info */
	return 0;
}

static void on_udp_reply_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dns_msg_t* reqmsg = (mio_dns_msg_t*)job->ctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(reqmsg);
	mio_dev_sck_t* dev = msgxtn->dev;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_errnum_t status = MIO_ETMOUT;

	MIO_ASSERT (mio, msgxtn->rtmridx == MIO_TMRIDX_INVALID);
	MIO_ASSERT (mio, dev == dnc->udp_sck);

MIO_DEBUG1 (mio, "DNC - unable to receive dns response in time over udp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(reqmsg)->id));
	if (msgxtn->rtries < msgxtn->rmaxtries)
	{
		mio_ntime_t* tmout;

		tmout = MIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: MIO_NULL;
MIO_DEBUG1 (mio, "DNC - sending dns question again over udp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(reqmsg)->id));
		if (mio_dev_sck_timedwrite(dev, mio_dns_msg_to_pkt(reqmsg), reqmsg->pktlen, tmout, reqmsg, &msgxtn->servaddr) >= 0) return; /* resent */

		/* retry failed */
		status = mio_geterrnum(mio);
	}

	if (MIO_LIKELY(msgxtn->on_done)) msgxtn->on_done (dnc, reqmsg, status, MIO_NULL, 0);
	release_dns_msg (dnc, reqmsg);
}

static int on_udp_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dns_msg_t* msg = (mio_dns_msg_t*)wrctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_errnum_t status;

	MIO_ASSERT (mio, dev == (mio_dev_sck_t*)msgxtn->dev);

	if (wrlen <= -1)
	{
		/* write has timed out or an error has occurred */
		status = mio_geterrnum(mio);
		goto finalize;
	}
	else if (mio_dns_msg_to_pkt(msg)->qr == 0 && msgxtn->rmaxtries > 0)
	{
		/* question. schedule to wait for response */
		mio_tmrjob_t tmrjob;

		MIO_DEBUG1 (mio, "DNC - sent dns question over udp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));
		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = msg;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &msgxtn->rtmout);
		tmrjob.handler = on_udp_reply_timeout;
		tmrjob.idxptr = &msgxtn->rtmridx;
		msgxtn->rtmridx = mio_instmrjob(mio, &tmrjob);
		if (msgxtn->rtmridx == MIO_TMRIDX_INVALID)
		{
			/* call the callback to indicate this operation failure in the middle of transaction */
			status = mio_geterrnum(mio);
			MIO_DEBUG1 (mio, "DNC - unable to schedule udp timeout - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));
			goto finalize;
		}

		if (msgxtn->rtries == 0)
		{
			/* this is the first wait */
			/* TODO: improve performance. hashing by id? */
			/* chain it to the peing request list */
			chain_pending_dns_reqmsg (dnc, msg);
		}
		msgxtn->rtries++;
	}
	else
	{
		MIO_DEBUG1 (mio, "DNC - sent dns message over udp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));
		/* sent an answer. however this may be a question if msgxtn->rmaxtries is 0. */
		status = MIO_ENOERR;
		goto finalize;
	}

	return 0;

finalize:
	if (MIO_LIKELY(msgxtn->on_done)) msgxtn->on_done (dnc, msg, status, MIO_NULL, 0);
	release_dns_msg (dnc, msg);
	return 0;
}

static void on_udp_connect (mio_dev_sck_t* dev)
{
}

static void on_udp_disconnect (mio_dev_sck_t* dev)
{
	/*mio_t* mio = dev->mio;*/
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_msg_t* reqmsg;

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
		mio_dns_msg_t* nextreqmsg = reqmsgxtn->next;

		if (reqmsgxtn->dev == dev)
		{
			if (MIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, MIO_ENORSP, MIO_NULL, 0);
			release_dns_msg (dnc, reqmsg);
		}

		reqmsg = nextreqmsg;
	}
}

mio_svc_dnc_t* mio_svc_dnc_start (mio_t* mio, const mio_skad_t* serv_addr, const mio_skad_t* bind_addr, const mio_ntime_t* send_tmout, const mio_ntime_t* reply_tmout, mio_oow_t max_tries)
{
	mio_svc_dnc_t* dnc = MIO_NULL;
	mio_dev_sck_make_t mkinfo;
	dnc_sck_xtn_t* sckxtn;
	mio_ntime_t now;

	dnc = (mio_svc_dnc_t*)mio_callocmem(mio, MIO_SIZEOF(*dnc));
	if (MIO_UNLIKELY(!dnc)) goto oops;

	dnc->mio = mio;
	dnc->svc_stop = mio_svc_dnc_stop;
	dnc->serv_addr = *serv_addr;
	dnc->send_tmout = *send_tmout;
	dnc->reply_tmout = *reply_tmout;
	dnc->max_tries = max_tries;

	MIO_MEMSET (&mkinfo, 0, MIO_SIZEOF(mkinfo));
	switch (mio_skad_family(serv_addr))
	{
		case MIO_AF_INET:
			mkinfo.type = MIO_DEV_SCK_UDP4;
			break;

		case MIO_AF_INET6:
			mkinfo.type = MIO_DEV_SCK_UDP6;
			break;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			goto oops;
	}
	mkinfo.on_write = on_udp_write;
	mkinfo.on_read = on_udp_read;
	mkinfo.on_connect = on_udp_connect;
	mkinfo.on_disconnect = on_udp_disconnect;
	dnc->udp_sck = mio_dev_sck_make(mio, MIO_SIZEOF(*sckxtn), &mkinfo);
	if (!dnc->udp_sck) goto oops;

	sckxtn = (dnc_sck_xtn_t*)mio_dev_sck_getxtn(dnc->udp_sck);
	sckxtn->dnc = dnc;

	if (bind_addr) /* TODO: get mio_dev_sck_bind_t? instead of bind_addr? */
	{
		mio_dev_sck_bind_t bi;
		MIO_MEMSET (&bi, 0, MIO_SIZEOF(bi));
		bi.localaddr = *bind_addr;
		if (mio_dev_sck_bind(dnc->udp_sck, &bi) <= -1) goto oops;
	}


	/* initialize the dns cookie key */
	mio_gettime (mio, &now);
	MIO_MEMCPY (&dnc->cookie.key[0], &now.sec, (MIO_SIZEOF(now.sec) < 8? MIO_SIZEOF(now.sec): 8));
	MIO_MEMCPY (&dnc->cookie.key[8], &now.nsec, (MIO_SIZEOF(now.nsec) < 8? MIO_SIZEOF(now.nsec): 8));

	MIO_SVCL_APPEND_SVC (&mio->actsvc, (mio_svc_t*)dnc);
	MIO_DEBUG1 (mio, "DNC - STARTED SERVICE %p\n", dnc);
	return dnc;

oops:
	if (dnc)
	{
		if (dnc->udp_sck) mio_dev_sck_kill (dnc->udp_sck);
		mio_freemem (mio, dnc);
	}
	return MIO_NULL;
}

void mio_svc_dnc_stop (mio_svc_dnc_t* dnc)
{
	mio_t* mio = dnc->mio;

	MIO_DEBUG1 (mio, "DNC - STOPPING SERVICE %p\n", dnc);
	if (dnc->udp_sck) mio_dev_sck_kill (dnc->udp_sck);
	if (dnc->tcp_sck) mio_dev_sck_kill (dnc->tcp_sck);
	while (dnc->pending_req) release_dns_msg (dnc, dnc->pending_req);
	MIO_SVCL_UNLINK_SVC (dnc);
	mio_freemem (mio, dnc);
}


static MIO_INLINE int send_dns_msg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg, int send_flags)
{
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	mio_ntime_t* tmout;

	if ((send_flags & MIO_SVC_DNC_SEND_FLAG_PREFER_TCP) && switch_reqmsg_transport_to_tcp(dnc, msg) >= 0) return 0;

	MIO_DEBUG1 (dnc->mio, "DNC - sending dns message over udp - msgid:%d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));

	tmout = MIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: MIO_NULL;
/* TODO: optionally, override dnc->serv_addr and use the target address passed as a parameter */
	return mio_dev_sck_timedwrite(dnc->udp_sck, mio_dns_msg_to_pkt(msg), msg->pktlen, tmout, msg, &msgxtn->servaddr);
}

mio_dns_msg_t* mio_svc_dnc_sendmsg (mio_svc_dnc_t* dnc, mio_dns_bhdr_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, int send_flags, mio_svc_dnc_on_done_t on_done, mio_oow_t xtnsize)
{
	/* send a request or a response */
	mio_dns_msg_t* msg;

	msg = make_dns_msg(dnc, bdns, qr, qr_count, rr, rr_count, edns, on_done, xtnsize);
	if (!msg) return MIO_NULL;

	if (send_dns_msg(dnc, msg, send_flags) <= -1)
	{
		release_dns_msg (dnc, msg);
		return MIO_NULL;
	}

	return msg;
}

mio_dns_msg_t* mio_svc_dnc_sendreq (mio_svc_dnc_t* dnc, mio_dns_bhdr_t* bdns, mio_dns_bqr_t* qr, mio_dns_bedns_t* edns, int send_flags, mio_svc_dnc_on_done_t on_done, mio_oow_t xtnsize)
{
	/* send a request without resource records */
	if (bdns->rcode != MIO_DNS_RCODE_NOERROR)
	{
		mio_seterrnum (dnc->mio, MIO_EINVAL);
		return MIO_NULL;
	}

	return mio_svc_dnc_sendmsg(dnc, bdns, qr, 1, MIO_NULL, 0, edns, send_flags, on_done, xtnsize);
}

/* ----------------------------------------------------------------------- */


struct dnc_dns_msg_resolve_xtn_t
{
	mio_dns_rrt_t qtype;
	int flags;
	mio_uint8_t client_cookie[MIO_DNS_COOKIE_CLIENT_LEN];
	mio_svc_dnc_on_resolve_t on_resolve;
};
typedef struct dnc_dns_msg_resolve_xtn_t dnc_dns_msg_resolve_xtn_t;

#if defined(MIO_HAVE_INLINE)
	static MIO_INLINE dnc_dns_msg_resolve_xtn_t* dnc_dns_msg_resolve_getxtn(mio_dns_msg_t* msg) { return ((dnc_dns_msg_resolve_xtn_t*)((mio_uint8_t*)dnc_dns_msg_getxtn(msg) + MIO_SIZEOF(dnc_dns_msg_xtn_t))); }
#else
#	define dnc_dns_msg_resolve_getxtn(msg) ((dnc_dns_msg_resolve_xtn_t*)((mio_uint8_t*)dnc_dns_msg_getxtn(msg) + MIO_SIZEOF(dnc_dns_msg_xtn_t)))
#endif

static void on_dnc_resolve (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg, mio_errnum_t status, const void* data, mio_oow_t dlen)
{
	mio_t* mio = mio_svc_dnc_getmio(dnc);
	mio_dns_pkt_info_t* pi = MIO_NULL;
	dnc_dns_msg_resolve_xtn_t* resolxtn = dnc_dns_msg_resolve_getxtn(reqmsg);

	if (data)
	{
		mio_uint32_t i;

		MIO_ASSERT (mio, status == MIO_ENOERR);

		pi = mio_dns_make_pkt_info(mio, data, dlen);
		if (!pi)
		{
			status = mio_geterrnum(mio);
			goto no_data;
		}

		if (resolxtn->flags & MIO_SVC_DNC_RESOLVE_FLAG_COOKIE)
		{
			if (pi->edns.cookie.server_len > 0)
			{
				/* remember the received server cookie to use it with other new requests */
				MIO_MEMCPY (dnc->cookie.data.server, pi->edns.cookie.data.server, pi->edns.cookie.server_len);
				dnc->cookie.server_len = pi->edns.cookie.server_len;
			}
		}

		if (!(resolxtn->flags & MIO_SVC_DNC_RESOLVE_FLAG_BRIEF))
		{
			/* the full reply packet is requested. */
			if (resolxtn->on_resolve) resolxtn->on_resolve (dnc, reqmsg, status, pi, 0);
			goto done;
		}

		if (pi->hdr.rcode != MIO_DNS_RCODE_NOERROR) 
		{
			status = MIO_EINVAL;
			goto no_data;
		}

		if (pi->ancount < 0) goto no_data;

		/* in the brief mode, we inspect the answer section only */
		if (resolxtn->qtype == MIO_DNS_RRT_Q_ANY)
		{
			/* return A or AAAA for ANY in the brief mode */
			for (i = 0; i < pi->ancount; i++)
			{
				if (pi->rr.an[i].rrtype == MIO_DNS_RRT_A || pi->rr.an[i].rrtype == MIO_DNS_RRT_AAAA)
				{
				match_found:
					if (resolxtn->on_resolve) resolxtn->on_resolve (dnc, reqmsg, status, &pi->rr.an[i], MIO_SIZEOF(pi->rr.an[i]));
					goto done;
				}
			}
		}

		for (i = 0; i < pi->ancount; i++)
		{
			/* it is a bit time taking to retreive the query type from the packet
			 * bundled in reqmsg as it requires parsing of the packet. let me use
			 * the query type i stored in the extension space. */
			switch (resolxtn->qtype)
			{
				case MIO_DNS_RRT_Q_ANY: 
				case MIO_DNS_RRT_Q_AFXR: /* AFXR doesn't make sense in the brief mode. just treat it like ANY */
					/* no A or AAAA found. so give the first entry in the answer */
					goto match_found;

				case MIO_DNS_RRT_Q_MAILA:
					/* if you want to get the full RRs, don't use the brief mode. */
					if (pi->rr.an[i].rrtype == MIO_DNS_RRT_MD || pi->rr.an[i].rrtype == MIO_DNS_RRT_MF) goto match_found;
					break;

				case MIO_DNS_RRT_Q_MAILB:
					/* if you want to get the full RRs, don't use the brief mode. */
					if (pi->rr.an[i].rrtype == MIO_DNS_RRT_MB || pi->rr.an[i].rrtype == MIO_DNS_RRT_MG ||
					    pi->rr.an[i].rrtype == MIO_DNS_RRT_MR || pi->rr.an[i].rrtype == MIO_DNS_RRT_MINFO) goto match_found;
					break;

				default:
					if (pi->rr.an[i].rrtype == resolxtn->qtype) goto match_found;
					break;
			}
		}
		goto no_data;
	}
	else
	{
	no_data:
		if (resolxtn->on_resolve) resolxtn->on_resolve (dnc, reqmsg, status, MIO_NULL, 0);
	}

done:
	if (pi) mio_dns_free_pkt_info(mio_svc_dnc_getmio(dnc), pi);
}

mio_dns_msg_t* mio_svc_dnc_resolve (mio_svc_dnc_t* dnc, const mio_bch_t* qname, mio_dns_rrt_t qtype, int resolve_flags, mio_svc_dnc_on_resolve_t on_resolve, mio_oow_t xtnsize)
{
	static mio_dns_bhdr_t qhdr =
	{
		-1, /* id */
		0,  /* qr */
		MIO_DNS_OPCODE_QUERY, /* opcode */
		0, /* aa */
		0, /* tc */
		1, /* rd */
		0, /* ra */
		0, /* ad */
		0, /* cd */
		MIO_DNS_RCODE_NOERROR /* rcode */
	};

	mio_dns_bedns_t qedns =
	{
		4096, /* uplen */

		0,    /* edns version */
		0,    /* dnssec ok */

		0,    /* number of edns options */
		MIO_NULL
	};

	mio_dns_beopt_t beopt_cookie;

	mio_dns_bqr_t qr;
	mio_dns_msg_t* reqmsg;
	dnc_dns_msg_resolve_xtn_t* resolxtn;

	qr.qname = (mio_bch_t*)qname;
	qr.qtype = qtype;
	qr.qclass = MIO_DNS_RRC_IN;

	if (resolve_flags & MIO_SVC_DNC_RESOLVE_FLAG_COOKIE)
	{
		beopt_cookie.code = MIO_DNS_EOPT_COOKIE;
		beopt_cookie.dptr = &dnc->cookie.data;

		beopt_cookie.dlen = MIO_DNS_COOKIE_CLIENT_LEN; 
		if (dnc->cookie.server_len > 0) beopt_cookie.dlen += dnc->cookie.server_len;

		/* compute the client cookie */
		MIO_STATIC_ASSERT (MIO_SIZEOF(dnc->cookie.data.client) == MIO_DNS_COOKIE_CLIENT_LEN);
		mio_sip_hash_24 (dnc->cookie.key, &dnc->serv_addr, MIO_SIZEOF(dnc->serv_addr), dnc->cookie.data.client);

		qedns.beonum = 1;
		qedns.beoptr = &beopt_cookie;
	}

	if (resolve_flags & MIO_SVC_DNC_RESOLVE_FLAG_DNSSEC)
	{
		qedns.dnssecok = 1;
	}

	reqmsg = make_dns_msg(dnc, &qhdr, &qr, 1, MIO_NULL, 0, &qedns, on_dnc_resolve, MIO_SIZEOF(*resolxtn) + xtnsize);
	if (reqmsg)
	{
		int send_flags;

#if 0
		if ((resolve_flags & MIO_SVC_DNC_RESOLVE_FLAG_COOKIE) && dnc->cookie.server_len == 0)
		{
			/* Exclude the server cookie from the packet when the server cookie is not available.
			 *
			 * ASSUMPTIONS:
			 *  the eopt entries are at the back of the packet.
			 *  only 1 eopt entry(MIO_DNS_EOPT_COOKIE) has been added. 
			 * 
			 * manipulate the data length of the EDNS0 RR and COOKIE option 
			 * as if the server cookie data has not been added.
			 */
			mio_dns_rrtr_t* edns_rrtr;
			mio_dns_eopt_t* eopt;

			edns_rrtr = (mio_dns_rrtr_t*)((mio_uint8_t*)mio_dns_msg_to_pkt(reqmsg) + reqmsg->ednsrrtroff);
			reqmsg->pktlen -= MIO_DNS_COOKIE_SERVER_MAX_LEN;

			MIO_ASSERT (dnc->mio, edns_rrtr->rrtype == MIO_CONST_HTON16(MIO_DNS_RRT_OPT));
			MIO_ASSERT (dnc->mio, edns_rrtr->dlen == MIO_CONST_HTON16(MIO_SIZEOF(mio_dns_eopt_t) + MIO_DNS_COOKIE_MAX_LEN));
			edns_rrtr->dlen = MIO_CONST_HTON16(MIO_SIZEOF(mio_dns_eopt_t) + MIO_DNS_COOKIE_CLIENT_LEN);

			eopt = (mio_dns_eopt_t*)(edns_rrtr + 1);
			MIO_ASSERT (dnc->mio, eopt->dlen == MIO_CONST_HTON16(MIO_DNS_COOKIE_MAX_LEN));
			eopt->dlen = MIO_CONST_HTON16(MIO_DNS_COOKIE_CLIENT_LEN);
		}
#endif

		resolxtn = dnc_dns_msg_resolve_getxtn(reqmsg);
		resolxtn->on_resolve = on_resolve;
		resolxtn->qtype = qtype;
		resolxtn->flags = resolve_flags;
		/* store in the extension area the client cookie set in the packet */
		MIO_MEMCPY (resolxtn->client_cookie, dnc->cookie.data.client, MIO_DNS_COOKIE_CLIENT_LEN);

		send_flags = (resolve_flags & MIO_SVC_DNC_SEND_FLAG_ALL);
		if (MIO_UNLIKELY(qtype == MIO_DNS_RRT_Q_AFXR)) send_flags |= MIO_SVC_DNC_SEND_FLAG_PREFER_TCP;
		if (send_dns_msg(dnc, reqmsg, send_flags) <= -1)
		{
			release_dns_msg (dnc, reqmsg);
			return MIO_NULL;
		}
	}

	return reqmsg;
}

int mio_svc_dnc_checkclientcookie (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg, mio_dns_pkt_info_t* respi)
{
	mio_uint8_t xb[MIO_DNS_COOKIE_CLIENT_LEN];
	mio_uint8_t* x;

	x = mio_dns_find_client_cookie_in_msg(reqmsg, &xb);
	if (x)
	{
		/* there is a client cookie in the request. */
		if (respi->edns.cookie.client_len > 0)
		{
			MIO_ASSERT (dnc->mio, respi->edns.cookie.client_len == MIO_DNS_COOKIE_CLIENT_LEN);
			return MIO_MEMCMP(x, respi->edns.cookie.data.client, MIO_DNS_COOKIE_CLIENT_LEN) == 0; /* 1 if ok, 0 if not */
		}
		else
		{
			/* no client cookie in the response - the server doesn't support cookie? */
			return -1;
		}
	}

	return 2; /* ok because the request doesn't include the client cookie */
}

/* TODO: upon startup, read /etc/hosts. setup inotify or find a way to detect file changes..
 *       in resolve, add an option to use entries from /etc/hosts */

/* TODO: afxr client ... */

/* TODO: trace function to do its own recursive resolution?... 
mio_dns_msg_t* mio_svc_dnc_trace (mio_svc_dnc_t* dnc, const mio_bch_t* qname, mio_dns_rrt_t qtype, int resolve_flags, mio_svc_dnc_on_resolve_t on_resolve, mio_oow_t xtnsize)
{
}
*/
