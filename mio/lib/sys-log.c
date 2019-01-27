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

#include "mio-prv.h"

#if defined(_WIN32)
#	include <windows.h>
#	include <time.h>
#	include <errno.h>
#	include <io.h>
#	include <fcntl.h>

#elif defined(__OS2__)
#	define INCL_DOSDATETIME
#	include <os2.h>
#	include <time.h>
#	include <errno.h>
#	include <io.h>
#	include <fcntl.h>

#elif defined(__DOS__)
#	include <dos.h>
#	include <time.h>
#	include <errno.h>
#	include <io.h>
#	include <fcntl.h>

#elif defined(macintosh)
#	include <Types.h>
#	include <OSUtils.h>
#	include <Timer.h>
#	include <MacErrors.h>

	/* TODO: a lot to do */

#elif defined(vms) || defined(__vms)
#	define __NEW_STARLET 1
#	include <starlet.h> /* (SYS$...) */
#	include <ssdef.h> /* (SS$...) */
#	include <lib$routines.h> /* (lib$...) */

	/* TODO: a lot to do */

#else
#	include <sys/types.h>
#	include <unistd.h>
#	include <fcntl.h>
#	include <errno.h>

#	if defined(HAVE_TIME_H)
#		include <time.h>
#	endif
#	if defined(HAVE_SYS_TIME_H)
#		include <sys/time.h>
#	endif

#endif

struct mio_sys_log_t
{
	struct
	{
		int fd;
		int fd_flag; /* bitwise OR'ed fo logfd_flag_t bits */

		struct
		{
			mio_bch_t buf[4096];
			mio_oow_t len;
		} out;
	} log;
};

typedef mio_sys_log_t xtn_t;
#define GET_XTN(mio) (&(mio)->sys.log)


enum logfd_flag_t
{
	LOGFD_TTY = (1 << 0),
	LOGFD_OPENED_HERE = (1 << 1)
};

static int write_all (int fd, const mio_bch_t* ptr, mio_oow_t len)
{
	while (len > 0)
	{
		mio_ooi_t wr;

		wr = write(fd, ptr, len);

		if (wr <= -1)
		{
		#if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN == EWOULDBLOCK)
			if (errno == EAGAIN) continue;
		#else
			#if defined(EAGAIN)
			if (errno == EAGAIN) continue;
			#elif defined(EWOULDBLOCK)
			if (errno == EWOULDBLOCK) continue;
			#endif
		#endif

		#if defined(EINTR)
			/* TODO: would this interfere with non-blocking nature of this VM? */
			if (errno == EINTR) continue;
		#endif
			return -1;
		}

		ptr += wr;
		len -= wr;
	}

	return 0;
}

static int write_log (mio_t* mio, int fd, const mio_bch_t* ptr, mio_oow_t len)
{
	xtn_t* xtn = GET_XTN(mio);

	while (len > 0)
	{
		if (xtn->log.out.len > 0)
		{
			mio_oow_t rcapa, cplen;

			rcapa = MIO_COUNTOF(xtn->log.out.buf) - xtn->log.out.len;
			cplen = (len >= rcapa)? rcapa: len;

			MIO_MEMCPY (&xtn->log.out.buf[xtn->log.out.len], ptr, cplen);
			xtn->log.out.len += cplen;
			ptr += cplen;
			len -= cplen;

			if (xtn->log.out.len >= MIO_COUNTOF(xtn->log.out.buf))
			{
				int n;
				n = write_all(fd, xtn->log.out.buf, xtn->log.out.len);
				xtn->log.out.len = 0;
				if (n <= -1) return -1;
			}
		}
		else
		{
			mio_oow_t rcapa;

			rcapa = MIO_COUNTOF(xtn->log.out.buf);
			if (len >= rcapa)
			{
				if (write_all(fd, ptr, rcapa) <= -1) return -1;
				ptr += rcapa;
				len -= rcapa;
			}
			else
			{
				MIO_MEMCPY (xtn->log.out.buf, ptr, len);
				xtn->log.out.len += len;
				ptr += len;
				len -= len;
				
			}
		}
	}

	return 0;
}

static void flush_log (mio_t* mio, int fd)
{
	xtn_t* xtn = GET_XTN(mio);
	if (xtn->log.out.len > 0)
	{
		write_all (fd, xtn->log.out.buf, xtn->log.out.len);
		xtn->log.out.len = 0;
	}
}

