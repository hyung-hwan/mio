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

#ifndef _MIO_JSON_H_
#define _MIO_JSON_H_

#include <mio.h>

/** 
 * The mio_json_t type defines a simple json parser.
 */
typedef struct mio_json_t mio_json_t;

/* ========================================================================= */

enum mio_json_state_t
{
	MIO_JSON_STATE_START,
	MIO_JSON_STATE_IN_ARRAY,
	MIO_JSON_STATE_IN_OBJECT,

	MIO_JSON_STATE_IN_WORD_VALUE,
	MIO_JSON_STATE_IN_NUMERIC_VALUE,
	MIO_JSON_STATE_IN_STRING_VALUE
};
typedef enum mio_json_state_t mio_json_state_t;

/* ========================================================================= */
enum mio_json_inst_t
{
	MIO_JSON_INST_START_ARRAY,
	MIO_JSON_INST_END_ARRAY,
	MIO_JSON_INST_START_OBJECT,
	MIO_JSON_INST_END_OBJECT,

	MIO_JSON_INST_KEY,

	MIO_JSON_INST_STRING,
	MIO_JSON_INST_NUMBER,
	MIO_JSON_INST_NIL,
	MIO_JSON_INST_TRUE,
	MIO_JSON_INST_FALSE,
};
typedef enum mio_json_inst_t mio_json_inst_t;

typedef int (*mio_json_instcb_t) (
	mio_json_t*           json,
	mio_json_inst_t       inst,
	mio_oow_t             level,
	mio_oow_t             index,
	mio_json_state_t      container_state,
	const mio_oocs_t*     str,
	void*                 ctx
);


typedef struct mio_json_state_node_t mio_json_state_node_t;
struct mio_json_state_node_t
{
	mio_json_state_t state;
	mio_oow_t level;
	mio_oow_t index;
	int in_comment;
	union
	{
		struct
		{
			int got_value;
		} ia; /* in array */

		struct
		{
			/* 0: ready to get key (at the beginning or got comma), 
			 * 1: got key, 2: got colon, 3: got value */
			int state; 
		} io; /* in object */
		struct
		{
			int escaped;
			int digit_count;
			/* acc is always of unicode type to handle \u and \U. 
			 * in the bch mode, it will get converted to a utf8 stream. */
			mio_uch_t acc;
		} sv;
		struct
		{
			int escaped;
			int digit_count;
			/* for a character, no way to support the unicode character
			 * in the bch mode */
			mio_ooch_t acc; 
		} cv;
		struct
		{
			int progress;
		} nv;
	} u;
	mio_json_state_node_t* next;
};

enum mio_json_option_t
{
	/* allow an unquoted word as an object key */
	MIO_JSON_PERMIT_WORD_KEY  = ((mio_bitmask_t)1 << 0), 

	/* a comma as a separator is not mandatory */
	MIO_JSON_OPTIONAL_COMMA   = ((mio_bitmask_t)1 << 1),

	/* support the line comment. the text beginning with # is a comment to the end of the line */
	MIO_JSON_LINE_COMMENT   = ((mio_bitmask_t)1 << 2)
};

typedef enum mio_json_option_t mio_json_option_t;

struct mio_json_t
{
	mio_t* mio;
	mio_json_instcb_t instcb;
	void* rctx;
	mio_bitmask_t option;

	mio_json_state_node_t state_top;
	mio_json_state_node_t* state_stack;

	mio_oocs_t tok;
	mio_oow_t tok_capa;
	mio_oow_t tok_line;
	mio_oow_t tok_col;

	mio_oow_t c_line;
	mio_oow_t c_col;
};

/* ========================================================================= */

typedef struct mio_jsonwr_t mio_jsonwr_t;

typedef int (*mio_jsonwr_writecb_t) (
	mio_jsonwr_t*         jsonwr,
	const mio_bch_t*      dptr,
	mio_oow_t             dlen,
	void*                 ctx
);

typedef struct mio_jsonwr_state_node_t mio_jsonwr_state_node_t;
struct mio_jsonwr_state_node_t
{
	mio_json_state_t state;
	mio_oow_t level;
	mio_oow_t index;
	int obj_awaiting_val;
	mio_jsonwr_state_node_t* next;
};

