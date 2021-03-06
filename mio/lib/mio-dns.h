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

#ifndef _MIO_DNS_H_
#define _MIO_DNS_H_

#include <mio.h>
#include <mio-skad.h>

#define MIO_DNS_PORT (53)

enum mio_dns_opcode_t
{
	MIO_DNS_OPCODE_QUERY = 0, /* standard query */
	MIO_DNS_OPCODE_IQUERY = 1, /* inverse query */
	MIO_DNS_OPCODE_STATUS = 2, /* status request */
	/* 3 unassigned */
	MIO_DNS_OPCODE_NOTIFY = 4,
	MIO_DNS_OPCODE_UPDATE = 5
};
typedef enum mio_dns_opcode_t mio_dns_opcode_t;

enum mio_dns_rcode_t
{
	MIO_DNS_RCODE_NOERROR   = 0,
	MIO_DNS_RCODE_FORMERR   = 1,  /* format error */
	MIO_DNS_RCODE_SERVFAIL  = 2,  /* server failure */
	MIO_DNS_RCODE_NXDOMAIN  = 3,  /* non-existent domain */
	MIO_DNS_RCODE_NOTIMPL   = 4,  /* not implemented */
	MIO_DNS_RCODE_REFUSED   = 5,  /* query refused */
	MIO_DNS_RCODE_YXDOMAIN  = 6,  /* name exists when it should not */
	MIO_DNS_RCODE_YXRRSET   = 7,  /* RR set exists when it should not */
	MIO_DNS_RCODE_NXRRSET   = 8,  /* RR set exists when it should not */
	MIO_DNS_RCODE_NOTAUTH   = 9,  /* not authorized or server not authoritative for zone*/
	MIO_DNS_RCODE_NOTZONE   = 10, /* name not contained in zone */

	/* the standard rcode field is 4 bit long. so the max is 15. */
	/* items belows require EDNS0 */

	MIO_DNS_RCODE_BADVERS   = 16,
	MIO_DNS_RCODE_BADSIG    = 17,
	MIO_DNS_RCODE_BADTIME   = 18,
	MIO_DNS_RCODE_BADMODE   = 19,
	MIO_DNS_RCODE_BADNAME   = 20,
	MIO_DNS_RCODE_BADALG    = 21,
	MIO_DNS_RCODE_BADTRUNC  = 22,
	MIO_DNS_RCODE_BADCOOKIE = 23
};
typedef enum mio_dns_rcode_t mio_dns_rcode_t;


enum mio_dns_rrt_t
{
/*
 * [RFC1035]
 *  TYPE fields are used in resource records.  Note that these types are a
 *  subset of QTYPEs.
 */
	MIO_DNS_RRT_A = 1,
	MIO_DNS_RRT_NS = 2,
	MIO_DNS_RRT_MD = 3, /* mail destination. RFC973 replaced this with MX*/
	MIO_DNS_RRT_MF = 4, /* mail forwarder. RFC973 replaced this with MX */
	
	MIO_DNS_RRT_CNAME = 5,
	MIO_DNS_RRT_SOA = 6,

	MIO_DNS_RRT_MB = 7, /* kind of obsoleted. RFC1035, RFC2505 */
	MIO_DNS_RRT_MG = 8, /* kind of obsoleted. RFC1035, RFC2505 */
	MIO_DNS_RRT_MR = 9, /* kind of obsoleted. RFC1035, RFC2505 */

	MIO_DNS_RRT_NULL = 10,
	MIO_DNS_RRT_PTR = 12,

	MIO_DNS_RRT_MINFO = 15, /* kind of obsoleted. RFC1035, RFC2505 */