void mio_sys_writelog (mio_t* mio, mio_bitmask_t mask, const mio_ooch_t* msg, mio_oow_t len)
{
	xtn_t* xtn = GET_XTN(mio);
	mio_bch_t buf[256];
	mio_oow_t ucslen, bcslen, msgidx;
	int n, logfd;

	if (mask & MIO_LOG_STDERR)
	{
		/* the messages that go to STDERR don't get masked out */
		logfd = 2;
	}
	else
	{
#if 0
		if (!(xtn->log.mask & mask & ~MIO_LOG_ALL_LEVELS)) return;  /* check log types */
		if (!(xtn->log.mask & mask & ~MIO_LOG_ALL_TYPES)) return;  /* check log levels */
#endif

		if (mask & MIO_LOG_STDOUT) logfd = 1;
		else
		{
			logfd = xtn->log.fd;
			if (logfd <= -1) return;
		}
	}

/* TODO: beautify the log message.
 *       do classification based on mask. */
	if (!(mask & (MIO_LOG_STDOUT | MIO_LOG_STDERR)))
	{
		time_t now;
		char ts[32];
		size_t tslen;
		struct tm tm, *tmp;

		now = time(MIO_NULL);
	#if defined(_WIN32)
		tmp = localtime(&now);
		tslen = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S %z ", tmp);
		if (tslen == 0) 
		{
			tslen = sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d ", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
		}
	#elif defined(__OS2__)
		#if defined(__WATCOMC__)
		tmp = _localtime(&now, &tm);
		#else
		tmp = localtime(&now);
		#endif

		#if defined(__BORLANDC__)
		/* the borland compiler doesn't handle %z properly - it showed 00 all the time */
		tslen = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S %Z ", tmp);
		#else
		tslen = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S %z ", tmp);
		#endif
		if (tslen == 0) 
		{
			tslen = sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d ", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
		}

	#elif defined(__DOS__)
		tmp = localtime(&now);
		/* since i know that %z/%Z is not available in strftime, i switch to sprintf immediately */
		tslen = sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d ", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
	#else
		#if defined(HAVE_LOCALTIME_R)
		tmp = localtime_r(&now, &tm);
		#else
		tmp = localtime(&now);
		#endif

		#if defined(HAVE_STRFTIME_SMALL_Z)
		tslen = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S %z ", tmp);
		#else
		tslen = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S %Z ", tmp); 
		#endif
		if (tslen == 0) 
		{
			tslen = sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d ", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
		}
	#endif
		write_log (mio, logfd, ts, tslen);
	}

	if (logfd == xtn->log.fd && (xtn->log.fd_flag & LOGFD_TTY))
	{
		if (mask & MIO_LOG_FATAL) write_log (mio, logfd, "\x1B[1;31m", 7);
		else if (mask & MIO_LOG_ERROR) write_log (mio, logfd, "\x1B[1;32m", 7);
		else if (mask & MIO_LOG_WARN) write_log (mio, logfd, "\x1B[1;33m", 7);
	}

#if defined(MIO_OOCH_IS_UCH)
	msgidx = 0;
	while (len > 0)
	{
		ucslen = len;
		bcslen = MIO_COUNTOF(buf);

		n = mio_convootobchars(mio, &msg[msgidx], &ucslen, buf, &bcslen);
		if (n == 0 || n == -2)
		{
			/* n = 0: 
			 *   converted all successfully 
			 * n == -2: 
			 *    buffer not sufficient. not all got converted yet.
			 *    write what have been converted this round. */

			MIO_ASSERT (mio, ucslen > 0); /* if this fails, the buffer size must be increased */

			/* attempt to write all converted characters */
			if (write_log(mio, logfd, buf, bcslen) <= -1) break;

			if (n == 0) break;
			else
			{
				msgidx += ucslen;
				len -= ucslen;
			}
		}
		else if (n <= -1)
		{
			/* conversion error */
			break;
		}
	}
#else
	write_log (mio, logfd, msg, len);
#endif

	if (logfd == xtn->log.fd && (xtn->log.fd_flag & LOGFD_TTY))
	{
		if (mask & (MIO_LOG_FATAL | MIO_LOG_ERROR | MIO_LOG_WARN)) write_log (mio, logfd, "\x1B[0m", 4);
	}

	flush_log (mio, logfd);
}

static MIO_INLINE void reset_log_to_default (xtn_t* xtn)
{
#if defined(ENABLE_LOG_INITIALLY)
	xtn->log.fd = 2;
	xtn->log.fd_flag = 0;
	#if defined(HAVE_ISATTY)
	if (isatty(xtn->log.fd)) xtn->log.fd_flag |= LOGFD_TTY;
	#endif
#else
	xtn->log.fd = -1;
	xtn->log.fd_flag = 0;
#endif
}

void mio_sys_initlog (mio_t* mio)
{
	xtn_t* xtn = GET_XTN(mio);
	mio_bitmask_t logmask;
	mio_oow_t pathlen;

/* TODO: */
#define LOG_FILE "/dev/stderr"
	xtn->log.fd = open(LOG_FILE, O_CREAT | O_WRONLY | O_APPEND , 0644);
	if (xtn->log.fd == -1)
	{
		/*mio_seterrbfmtwithsyserr (mio, 0, errno, "cannot open log file %hs", LOG_FILE);*/
		xtn->log.fd = 2;
		xtn->log.fd_flag = 0;
	}
	else
	{
		xtn->log.fd_flag |= LOGFD_OPENED_HERE;
	}

#if defined(HAVE_ISATTY)
	if (isatty(xtn->log.fd)) xtn->log.fd_flag |= LOGFD_TTY;
#endif

	logmask = MIO_LOG_ALL_TYPES | MIO_LOG_ALL_LEVELS;
	mio_setoption (mio, MIO_LOG_MASK, &logmask);
}

void mio_sys_finilog (mio_t* mio)
{
	xtn_t* xtn = GET_XTN(mio);
	if ((xtn->log.fd_flag & LOGFD_OPENED_HERE) && xtn->log.fd >= 0) close (xtn->log.fd);
	reset_log_to_default (xtn);
}