enum mio_jsonwr_flag_t
{
	MIO_JSONWR_FLAG_PRETTY = (1 << 0)
};
typedef enum mio_jsonwr_flag_t mio_jsonwr_flag_t;

struct mio_jsonwr_t
{
	mio_t* mio;
	mio_jsonwr_writecb_t writecb;
	mio_jsonwr_state_node_t state_top;
	mio_jsonwr_state_node_t* state_stack;
	int flags;

	void* wctx;
	mio_bch_t wbuf[8192];
	mio_oow_t wbuf_len;
};

/* ========================================================================= */

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT mio_json_t* mio_json_open (
	mio_t*             mio,
	mio_oow_t          xtnsize
);

MIO_EXPORT void mio_json_close (
	mio_json_t* json
);

MIO_EXPORT int mio_json_init (
	mio_json_t* json,
	mio_t*      mio
);

MIO_EXPORT void mio_json_fini (
	mio_json_t* json
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_json_getmio (mio_json_t* json) { return json->mio; }
#else
#	define mio_json_getmio(json) (((mio_json_t*)(json))->mio)
#endif

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_json_getxtn (mio_json_t* json) { return (void*)(json + 1); }
#else
#define mio_json_getxtn(json) ((void*)((mio_json_t*)(json) + 1))
#endif

MIO_EXPORT mio_bitmask_t mio_json_getoption (
	mio_json_t*       json
);

MIO_EXPORT void mio_json_setoption (
	mio_json_t*       json,
	mio_bitmask_t     mask
);

MIO_EXPORT void mio_json_setinstcb (
	mio_json_t*       json,
	mio_json_instcb_t instcb,
	void*             ctx
);

MIO_EXPORT mio_json_state_t mio_json_getstate (
	mio_json_t*   json
);

MIO_EXPORT void mio_json_resetstates (
	mio_json_t*   json
);

MIO_EXPORT void mio_json_resetfeedloc (
	mio_json_t*   json
);

/**
 * The mio_json_feed() function processes the raw data.
 *
 * If stop_if_ever_complted is 0, it returns 0 on success. If the value pointed to by
 * rem is greater 0 after the call, processing is not complete and more feeding is 
 * required. Incomplete feeding may be caused by incomplete byte sequences or incomplete
 * json object.
 *
 * If stop_if_ever_completed is non-zero, it returns 0 upon incomplet byte sequence or
 * incomplete json object. It returns 1 if it sees the first complete json object. It stores
 * the size of remaning raw data in the memory pointed to by rem.
 * 
 * The function returns -1 upon failure.
 */
MIO_EXPORT int mio_json_feed (
	mio_json_t*   json,
	const void*   ptr,
	mio_oow_t     len,
	mio_oow_t*    rem,
	int stop_if_ever_completed
);

/* ========================================================================= */

MIO_EXPORT mio_jsonwr_t* mio_jsonwr_open (
	mio_t*             mio,
	mio_oow_t          xtnsize,
	int                flags
);

MIO_EXPORT void mio_jsonwr_close (
	mio_jsonwr_t* jsonwr
);

MIO_EXPORT int mio_jsonwr_init (
	mio_jsonwr_t* jsonwr,
	mio_t*        mio,
	int           flags
);

MIO_EXPORT void mio_jsonwr_fini (
	mio_jsonwr_t* jsonwr
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE mio_t* mio_jsonwr_getmio (mio_jsonwr_t* jsonwr) { return jsonwr->mio; }
#else
#	define mio_jsonwr_getmio(jsonwr) (((mio_jsonwr_t*)(jsonwr))->mio)
#endif

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_jsonwr_getxtn (mio_jsonwr_t* jsonwr) { return (void*)(jsonwr + 1); }
#else
#define mio_jsonwr_getxtn(jsonwr) ((void*)((mio_jsonwr_t*)(jsonwr) + 1))
#endif

MIO_EXPORT void mio_jsonwr_setwritecb (
	mio_jsonwr_t*        jsonwr,
	mio_jsonwr_writecb_t writecb,
	void*                ctx
);

MIO_EXPORT int mio_jsonwr_write (
	mio_jsonwr_t*   jsonwr,
	mio_json_inst_t inst,
	int             is_uchars,
	const void*     dptr,
	mio_oow_t       dlen
);



#define mio_jsonwr_startarray(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_START_ARRAY, 0, MIO_NULL, 0)
#define mio_jsonwr_endarray(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_END_ARRAY, 0, MIO_NULL, 0)

#define mio_jsonwr_startobject(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_START_OBJECT, 0, MIO_NULL, 0)
#define mio_jsonwr_endobject(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_END_OBJECT, 0, MIO_NULL, 0)

#define mio_jsonwr_writenil(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_NIL, 0, MIO_NULL, 0)
#define mio_jsonwr_writetrue(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_TRUE, 0, MIO_NULL, 0)
#define mio_jsonwr_writefalse(jsonwr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_FALSE, 0, MIO_NULL, 0)

#define mio_jsonwr_writekeywithuchars(jsonwr,dptr,dlen) mio_jsonwr_write(jsonwr, MIO_JSON_INST_KEY, 1, dptr, dlen)
#define mio_jsonwr_writekeywithbchars(jsonwr,dptr,dlen) mio_jsonwr_write(jsonwr, MIO_JSON_INST_KEY, 0, dptr, dlen)

#define mio_jsonwr_writekeywithucstr(jsonwr,dptr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_KEY, 1, dptr, mio_count_ucstr(dptr))
#define mio_jsonwr_writekeywithbcstr(jsonwr,dptr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_KEY, 0, dptr, mio_count_bcstr(dptr))

#define mio_jsonwr_writenumberwithuchars(jsonwr,dptr,dlen) mio_jsonwr_write(jsonwr, MIO_JSON_INST_NUMBER, 1, dptr, dlen)
#define mio_jsonwr_writenumberwithbchars(jsonwr,dptr,dlen) mio_jsonwr_write(jsonwr, MIO_JSON_INST_NUMBER, 0, dptr, dlen)

#define mio_jsonwr_writenumberwithucstr(jsonwr,dptr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_NUMBER, 1, dptr, mio_count_ucstr(dptr))
#define mio_jsonwr_writenumberwithbcstr(jsonwr,dptr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_NUMBER, 0, dptr, mio_count_bcstr(dptr))

#define mio_jsonwr_writestringwithuchars(jsonwr,dptr,dlen) mio_jsonwr_write(jsonwr, MIO_JSON_INST_STRING, 1, dptr, dlen)
#define mio_jsonwr_writestringwithbchars(jsonwr,dptr,dlen) mio_jsonwr_write(jsonwr, MIO_JSON_INST_STRING, 0, dptr, dlen)

#define mio_jsonwr_writestringwithucstr(jsonwr,dptr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_STRING, 1, dptr, mio_count_ucstr(dptr))
#define mio_jsonwr_writestringwithbcstr(jsonwr,dptr) mio_jsonwr_write(jsonwr, MIO_JSON_INST_STRING, 0, dptr, mio_count_bcstr(dptr))

MIO_EXPORT int mio_jsonwr_writeintmax (
	mio_jsonwr_t*     jsonwr,
	mio_intmax_t      v
);

MIO_EXPORT int mio_jsonwr_writeuintmax (
	mio_jsonwr_t*     jsonwr,
	mio_uintmax_t     v
);

MIO_EXPORT int mio_jsonwr_writerawuchars (
	mio_jsonwr_t*     jsonwr,
	const mio_uch_t*  dptr,
	mio_oow_t         dlen
);

MIO_EXPORT int mio_jsonwr_writerawucstr (
	mio_jsonwr_t*     jsonwr,
	const mio_uch_t*  dptr
);

MIO_EXPORT int mio_jsonwr_writerawbchars (
	mio_jsonwr_t*     jsonwr,
	const mio_bch_t*  dptr,
	mio_oow_t         dlen
);

MIO_EXPORT int mio_jsonwr_writerawbcstr (
	mio_jsonwr_t*     jsonwr,
	const mio_bch_t*  dptr
);

#if defined(__cplusplus)
}
#endif

#endif
