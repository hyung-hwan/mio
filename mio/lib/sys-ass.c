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

#include "mio-prv.h"

#if defined(_WIN32)
#	include <windows.h>
#	include <errno.h>

#elif defined(__OS2__)
#	define INCL_DOSERRORS
#	include <os2.h>
#	include <errno.h>

#elif defined(__DOS__)
#	include <dos.h>
#	include <errno.h>

#	if defined(_INTELC32_)
#		define DOS_EXIT 0x4C
#		include <i32.h>
#		include <stk.h>
#	else
#		include <dosfunc.h>
#	endif

#elif defined(macintosh)
#	include <Types.h>
#	include <OSUtils.h>
#	include <Timer.h>

#	include <MacErrors.h>
#	include <Process.h>
#	include <Dialogs.h>
#	include <TextUtils.h>

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
#	include <errno.h>
#	include <signal.h>
#	include <stdlib.h>
#endif

#if defined(MIO_BUILD_RELEASE)

void mio_sys_assertfail (mio_t* mio, const mio_bch_t* expr, const mio_bch_t* file, mio_oow_t line)
{
	/* do nothing */
}

#else /* defined(MIO_BUILD_RELEASE) */

#if defined(MIO_ENABLE_LIBUNWIND)
#include <libunwind.h>
static void backtrace_stack_frames (mio_t* mio)
{
	unw_cursor_t cursor;
	unw_context_t context;
	int n;

	unw_getcontext(&context);
	unw_init_local(&cursor, &context);

	mio_logbfmt (mio, MIO_LOG_UNTYPED | MIO_LOG_DEBUG, "[BACKTRACE]\n");
	for (n = 0; unw_step(&cursor) > 0; n++) 
	{
		unw_word_t ip, sp, off;
		char symbol[256];

		unw_get_reg (&cursor, UNW_REG_IP, &ip);
		unw_get_reg (&cursor, UNW_REG_SP, &sp);

		if (unw_get_proc_name(&cursor, symbol, MIO_COUNTOF(symbol), &off)) 
		{
			mio_copy_bcstr (symbol, MIO_COUNTOF(symbol), "<unknown>");
		}

		mio_logbfmt (mio, MIO_LOG_UNTYPED | MIO_LOG_DEBUG, 
			"#%02d ip=0x%*p sp=0x%*p %s+0x%zu\n", 
			n, MIO_SIZEOF(void*) * 2, (void*)ip, MIO_SIZEOF(void*) * 2, (void*)sp, symbol, (mio_oow_t)off);
	}
}
#elif defined(HAVE_BACKTRACE)
#include <execinfo.h>
static void backtrace_stack_frames (mio_t* mio)
{
	void* btarray[128];
	mio_oow_t btsize;
	char** btsyms;

	btsize = backtrace (btarray, MIO_COUNTOF(btarray));
	btsyms = backtrace_symbols (btarray, btsize);
	if (btsyms)
	{
		mio_oow_t i;
		mio_logbfmt (mio, MIO_LOG_UNTYPED | MIO_LOG_DEBUG, "[BACKTRACE]\n");

		for (i = 0; i < btsize; i++)
		{
			mio_logbfmt(mio, MIO_LOG_UNTYPED | MIO_LOG_DEBUG, "  %s\n", btsyms[i]);
		}
		free (btsyms);
	}
}
#else
static void backtrace_stack_frames (mio_t* mio)
{
	/* do nothing. not supported */
}
#endif /* defined(MIO_ENABLE_LIBUNWIND) */

void mio_sys_assertfail (mio_t* mio, const mio_bch_t* expr, const mio_bch_t* file, mio_oow_t line)
{
	mio_logbfmt (mio, MIO_LOG_UNTYPED | MIO_LOG_FATAL, "ASSERTION FAILURE: %s at %s:%zu\n", expr, file, line);
	backtrace_stack_frames (mio);

#if defined(_WIN32)
	ExitProcess (249);
#elif defined(__OS2__)
	DosExit (EXIT_PROCESS, 249);
#elif defined(__DOS__)
	{
		union REGS regs;
		regs.h.ah = DOS_EXIT;
		regs.h.al = 249;
		intdos (&regs, &regs);
	}
#elif defined(vms) || defined(__vms)
	lib$stop (SS$_ABORT); /* use SS$_OPCCUS instead? */
	/* this won't be reached since lib$stop() terminates the process */
	sys$exit (SS$_ABORT); /* this condition code can be shown with
	                       * 'show symbol $status' from the command-line. */
#elif defined(macintosh)

	ExitToShell ();

#else

	kill (getpid(), SIGABRT);
	_exit (1);
#endif
}

#endif /* defined(MIO_BUILD_RELEASE) */
