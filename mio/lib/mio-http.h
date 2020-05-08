/*
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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MIO_HTTP_H_
#define _MIO_HTTP_H_

#include <mio-ecs.h>
#include <mio-sck.h>

/** \file
 * This file provides basic data types and functions for the http protocol.
 */

/**
 * The mio_http_version_t type defines http version.
 */
struct mio_http_version_t
{
	short major; /**< major version */
	short minor; /**< minor version */
};

typedef struct mio_http_version_t mio_http_version_t;

/**
 * The mio_http_method_t type defines http methods .
 */
enum mio_http_method_t
{
	MIO_HTTP_OTHER,

	/* rfc 2616 */
	MIO_HTTP_HEAD,
	MIO_HTTP_GET,
	MIO_HTTP_POST,
	MIO_HTTP_PUT,
	MIO_HTTP_DELETE,
	MIO_HTTP_OPTIONS,
	MIO_HTTP_TRACE,
	MIO_HTTP_CONNECT

#if 0
	/* rfc 2518 */
	MIO_HTTP_PROPFIND,
	MIO_HTTP_PROPPATCH,
	MIO_HTTP_MKCOL,
	MIO_HTTP_COPY,
	MIO_HTTP_MOVE,
	MIO_HTTP_LOCK,
	MIO_HTTP_UNLOCK,

	/* rfc 3253 */
	MIO_HTTP_VERSION_CONTROL,
	MIO_HTTP_REPORT,
	MIO_HTTP_CHECKOUT,
	MIO_HTTP_CHECKIN,
	MIO_HTTP_UNCHECKOUT,
	MIO_HTTP_MKWORKSPACE,
	MIO_HTTP_UPDATE,
	MIO_HTTP_LABEL,
	MIO_HTTP_MERGE,
	MIO_HTTP_BASELINE_CONTROL,
	MIO_HTTP_MKACTIVITY,
	
	/* microsoft */
	MIO_HTTP_BPROPFIND,
	MIO_HTTP_BPROPPATCH,
	MIO_HTTP_BCOPY,
	MIO_HTTP_BDELETE,
	MIO_HTTP_BMOVE,
	MIO_HTTP_NOTIFY,
	MIO_HTTP_POLL,
	MIO_HTTP_SUBSCRIBE,
	MIO_HTTP_UNSUBSCRIBE,
#endif
};

typedef enum mio_http_method_t mio_http_method_t;

/** 
 * The #mio_http_range_int_t type defines an integer that can represent
 * a range offset. Depening on the size of #mio_foff_t, it is defined to
 * either #mio_foff_t or #mio_ulong_t.
 */
#if defined(MIO_SIZEOF_FOFF_T) && defined(MIO_SIZEOF_UINTMAX_T) && (MIO_SIZEOF_FOFF_T > MIO_SIZEOF_UINTMAX_T)
typedef mio_foff_t mio_http_range_int_t;
#else
typedef mio_uintmax_t mio_http_range_int_t;
#endif

enum mio_http_range_type_t
{
	MIO_HTTP_RANGE_NONE,
	MIO_HTTP_RANGE_PROPER,
	MIO_HTTP_RANGE_SUFFIX
};
typedef enum mio_http_range_type_t mio_http_range_type_t;
/**
 * The mio_http_range_t type defines a structure that can represent
 * a value for the \b Range: http header. 
 *
 * If type is #MIO_HTTP_RANGE_NONE, this range is not valid.
 * 
 * If type is #MIO_HTTP_RANGE_SUFFIX, 'from' is meaningleass and 'to' indicates 
 * the number of bytes from the back. 
 *  - -500    => last 500 bytes
 *
 * You should adjust a range when the size that this range belongs to is 
 * made known. See this code:
 * \code
 *  range.from = total_size - range.to;
 *  range.to = range.to + range.from - 1;
 * \endcode
 *
 * If type is #MIO_HTTP_RANGE_PROPER, 'from' and 'to' represents a proper range
 * where the value of 0 indicates the first byte. This doesn't require any 
 * adjustment.
 *  - 0-999   => first 1000 bytes
 *  - 99-     => from the 100th bytes to the end.
 */
struct mio_http_range_t
{
	mio_http_range_type_t type; /**< type indicator */
	mio_http_range_int_t from;  /**< starting offset */
	mio_http_range_int_t to;    /**< ending offset */
};
typedef struct mio_http_range_t mio_http_range_t;


enum mio_perenc_http_opt_t
{
	MIO_PERENC_HTTP_KEEP_SLASH = (1 << 0)
};
typedef enum mio_perenc_http_opt_t mio_perenc_bcstr_opt_t;


/* -------------------------------------------------------------- */
typedef struct mio_svc_htts_t mio_svc_htts_t;
typedef struct mio_svc_httc_t mio_svc_httc_t;

/* -------------------------------------------------------------- */

typedef struct mio_svc_htts_rsrc_t mio_svc_htts_rsrc_t;
typedef mio_uint64_t mio_foff_t ; /* TODO: define this via the main configure.ac ... */

typedef int (*mio_svc_htts_rsrc_on_write_t) (
	mio_svc_htts_rsrc_t* rsrc,
	mio_dev_sck_t*       sck
);