	MIO_DNS_RRT_MX = 15,
	MIO_DNS_RRT_TXT = 16,
	MIO_DNS_RRT_AAAA = 28,
	MIO_DNS_RRT_EID = 31,
	MIO_DNS_RRT_SRV = 33,
	MIO_DNS_RRT_OPT = 41,
	MIO_DNS_RRT_RRSIG = 46,

/*
 * [RFC1035] 
 *  QTYPE fields appear in the question part of a query.  QTYPES are a
 *  superset of TYPEs, hence all TYPEs are valid QTYPEs.  In addition, the
 *  following QTYPEs are defined:
 */
	MIO_DNS_RRT_Q_AFXR = 252, /* A request for a transfer of an entire zone */
	MIO_DNS_RRT_Q_MAILB = 253, /*  A request for mailbox-related records (MB, MG or MR) */
	MIO_DNS_RRT_Q_MAILA = 254, /* A request for mail agent RRs (Obsolete - see MX) */
	MIO_DNS_RRT_Q_ANY = 255 /*  A request for all records */
};
typedef enum mio_dns_rrt_t mio_dns_rrt_t;

/*
 * CLASS fields appear in resource records.  The following CLASS mnemonics
 * and values are defined:
 */
enum mio_dns_rrc_t
{
	MIO_DNS_RRC_IN = 1, /* internet */
	MIO_DNS_RRC_CH = 3, /* chaos */
	MIO_DNS_RRC_HS = 4, /* Hesiod [Dyer 87] */
	MIO_DNS_RRC_NONE = 254,

/*
 * 
 * QCLASS fields appear in the question section of a query.  QCLASS values
 * are a superset of CLASS values; every CLASS is a valid QCLASS.  In
 * addition to CLASS values, the following QCLASSes are defined:
 */

	MIO_DNS_RRC_Q_ANY = 255
};
typedef enum mio_dns_rrc_t mio_dns_rrc_t;



enum mio_dns_eopt_code_t
{
	MIO_DNS_EOPT_NSID         = 3,
	MIO_DNS_EOPT_DAU          = 5,
	MIO_DNS_EOPT_DHU          = 6,
	MIO_DNS_EOPT_N3U          = 7,
	MIO_DNS_EOPT_ECS          = 8,
	MIO_DNS_EOPT_EXPIRE       = 9,
	MIO_DNS_EOPT_COOKIE       = 10,
	MIO_DNS_EOPT_TCPKEEPALIVE = 11,
	MIO_DNS_EOPT_PADDING      = 12,
	MIO_DNS_EOPT_CHAIN        = 13,
	MIO_DNS_EOPT_KEYTAG       = 14,
};
typedef enum mio_dns_eopt_code_t mio_dns_eopt_code_t;

/* dns message preamble */
typedef struct mio_dns_msg_t mio_dns_msg_t;
struct mio_dns_msg_t
{
	mio_oow_t      msglen;
	mio_oow_t      ednsrrtroff; /* offset to trailing data after the name in the dns0 RR*/
	mio_oow_t      pktlen;
	mio_oow_t      pktalilen;
};

#include <mio-pac1.h>
struct mio_dns_pkt_t /* dns packet header */
{
	mio_uint16_t id;
#if defined(MIO_ENDIAN_BIG)
	mio_uint16_t qr: 1; /* query(0), answer(1) */
	mio_uint16_t opcode: 4; /* operation type */
	mio_uint16_t aa: 1; /* authoritative answer */
	mio_uint16_t tc: 1; /* truncated. response too large for UDP */
	mio_uint16_t rd: 1; /* recursion desired */

	mio_uint16_t ra: 1; /* recursion available */
	mio_uint16_t unused_1: 1;
	mio_uint16_t ad: 1; /* authentication data - dnssec */
	mio_uint16_t cd: 1; /* checking disabled - dnssec */
	mio_uint16_t rcode: 4; /* reply code - for reply only */
#else
	mio_uint16_t rd: 1;
	mio_uint16_t tc: 1;
	mio_uint16_t aa: 1;
	mio_uint16_t opcode: 4;
	mio_uint16_t qr: 1;

	mio_uint16_t rcode: 4;
	mio_uint16_t cd: 1;
	mio_uint16_t ad: 1;
	mio_uint16_t unused_1: 1;
	mio_uint16_t ra: 1;
#endif

	mio_uint16_t qdcount; /* number of questions */
	mio_uint16_t ancount; /* number of answers (answer part) */
	mio_uint16_t nscount; /* number of name servers (authority part. only NS types) */
	mio_uint16_t arcount; /* number of additional resource (additional part) */
};
typedef struct mio_dns_pkt_t mio_dns_pkt_t;

struct mio_dns_pkt_alt_t
{
	mio_uint16_t id;
	mio_uint16_t flags;
	mio_uint16_t rrcount[4];
};
typedef struct mio_dns_pkt_alt_t mio_dns_pkt_alt_t;
/* question
 *   name, qtype, qclass
 * answer
 *   name, qtype, qclass, ttl, rlength, rdata
 */

/* trailing part after the domain name in a resource record in a question */
struct mio_dns_qrtr_t
{
	/* qname upto 64 bytes */
	mio_uint16_t qtype;
	mio_uint16_t qclass;
};
typedef struct mio_dns_qrtr_t mio_dns_qrtr_t;

/* trailing part after the domain name in a resource record in an answer */
struct mio_dns_rrtr_t
{
	/* qname upto 64 bytes */
	mio_uint16_t rrtype;
	mio_uint16_t rrclass;
	mio_uint32_t ttl;
	mio_uint16_t dlen; /* data length */
	/* actual data if if dlen > 0 */
};
typedef struct mio_dns_rrtr_t mio_dns_rrtr_t;

struct mio_dns_eopt_t
{
	mio_uint16_t code;
	mio_uint16_t dlen;
	/* actual data if if dlen > 0 */
};
typedef struct mio_dns_eopt_t mio_dns_eopt_t;

#include <mio-upac.h>

/* ---------------------------------------------------------------- */

/*
#define MIO_DNS_HEADER_MAKE_FLAGS(qr,opcode,aa,tc,rd,ra,ad,cd,rcode) \
	((((qr) & 0x01) << 15) | (((opcode) & 0x0F) << 14) | (((aa) & 0x01) << 10) | (((tc) & 0x01) << 9) | \
	(((rd) & 0x01) << 8) | (((ra) & 0x01) << 7) | (((ad) & 0x01) << 5) | (((cd) & 0x01) << 4) | ((rcode) & 0x0F))
*/

/* breakdown of the dns message id and flags. it excludes rr count fields.*/
struct mio_dns_bhdr_t
{
	mio_int32_t id;

	mio_uint8_t qr; /* query(0), answer(1) */
	mio_uint8_t opcode; /* operation type */
	mio_uint8_t aa; /* authoritative answer */
	mio_uint8_t tc; /* truncated. response too large for UDP */
	mio_uint8_t rd; /* recursion desired */

	mio_uint8_t ra; /* recursion available */
	mio_uint8_t ad; /* authentication data - dnssec */
	mio_uint8_t cd; /* checking disabled - dnssec */
	mio_uint8_t rcode; /* reply code - for reply only */
};
typedef struct mio_dns_bhdr_t mio_dns_bhdr_t;

/* breakdown of question record */
struct mio_dns_bqr_t
{
	mio_bch_t*   qname;
	mio_uint16_t qtype;
	mio_uint16_t qclass;
};
typedef struct mio_dns_bqr_t mio_dns_bqr_t;


enum mio_dns_rr_part_t
{
	MIO_DNS_RR_PART_ANSWER,
	MIO_DNS_RR_PART_AUTHORITY,
	MIO_DNS_RR_PART_ADDITIONAL
};
typedef enum mio_dns_rr_part_t mio_dns_rr_part_t;

/* breakdown of resource record */
struct mio_dns_brr_t
{
	mio_dns_rr_part_t  part;
	mio_bch_t*         rrname;
	mio_uint16_t       rrtype;
	mio_uint16_t       rrclass;
	mio_uint32_t       ttl;
	mio_uint16_t       dlen;
	void*              dptr;
};
typedef struct mio_dns_brr_t mio_dns_brr_t;

#if 0
/* A RDATA */
struct mio_dns_brrd_a_t 
{
};
typedef struct mio_dns_brrd_a_t mio_dns_brrd_a_t;