typedef void (*mio_svc_htts_rsrc_on_kill_t) (
	mio_svc_htts_rsrc_t* rsrc
);

enum mio_svc_htts_rsrc_flag_t
{
	MIO_SVC_HTTS_RSRC_FLAG_CONTENT_LENGTH_SET = (1 << 0)
};
typedef enum mio_svc_htts_rsrc_flag_t mio_svc_htts_rsrc_flag_t;

struct mio_svc_htts_rsrc_t
{
	mio_svc_htts_t* htts;
//	mio_svc_htts_rsrc_t* rsrc_prev;
//	mio_svc_htts_rsrc_t* rsrc_next;

	int flags;
	mio_bch_t* content_type;
	mio_foff_t content_length;

	mio_svc_htts_rsrc_on_write_t on_write;
	mio_svc_htts_rsrc_on_kill_t on_kill;
};

/* -------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_comp_http_versions (
	const mio_http_version_t* v1,
	const mio_http_version_t* v2
);

MIO_EXPORT const mio_bch_t* mio_http_status_to_bcstr (
	int code
);

MIO_EXPORT const mio_bch_t* mio_http_method_to_bcstr (
	mio_http_method_t type
);

MIO_EXPORT mio_http_method_t mio_bcstr_to_http_method (
	const mio_bch_t* name
);

MIO_EXPORT mio_http_method_t mio_bchars_to_http_method (
	const mio_bch_t* nameptr,
	mio_oow_t        namelen
);

MIO_EXPORT int mio_parse_http_range_bcstr (
	const mio_bch_t*  str,
	mio_http_range_t* range
);

MIO_EXPORT int mio_parse_http_time_bcstr (
	const mio_bch_t* str,
	mio_ntime_t*     nt
);

MIO_EXPORT mio_bch_t* mio_fmt_http_time_to_bcstr (
	const mio_ntime_t* nt,
	mio_bch_t*         buf,
	mio_oow_t          bufsz
);

/**
 * The mio_is_perenced_http_bcstr() function determines if the given string
 * contains a valid percent-encoded sequence.
 */
MIO_EXPORT int mio_is_perenced_http_bcstr (
	const mio_bch_t* str
);

/**
 * The mio_perdec_http_bcstr() function performs percent-decoding over a string.
 * The caller must ensure that the output buffer \a buf is large enough.
 * If \a ndecs is not #MIO_NULL, it is set to the number of characters
 * decoded.  0 means no characters in the input string required decoding
 * \return the length of the output string.
 */
MIO_EXPORT mio_oow_t mio_perdec_http_bcstr (
	const mio_bch_t* str, 
	mio_bch_t*       buf,
	mio_oow_t*       ndecs
);

/**
 * The mio_perenc_http_bcstr() function performs percent-encoding over a string.
 * The caller must ensure that the output buffer \a buf is large enough.
 * If \a nencs is not #MIO_NULL, it is set to the number of characters
 * encoded.  0 means no characters in the input string required encoding.
 * \return the length of the output string.
 */
MIO_EXPORT mio_oow_t mio_perenc_http_bcstr (
	int              opt, /**< 0 or bitwise-OR'ed of #mio_perenc_http_bcstr_opt_t */
	const mio_bch_t* str, 
	mio_bch_t*       buf,
	mio_oow_t*       nencs
);

#if 0
/* TODO: rename this function according to the naming convension */
MIO_EXPORT mio_bch_t* mio_perenc_http_bcstrdup (
	int                opt, /**< 0 or bitwise-OR'ed of #mio_perenc_http_bcstr_opt_t */
	const mio_bch_t*   str, 
	mio_mmgr_t*        mmgr
);
#endif

/* ------------------------------------------------------------------------- */
/* HTTP SERVER SERVICE                                                       */
/* ------------------------------------------------------------------------- */

MIO_EXPORT mio_svc_htts_t* mio_svc_htts_start (
	mio_t*            mio,
	const mio_skad_t* bind_addr
);

MIO_EXPORT void mio_svc_htts_stop (
	mio_svc_htts_t* htts
);

MIO_EXPORT int mio_svc_htts_setservernamewithbcstr (
	mio_svc_htts_t*  htts,
	const mio_bch_t* server_name

);

MIO_EXPORT int mio_svc_htts_sendfile (
	mio_svc_htts_t*           htts,
	mio_dev_sck_t*            csck,
	const mio_bch_t*          file_path,
	int                       status_code,
	mio_http_method_t         method,
	const mio_http_version_t* version,
	int                       keepalive
);

MIO_EXPORT int mio_svc_htts_sendstatus (
	mio_svc_htts_t*           htts,
	mio_dev_sck_t*            csck,
	int                       status_code,
	mio_http_method_t         method,
	const mio_http_version_t* version,
	int                       keepalive,
	void*                     extra
);

MIO_EXPORT void mio_svc_htts_fmtgmtime (
	mio_svc_htts_t*           htts,
	const mio_ntime_t*        nt,
	mio_bch_t*                buf,
	mio_oow_t                 len
);

#if defined(__cplusplus)
}
#endif


#endif