/* 3.3.1 CNAME RDATA format */
struct mio_dns_brrd_cname_t
{
};
typedef struct mio_dns_brrd_cname_t mio_dns_brc_cname_t;

#endif

/* 3.3.9 MX RDATA format */
struct mio_dns_brrd_mx_t
{
	mio_uint16_t preference;
	mio_bch_t*   exchange;
};
typedef struct mio_dns_brrd_mx_t mio_dns_brrd_mx_t;

/* 3.3.13. SOA RDATA format */
struct mio_dns_brrd_soa_t
{
	mio_bch_t*   mname;
	mio_bch_t*   rname; 
	mio_uint32_t serial;
	mio_uint32_t refresh;
	mio_uint32_t retry;
	mio_uint32_t expire;
	mio_uint32_t minimum;
};
typedef struct mio_dns_brrd_soa_t mio_dns_brrd_soa_t;

struct mio_dns_beopt_t
{
	mio_uint16_t code;
	mio_uint16_t dlen;
	void*        dptr;
};
typedef struct mio_dns_beopt_t mio_dns_beopt_t;

/* the full rcode must be given. the macro takes the upper 8 bits */
#define MIO_DNS_EDNS_MAKE_TTL(rcode,version,dnssecok) ((((((mio_uint32_t)rcode) >> 4) & 0xFF) << 24) | (((mio_uint32_t)version & 0xFF) << 16) | (((mio_uint32_t)dnssecok & 0x1) << 15))

struct mio_dns_bedns_t
{
	mio_uint16_t     uplen; /* udp payload len - will be placed in the qclass field of RR. */

	/* the ttl field(32 bits) of RR holds extended rcode, version, dnssecok */
	mio_uint8_t      version; 
	mio_uint8_t      dnssecok;

	mio_oow_t        beonum; /* option count */
	mio_dns_beopt_t* beoptr;
};
typedef struct mio_dns_bedns_t mio_dns_bedns_t;

/* ---------------------------------------------------------------- */

typedef struct mio_svc_dns_t mio_svc_dns_t; /* server service */
typedef struct mio_svc_dnc_t mio_svc_dnc_t; /* client service */
typedef struct mio_svc_dnr_t mio_svc_dnr_t; /* recursor service */

typedef void (*mio_svc_dnc_on_done_t) (
	mio_svc_dnc_t* dnc,
	mio_dns_msg_t* reqmsg,
	mio_errnum_t   status,
	const void*    data,
	mio_oow_t      len
);


typedef void (*mio_svc_dnc_on_resolve_t) (
	mio_svc_dnc_t* dnc,
	mio_dns_msg_t* reqmsg,
	mio_errnum_t   status,
	const void*    data,
	mio_oow_t      len
);



enum mio_svc_dnc_send_flag_t
{
	MIO_SVC_DNC_SEND_FLAG_PREFER_TCP = (1 << 0),
	MIO_SVC_DNC_SEND_FLAG_TCP_IF_TC  = (1 << 1), // retry over tcp if the truncated bit is set in an answer over udp.

	MIO_SVC_DNC_SEND_FLAG_ALL = (MIO_SVC_DNC_SEND_FLAG_PREFER_TCP | MIO_SVC_DNC_SEND_FLAG_TCP_IF_TC)
};
typedef enum mio_svc_dnc_send_flag_t mio_svc_dnc_send_flag_t;

enum mio_svc_dnc_resolve_flag_t
{
	MIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP = MIO_SVC_DNC_SEND_FLAG_PREFER_TCP,
	MIO_SVC_DNC_RESOLVE_FLAG_TCP_IF_TC  = MIO_SVC_DNC_SEND_FLAG_TCP_IF_TC,

	/* the following flag bits are resolver specific. it must not overlap with send flag bits */
	MIO_SVC_DNC_RESOLVE_FLAG_BRIEF      = (1 << 8),
	MIO_SVC_DNC_RESOLVE_FLAG_COOKIE     = (1 << 9),
	MIO_SVC_DNC_RESOLVE_FLAG_DNSSEC     = (1 << 10),

	MIO_SVC_DNC_RESOLVE_FLAG_ALL = (MIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP | MIO_SVC_DNC_RESOLVE_FLAG_TCP_IF_TC | MIO_SVC_DNC_RESOLVE_FLAG_BRIEF | MIO_SVC_DNC_RESOLVE_FLAG_COOKIE | MIO_SVC_DNC_RESOLVE_FLAG_DNSSEC)
};
typedef enum mio_svc_dnc_resolve_flag_t  mio_svc_dnc_resolve_flag_t;

/* ---------------------------------------------------------------- */

#define MIO_DNS_COOKIE_CLIENT_LEN (8)
#define MIO_DNS_COOKIE_SERVER_MIN_LEN (16)
#define MIO_DNS_COOKIE_SERVER_MAX_LEN (40)
#define MIO_DNS_COOKIE_MAX_LEN (MIO_DNS_COOKIE_CLIENT_LEN + MIO_DNS_COOKIE_SERVER_MAX_LEN)

typedef struct mio_dns_cookie_data_t mio_dns_cookie_data_t;
#include <mio-pac1.h>
struct mio_dns_cookie_data_t
{
	mio_uint8_t client[MIO_DNS_COOKIE_CLIENT_LEN];
	mio_uint8_t server[MIO_DNS_COOKIE_SERVER_MAX_LEN];
};
#include <mio-upac.h>

typedef struct mio_dns_cookie_t mio_dns_cookie_t;
struct mio_dns_cookie_t
{
	mio_dns_cookie_data_t data;
	mio_uint8_t client_len;
	mio_uint8_t server_len;
	mio_uint8_t key[16];
};

/* ---------------------------------------------------------------- */

struct mio_dns_pkt_info_t
{
	/* the following 5 fields are internal use only */
	mio_uint8_t* _start;
	mio_uint8_t* _end;
	mio_uint8_t* _ptr;
	mio_oow_t _rrdlen; /* length needed to store RRs decoded */
	mio_uint8_t* _rrdptr;

	/* you may access the following fields */
	mio_dns_bhdr_t hdr;

	struct
	{
		int exist;
		mio_uint16_t uplen; /* udp payload len - will be placed in the qclass field of RR. */
		mio_uint8_t  version; 
		mio_uint8_t  dnssecok;
		mio_dns_cookie_t cookie;
	} edns;

	mio_uint16_t qdcount; /* number of questions */
	mio_uint16_t ancount; /* number of answers (answer part) */
	mio_uint16_t nscount; /* number of name servers (authority part. only NS types) */
	mio_uint16_t arcount; /* number of additional resource (additional part) */

	struct
	{
		mio_dns_bqr_t* qd;
		mio_dns_brr_t* an;
		mio_dns_brr_t* ns;
		mio_dns_brr_t* ar;
	} rr;

};
typedef struct mio_dns_pkt_info_t mio_dns_pkt_info_t;


/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT mio_svc_dnc_t* mio_svc_dnc_start (
	mio_t*             mio,
	const mio_skad_t*  serv_addr, /* required */
	const mio_skad_t*  bind_addr, /* optional. can be MIO_NULL */
	const mio_ntime_t* send_tmout, /* required */
	const mio_ntime_t* reply_tmout, /* required */
	mio_oow_t          max_tries /* required */
);

MIO_EXPORT void mio_svc_dnc_stop (
	mio_svc_dnc_t* dnc
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_svc_dns_getmio(mio_svc_dns_t* svc) { return mio_svc_getmio((mio_svc_t*)svc); }
static MIO_INLINE mio_t* mio_svc_dnc_getmio(mio_svc_dnc_t* svc) { return mio_svc_getmio((mio_svc_t*)svc); }
static MIO_INLINE mio_t* mio_svc_dnr_getmio(mio_svc_dnr_t* svc) { return mio_svc_getmio((mio_svc_t*)svc); }
#else
#	define mio_svc_dns_getmio(svc) mio_svc_getmio(svc)
#	define mio_svc_dnc_getmio(svc) mio_svc_getmio(svc)
#	define mio_svc_dnr_getmio(svc) mio_svc_getmio(svc)
#endif

MIO_EXPORT mio_dns_msg_t* mio_svc_dnc_sendmsg (
	mio_svc_dnc_t*         dnc,
	mio_dns_bhdr_t*        bdns,
	mio_dns_bqr_t*         qr,
	mio_oow_t              qr_count,
	mio_dns_brr_t*         rr,
	mio_oow_t              rr_count,
	mio_dns_bedns_t*       edns,
	int                    send_flags,
	mio_svc_dnc_on_done_t  on_done,
	mio_oow_t              xtnsize
);

MIO_EXPORT mio_dns_msg_t* mio_svc_dnc_sendreq (
	mio_svc_dnc_t*         dnc,
	mio_dns_bhdr_t*        bdns,
	mio_dns_bqr_t*         qr,
	mio_dns_bedns_t*       edns,
	int                    send_flags,
	mio_svc_dnc_on_done_t  on_done,
	mio_oow_t              xtnsize
);


MIO_EXPORT mio_dns_msg_t* mio_svc_dnc_resolve (
	mio_svc_dnc_t*           dnc,
	const mio_bch_t*         qname,
	mio_dns_rrt_t            qtype,
	int                      resolve_flags,
	mio_svc_dnc_on_resolve_t on_resolve,
	mio_oow_t                xtnsize
);

/*
 * -1: cookie in the request but no client cookie in the response. this may be ok or not ok depending on your policy 
 * 0: client cookie mismatch in the request in the response
 * 1: client cookie match in the request in the response
 * 2: no client cookie in the requset. so it deson't case about the response 
 */
MIO_EXPORT int mio_svc_dnc_checkclientcookie (
	mio_svc_dnc_t*      dnc,
	mio_dns_msg_t*      reqmsg,
	mio_dns_pkt_info_t* respi
);

/* ---------------------------------------------------------------- */

MIO_EXPORT mio_dns_pkt_info_t* mio_dns_make_pkt_info (
	mio_t*                mio,
	const mio_dns_pkt_t*  pkt,
	mio_oow_t             len
);

MIO_EXPORT void mio_dns_free_pkt_info (
	mio_t*                mio,
	mio_dns_pkt_info_t*   pi
);

/* ---------------------------------------------------------------- */

#if defined(MIO_HAVE_INLINE)
	static MIO_INLINE mio_dns_pkt_t* mio_dns_msg_to_pkt (mio_dns_msg_t* msg) { return (mio_dns_pkt_t*)(msg + 1); }
#else
#	define mio_dns_msg_to_pkt(msg) ((mio_dns_pkt_t*)((mio_dns_msg_t*)(msg) + 1))
#endif

MIO_EXPORT mio_dns_msg_t* mio_dns_make_msg (
	mio_t*                mio,
	mio_dns_bhdr_t*       bhdr,
	mio_dns_bqr_t*        qr,
	mio_oow_t             qr_count,
	mio_dns_brr_t*        rr,
	mio_oow_t             rr_count,
	mio_dns_bedns_t*      edns,
	mio_oow_t             xtnsize
);

MIO_EXPORT void mio_dns_free_msg (
	mio_t*                mio,
	mio_dns_msg_t*        msg
);

/* 
 * return the pointer to the client cookie data in the packet.
 * if cookie is not MIO_NULL, it copies the client cookie there.
 */
MIO_EXPORT mio_uint8_t* mio_dns_find_client_cookie_in_msg (
	mio_dns_msg_t* reqmsg,
	mio_uint8_t  (*cookie)[MIO_DNS_COOKIE_CLIENT_LEN]
);

MIO_EXPORT mio_bch_t* mio_dns_rcode_to_bcstr (
	mio_dns_rcode_t rcode
);

#if defined(__cplusplus)
}
#endif

#endif
