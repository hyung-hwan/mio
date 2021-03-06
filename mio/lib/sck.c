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


#include <mio-sck.h>
#include "mio-prv.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h> /* strerror */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#if defined(HAVE_NETPACKET_PACKET_H)
#	include <netpacket/packet.h>
#endif

#if defined(HAVE_NET_IF_DL_H)
#	include <net/if_dl.h>
#endif

#if defined(HAVE_SYS_SENDFILE_H)
#	include <sys/sendfile.h>
#endif

#if defined(HAVE_SYS_IOCTL_H)
#	include <sys/ioctl.h>
#endif

#if defined(HAVE_NET_BPF_H)
#	include <net/bpf.h>
#endif

#if defined(__linux__)
#	include <limits.h>
#	if defined(HAVE_LINUX_NETFILTER_IPV4_H)
#		include <linux/netfilter_ipv4.h> /* SO_ORIGINAL_DST */
#	endif
#	if !defined(SO_ORIGINAL_DST)
#		define SO_ORIGINAL_DST 80
#	endif
#	if !defined(IP_TRANSPARENT)
#		define IP_TRANSPARENT 19
#	endif
#	if !defined(SO_REUSEPORT)
#		define SO_REUSEPORT 15
#	endif
#endif

#if defined(HAVE_OPENSSL_SSL_H) && defined(HAVE_SSL)
#	include <openssl/ssl.h>
#	if defined(HAVE_OPENSSL_ERR_H)
#		include <openssl/err.h>
#	endif
#	if defined(HAVE_OPENSSL_ENGINE_H)
#		include <openssl/engine.h>
#	endif
#	define USE_SSL
#endif

/* ========================================================================= */

static mio_syshnd_t open_async_socket (mio_t* mio, int domain, int type, int proto)
{
	mio_syshnd_t sck = MIO_SYSHND_INVALID;
	int flags;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
open_socket:
#endif
	sck = socket(domain, type, proto); 
	if (sck == MIO_SYSHND_INVALID) 
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL && (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)))
		{
			type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
			goto open_socket;
		}
	#endif
		goto oops;
	}
	else
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) goto done;
	#endif
	}

	if (mio_makesyshndasync(mio, sck) <= -1 ||
	    mio_makesyshndcloexec(mio, sck) <= -1) goto oops;

done:
	return sck;

oops:
	mio_seterrwithsyserr (mio, 0, errno);
	if (sck != MIO_SYSHND_INVALID) close (sck);
	return MIO_SYSHND_INVALID;
}

static mio_syshnd_t open_async_qx (mio_t* mio, mio_syshnd_t* side_chan)
{
	int fd[2];
	int type = SOCK_DGRAM;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
open_socket:
#endif
	if (socketpair(AF_UNIX, type, 0, fd) <= -1)
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL && (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)))
		{
			type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
			goto open_socket;
		}
	#endif
		mio_seterrwithsyserr (mio, 0, errno);
		return MIO_SYSHND_INVALID;
	}
	else
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) goto done;
	#endif
	}

	if (mio_makesyshndasync(mio, fd[0]) <= -1 ||
	    mio_makesyshndasync(mio, fd[1]) <= -1 ||
	    mio_makesyshndcloexec(mio, fd[0]) <= -1 ||
	    mio_makesyshndcloexec(mio, fd[1]) <= -1) 
	{
		mio_seterrwithsyserr (mio, 0, errno);
		close (fd[0]);
		close (fd[1]);
		return MIO_SYSHND_INVALID;
	}

done:
	*side_chan = fd[1]; /* write end of the pipe */
	return fd[0]; /* read end of the pipe */
}

static mio_syshnd_t open_async_bpf (mio_t* mio)
{
	mio_syshnd_t fd = MIO_SYSHND_INVALID;
	int tmp;
	unsigned int bufsize;

	fd = open("/dev/bpf", O_RDWR);
	if (fd == MIO_SYSHND_INVALID) goto oops;

#if 0
	if (ioctl(fd, BIOCIMMEDIATE, &tmp) == -1) goto oops;
	if (ioctl(fd, BIOCGBLEN, &bufsize) == -1) goto oops;
#endif

	return fd;
oops:
	mio_seterrwithsyserr (mio, 0, errno);
	if (fd != MIO_SYSHND_INVALID) close (fd);
	return MIO_SYSHND_INVALID;
}

/* ========================================================================= */

static mio_devaddr_t* skad_to_devaddr (mio_dev_sck_t* dev, const mio_skad_t* sckaddr, mio_devaddr_t* devaddr)
{
	if (sckaddr)
	{
		devaddr->ptr = (void*)sckaddr;
		devaddr->len = mio_skad_size(sckaddr);
		return devaddr;
	}

	return MIO_NULL;
}

static MIO_INLINE mio_skad_t* devaddr_to_skad (mio_dev_sck_t* dev, const mio_devaddr_t* devaddr, mio_skad_t* sckaddr)
{
	return (mio_skad_t*)devaddr->ptr;
}

/* ========================================================================= */

#define IS_STATEFUL(sck) ((sck)->dev_cap & MIO_DEV_CAP_STREAM)

struct sck_type_map_t
{
	int domain;
	int type;
	int proto;
	int extra_dev_cap;
};

#define __AF_QX 999999
#define __AF_BPF 999998

static struct sck_type_map_t sck_type_map[] =
{
	/* MIO_DEV_SCK_QX */
	{ __AF_QX,    0,              0,                         0 },

	/* MIO_DEV_SCK_TCP4 */
	{ AF_INET,    SOCK_STREAM,    0,                         MIO_DEV_CAP_STREAM },

	/* MIO_DEV_SCK_TCP6 */
	{ AF_INET6,   SOCK_STREAM,    0,                         MIO_DEV_CAP_STREAM },

	/* MIO_DEV_SCK_UPD4 */
	{ AF_INET,    SOCK_DGRAM,     0,                         0                                             },

	/* MIO_DEV_SCK_UDP6 */
	{ AF_INET6,   SOCK_DGRAM,     0,                         0                                             },

	/* MIO_DEV_SCK_ICMP4 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET,    SOCK_RAW,       IPPROTO_ICMP,              0,                                             },

	/* MIO_DEV_SCK_ICMP6 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET6,   SOCK_RAW,       IPPROTO_ICMP,              0,                                             },

#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* MIO_DEV_SCK_ARP - Ethernet type is 2 bytes long. Protocol must be specified in the network byte order */
	{ AF_PACKET,  SOCK_RAW,       MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },

	/* MIO_DEV_SCK_ARP_DGRAM - link-level header removed*/
	{ AF_PACKET,  SOCK_DGRAM,     MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },

#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	/* MIO_DEV_SCK_ARP */
	{ AF_LINK,  SOCK_RAW,         MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },

	/* MIO_DEV_SCK_ARP_DGRAM */
	{ AF_LINK,  SOCK_DGRAM,       MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },
#else
	{ -1,       0,                0,                         0                                              },
	{ -1,       0,                0,                         0                                              },
#endif

#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* MIO_DEV_SCK_PACKET */
	{ AF_PACKET,  SOCK_RAW,       MIO_CONST_HTON16(ETH_P_ALL), 0                                            },
#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	/* MIO_DEV_SCK_PACKET */
	{ AF_LINK,    SOCK_RAW,       MIO_CONST_HTON16(0),       0                                              },
#else
	{ -1,       0,                0,                         0                                              },
#endif


	/* MIO_DEV_SCK_BPF - arp */
	{ __AF_BPF, 0, 0, 0 } /* not implemented yet */
};

/* ======================================================================== */

static void connect_timedout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	MIO_ASSERT (mio, IS_STATEFUL(rdev));

	if (rdev->state & MIO_DEV_SCK_CONNECTING)
	{
		/* the state check for MIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job 
		 * doesn't need to be deleted when it gets connected for this check 
		 * here. this libarary, however, deletes the job when it gets 
		 * connected. */
		MIO_DEBUG1 (mio, "SCK(%p) - connect timed out. halting\n", rdev);
		mio_dev_sck_halt (rdev);
	}
}

static void ssl_accept_timedout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	MIO_ASSERT (mio, IS_STATEFUL(rdev));

	if (rdev->state & MIO_DEV_SCK_ACCEPTING_SSL)
	{
		MIO_DEBUG1 (mio, "SCK(%p) - ssl-accept timed out. halting\n", rdev);
		mio_dev_sck_halt(rdev);
	}
}

static void ssl_connect_timedout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	MIO_ASSERT (mio, IS_STATEFUL(rdev));

	if (rdev->state & MIO_DEV_SCK_CONNECTING_SSL)
	{
		MIO_DEBUG1 (mio, "SCK(%p) - ssl-connect timed out. halting\n", rdev);
		mio_dev_sck_halt(rdev);
	}
}

static MIO_INLINE int schedule_timer_job_at (mio_dev_sck_t* dev, const mio_ntime_t* fire_at, mio_tmrjob_handler_t handler)
{
#if 1
	return mio_schedtmrjobat(dev->mio, fire_at, handler, &dev->tmrjob_index, dev);
#else
	mio_tmrjob_t tmrjob;

	MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = &dev->tmrjob_index;

	MIO_ASSERT (dev->mio, dev->tmrjob_index == MIO_TMRIDX_INVALID);
	dev->tmrjob_index = mio_instmrjob(dev->mio, &tmrjob);
	return dev->tmrjob_index == MIO_TMRIDX_INVALID? -1: 0;
#endif
}

static MIO_INLINE int schedule_timer_job_after (mio_dev_sck_t* dev, const mio_ntime_t* fire_after, mio_tmrjob_handler_t handler)
{
#if 1
	return mio_schedtmrjobafter(dev->mio, fire_after, handler, &dev->tmrjob_index, dev);
#else
	mio_t* mio = dev->mio;
	mio_ntime_t fire_at;

	MIO_ASSERT (mio, MIO_IS_POS_NTIME(fire_after));

	mio_gettime (mio, &fire_at);
	MIO_ADD_NTIME (&fire_at, &fire_at, fire_after);

	return schedule_timer_job_at(dev, &fire_at, handler);
#endif
}

/* ======================================================================== */
#if defined(USE_SSL)
static void set_ssl_error (mio_t* mio, int sslerr)
{
	mio_bch_t emsg[128];
	ERR_error_string_n (sslerr, emsg, MIO_COUNTOF(emsg));
	mio_seterrbfmt (mio, MIO_ESYSERR, "%hs", emsg);
}
#endif

static int dev_sck_make (mio_dev_t* dev, void* ctx)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_dev_sck_make_t* arg = (mio_dev_sck_make_t*)ctx;
	mio_syshnd_t hnd = MIO_SYSHND_INVALID;
	mio_syshnd_t side_chan = MIO_SYSHND_INVALID;

	MIO_ASSERT (mio, arg->type >= 0 && arg->type < MIO_COUNTOF(sck_type_map));

	/* initialize some fields first where 0 is not somthing initial or invalid. */
	rdev->hnd = MIO_SYSHND_INVALID;
	rdev->side_chan = MIO_SYSHND_INVALID;
	rdev->tmrjob_index = MIO_TMRIDX_INVALID;

	if (sck_type_map[arg->type].domain <= -1)
	{
		mio_seterrnum (mio, MIO_ENOIMPL); /* TODO: better error info? */
		goto oops;
	}

	if (MIO_UNLIKELY(sck_type_map[arg->type].domain == __AF_QX))
	{
		hnd = open_async_qx(mio, &side_chan);
		if (hnd == MIO_SYSHND_INVALID) goto oops;
	}
	else
	{
		hnd = open_async_socket(mio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
		if (hnd == MIO_SYSHND_INVALID) goto oops;
	}

	rdev->hnd = hnd;
	rdev->side_chan = side_chan;
	rdev->dev_cap = MIO_DEV_CAP_IN | MIO_DEV_CAP_OUT | sck_type_map[arg->type].extra_dev_cap;
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	rdev->on_connect = arg->on_connect;
	rdev->on_disconnect = arg->on_disconnect;
	rdev->on_raw_accept = arg->on_raw_accept;
	rdev->type = arg->type;

	if (arg->options & MIO_DEV_SCK_MAKE_LENIENT) rdev->state |= MIO_DEV_SCK_LENIENT;

	return 0;

oops:
	if (hnd != MIO_SYSHND_INVALID) 
	{
		close (hnd);
	}
	if (side_chan != MIO_SYSHND_INVALID) 
	{
		close (side_chan);
	}
	return -1;
}

static int dev_sck_make_client (mio_dev_t* dev, void* ctx)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_syshnd_t* clisckhnd = (mio_syshnd_t*)ctx;

	/* create a socket device that is made of a socket connection
	 * on a listening socket.
	 * nothing special is done here except setting the socket handle.
	 * most of the initialization is done by the listening socket device
	 * after a client socket has been created. */

	rdev->hnd = *clisckhnd;
	rdev->tmrjob_index = MIO_TMRIDX_INVALID;
	rdev->side_chan = MIO_SYSHND_INVALID;

	if (mio_makesyshndasync(mio, rdev->hnd) <= -1 ||
	    mio_makesyshndcloexec(mio, rdev->hnd) <= -1) goto oops;

	return 0;

oops:
	if (rdev->hnd != MIO_SYSHND_INVALID)
	{
		close (rdev->hnd);
		rdev->hnd = MIO_SYSHND_INVALID;
	}
	return -1;
}

static void dev_sck_fail_before_make_client (void* ctx)
{
	mio_syshnd_t* clisckhnd = (mio_syshnd_t*)ctx;
	close (*clisckhnd);
}

static int dev_sck_kill (mio_dev_t* dev, int force)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (IS_STATEFUL(rdev))
	{
		/*if (MIO_DEV_SCK_GET_PROGRESS(rdev))
		{*/
			/* for MIO_DEV_SCK_CONNECTING, MIO_DEV_SCK_CONNECTING_SSL, and MIO_DEV_SCK_ACCEPTING_SSL
			 * on_disconnect() is called without corresponding on_connect(). 
			 * it is the same if connect or accept has not been called. */
			if (rdev->on_disconnect) rdev->on_disconnect (rdev);
		/*}*/

		if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
		{
			mio_deltmrjob (mio, rdev->tmrjob_index);
			MIO_ASSERT (mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);
		}
	}
	else
	{
		MIO_ASSERT (mio, (rdev->state & MIO_DEV_SCK_ALL_PROGRESS_BITS) == 0);
		MIO_ASSERT (mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);

		if (rdev->on_disconnect) rdev->on_disconnect (rdev);
	}

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		SSL_shutdown ((SSL*)rdev->ssl); /* is this needed? */
		SSL_free ((SSL*)rdev->ssl);
		rdev->ssl = MIO_NULL;
	}
	if (!(rdev->state & (MIO_DEV_SCK_ACCEPTED | MIO_DEV_SCK_ACCEPTING_SSL)) && rdev->ssl_ctx)
	{
		SSL_CTX_free ((SSL_CTX*)rdev->ssl_ctx);
		rdev->ssl_ctx = MIO_NULL;
	}
#endif

	if (rdev->hnd != MIO_SYSHND_INVALID) 
	{
		close (rdev->hnd);
		rdev->hnd = MIO_SYSHND_INVALID;
	}

	if (rdev->side_chan != MIO_SYSHND_INVALID)
	{
		close (rdev->side_chan);
		rdev->side_chan = MIO_SYSHND_INVALID;
	}
	return 0;
}

static mio_syshnd_t dev_sck_getsyshnd (mio_dev_t* dev)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return (mio_syshnd_t)rdev->hnd;
}
/* ------------------------------------------------------------------------------ */

static int dev_sck_read_stateful (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		x = SSL_read((SSL*)rdev->ssl, buf, *len);
		if (x <= -1)
		{
			int err = SSL_get_error((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			set_ssl_error (mio, err);
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;

		x = recv(rdev->hnd, buf, *len, 0);
		if (x == -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
			if (errno == EINTR) return 0;
			mio_seterrwithsyserr (mio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

static int dev_sck_read_stateless (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_scklen_t srcaddrlen;
	ssize_t x;

	srcaddrlen = MIO_SIZEOF(rdev->remoteaddr);
	x = recvfrom(rdev->hnd, buf, *len, 0, (struct sockaddr*)&rdev->remoteaddr, &srcaddrlen);
	if (x <= -1)
	{
		int eno = errno;
		if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;  /* no data available */
		if (eno == EINTR) return 0;

		mio_seterrwithsyserr (mio, 0, eno);

		MIO_DEBUG2 (mio, "SCK(%p) - recvfrom failure - %hs", rdev, strerror(eno)); 
		return -1;
	}

	srcaddr->ptr = &rdev->remoteaddr;
	srcaddr->len = srcaddrlen;

	*len = x;
	return 1;
}

static int dev_sck_read_bpf (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_seterrwithsyserr (mio, 0, MIO_ENOIMPL);
	return -1;
}

/* ------------------------------------------------------------------------------ */

static int dev_sck_write_stateful (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		if (*len <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) == -1)
			{
				set_ssl_error (mio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		x = SSL_write((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			set_ssl_error (mio, err);
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;
		int flags = 0;

		if (*len <= 0)
		{
			/* the write handler for a stream device must handle a zero-length 
			 * writing request specially. it's a writing finish indicator. close
			 * the writing end of the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) == -1)
			{
				mio_seterrwithsyserr (mio, 0, errno);
				return -1;
			}

			/* it must return a non-zero positive value. if it returns 0, this request 
			 * gets enqueued by the core. we must aovid it */
			return 1;
		}

		/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
	#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
	#endif
		x = send(rdev->hnd, data, *len, flags);
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			mio_seterrwithsyserr (mio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}


static int dev_sck_writev_stateful (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;
		mio_iolen_t i, nwritten;

		if (*iovcnt <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) == -1)
			{
				set_ssl_error (mio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		nwritten = 0;
		for (i = 0; i < *iovcnt; i++)
		{
			/* no SSL_writev. invoke multiple calls to SSL_write(). 
			 * since the write function is for the stateful connection,
			 * mutiple calls shouldn't really matter */
			x = SSL_write((SSL*)rdev->ssl, iov[i].iov_ptr, iov[i].iov_len);
			if (x <= -1)
			{
				int err = SSL_get_error ((SSL*)rdev->ssl, x);
				if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
				set_ssl_error (mio, err);
				return -1;
			}
			nwritten += x;
		}

		*iovcnt = nwritten;
	}
	else
	{
#endif
		ssize_t x;
		int flags = 0;
		struct msghdr msg;

		if (*iovcnt <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) == -1)
			{
				mio_seterrwithsyserr (mio, 0, errno);
				return -1;
			}

			return 1;
		}

		/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
	#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
	#endif
	#if defined(MSG_DONTWAIT)
		flags |= MSG_DONTWAIT;
	#endif

	#if defined(HAVE_SENDMSG)
		MIO_MEMSET (&msg, 0, MIO_SIZEOF(msg));
		msg.msg_iov = (struct iovec*)iov;
		msg.msg_iovlen = *iovcnt;
		x = sendmsg(rdev->hnd, &msg, flags);
	#else
		x = writev(rdev->hnd, (const struct iovec*)iov, *iovcnt);
	#endif
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			mio_seterrwithsyserr (mio, 0, errno);
			return -1;
		}

		*iovcnt = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

/* ------------------------------------------------------------------------------ */

static int dev_sck_write_stateless (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	ssize_t x;

	x = sendto(rdev->hnd, data, *len, 0, dstaddr->ptr, dstaddr->len);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_sck_writev_stateless (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	struct msghdr msg;
	ssize_t x;
	int flags = 0;

	MIO_MEMSET (&msg, 0, MIO_SIZEOF(msg));
	if (MIO_LIKELY(dstaddr))
	{
		msg.msg_name = dstaddr->ptr;
		msg.msg_namelen = dstaddr->len;
	}
	msg.msg_iov = (struct iovec*)iov;
	msg.msg_iovlen = *iovcnt;


#if defined(MSG_NOSIGNAL)
	flags |= MSG_NOSIGNAL;
#endif
#if defined(MSG_DONTWAIT)
	flags |= MSG_DONTWAIT;
#endif

	x = sendmsg(rdev->hnd, &msg, flags);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

/* ------------------------------------------------------------------------------ */
static int dev_sck_write_bpf (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_seterrwithsyserr (mio, 0, MIO_ENOIMPL);
	return -1;
}

static int dev_sck_writev_bpf (mio_dev_t* dev, const mio_iovec_t* iov, mio_iolen_t* iovcnt, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_seterrwithsyserr (mio, 0, MIO_ENOIMPL);
	return -1;
}


/* ------------------------------------------------------------------------------ */

static int dev_sck_sendfile_stateful (mio_dev_t* dev, mio_syshnd_t in_fd, mio_foff_t foff, mio_iolen_t* len)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

#if 0 && defined(USE_SSL)
/* TODO: ssl needs to read from the file... and send... */
	if (rdev->ssl)
	{
		int x;

		if (*len <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) == -1)
			{
				set_ssl_error (mio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		x = SSL_write((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			set_ssl_error (mio, err);
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;

		if (*len <= 0)
		{
			/* the write handler for a stream device must handle a zero-length 
			 * writing request specially. it's a writing finish indicator. close
			 * the writing end of the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) == -1)
			{
				mio_seterrwithsyserr (mio, 0, errno);
				return -1;
			}

			/* it must return a non-zero positive value. if it returns 0, this request 
			 * gets enqueued by the core. we must aovid it */
			return 1;
		}

#if defined(HAVE_SENDFILE)
/* TODO: cater for other systems */
		x = sendfile(rdev->hnd, in_fd, &foff, *len);
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			mio_seterrwithsyserr (mio, 0, errno);
			return -1;
		}
		*len = x;
#else
		mio_seterrnum (mio, MIO_ENOIMPL);
		return -1;
#endif

	
#if 0 && defined(USE_SSL)
	}
#endif
	return 1;
}

/* ------------------------------------------------------------------------------ */

#if defined(USE_SSL)

static int do_ssl (mio_dev_sck_t* dev, int (*ssl_func)(SSL*))
{
	mio_t* mio = dev->mio;
	int ret, watcher_cmd, watcher_events;

	MIO_ASSERT (mio, dev->ssl_ctx);

	if (!dev->ssl)
	{
		SSL* ssl;

		ssl = SSL_new(dev->ssl_ctx);
		if (!ssl)
		{
			set_ssl_error (mio, ERR_get_error());
			return -1;
		}

		if (SSL_set_fd(ssl, dev->hnd) == 0)
		{
			set_ssl_error (mio, ERR_get_error());
			return -1;
		}

		SSL_set_read_ahead (ssl, 0);

		dev->ssl = ssl;
	}

	watcher_cmd = MIO_DEV_WATCH_RENEW;
	watcher_events = MIO_DEV_EVENT_IN;

	ret = ssl_func((SSL*)dev->ssl);
	if (ret <= 0)
	{
		int err = SSL_get_error(dev->ssl, ret);
		if (err == SSL_ERROR_WANT_READ)
		{
			/* handshaking isn't complete */
			ret = 0;
		}
		else if (err == SSL_ERROR_WANT_WRITE)
		{
			/* handshaking isn't complete */
			watcher_cmd = MIO_DEV_WATCH_UPDATE;
			watcher_events = MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT;
			ret = 0;
		}
		else
		{
			set_ssl_error (mio, err);
			ret = -1;
		}
	}
	else
	{
		ret = 1; /* accepted */
	}

	if (mio_dev_watch((mio_dev_t*)dev, watcher_cmd, watcher_events) <= -1)
	{
		mio_stop (mio, MIO_STOPREQ_WATCHER_ERROR);
		ret = -1;
	}

	return ret;
}

static MIO_INLINE int connect_ssl (mio_dev_sck_t* dev)
{
	return do_ssl(dev, SSL_connect);
}

static MIO_INLINE int accept_ssl (mio_dev_sck_t* dev)
{
	return do_ssl(dev, SSL_accept);
}
#endif

static int dev_sck_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	switch (cmd)
	{
		case MIO_DEV_SCK_BIND:
		{
			mio_dev_sck_bind_t* bnd = (mio_dev_sck_bind_t*)arg;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = MIO_NULL;
		#endif
			if (MIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't bind again */
				mio_seterrbfmt (mio, MIO_EPERM, "operation in progress. not allowed to bind again");
				return -1;
			}

			if (mio_skad_family(&bnd->localaddr) == MIO_AF_INET6) /* getsockopt(rdev->hnd, SO_DOMAIN, ...) may return the domain but it's kernel specific as well */
			{
				/* TODO: should i make it into bnd->options? MIO_DEV_SCK_BIND_IPV6ONLY? applicable to ipv6 though. */
				int v = 1;
				if (setsockopt(rdev->hnd, IPPROTO_IPV6, IPV6_V6ONLY, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrbfmtwithsyserr (mio, 0, errno, "unable to set IPV6_V6ONLY");
					return -1;
				}
			}

			if (bnd->options & MIO_DEV_SCK_BIND_BROADCAST)
			{
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_BROADCAST, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrbfmtwithsyserr (mio, 0, errno, "unable to set SO_BROADCAST");
					return -1;
				}
			}

			if (bnd->options & MIO_DEV_SCK_BIND_REUSEADDR)
			{
			#if defined(SO_REUSEADDR)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_REUSEADDR, &v, MIO_SIZEOF(v)) == -1)
				{
					if (!(bnd->options & MIO_DEV_SCK_BIND_IGNERR))
					{
						mio_seterrbfmtwithsyserr (mio, 0, errno, "unable to set SO_REUSEADDR");
						return -1;
					}
				}
			/* ignore it if not available
			#else
				mio_seterrnum (mio, MIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (bnd->options & MIO_DEV_SCK_BIND_REUSEPORT)
			{
			#if defined(SO_REUSEPORT)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_REUSEPORT, &v, MIO_SIZEOF(v)) == -1)
				{
					if (!(bnd->options & MIO_DEV_SCK_BIND_IGNERR))
					{
						mio_seterrbfmtwithsyserr (mio, 0, errno, "unable to set SO_REUSEPORT");
						return -1;
					}
				}
			/* ignore it if not available
			#else
				mio_seterrnum (mio, MIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (bnd->options & MIO_DEV_SCK_BIND_TRANSPARENT)
			{
			#if defined(IP_TRANSPARENT)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_IP, IP_TRANSPARENT, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrbfmtwithsyserr (mio, 0, errno, "unable to set IP_TRANSPARENT");
					return -1;
				}
			/* ignore it if not available
			#else
				mio_seterrnum (mio, MIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (rdev->ssl_ctx)
			{
			#if defined(USE_SSL)
				SSL_CTX_free (rdev->ssl_ctx);
			#endif
				rdev->ssl_ctx = MIO_NULL;

				if (rdev->ssl)
				{
				#if defined(USE_SSL)
					SSL_free (rdev->ssl);
				#endif
					rdev->ssl = MIO_NULL;
				}
			}

			if (bnd->options & MIO_DEV_SCK_BIND_SSL)
			{
			#if defined(USE_SSL)
				if (!bnd->ssl_certfile || !bnd->ssl_keyfile)
				{
					mio_seterrbfmt (mio, MIO_EINVAL, "SSL certficate/key file not set");
					return -1;
				}

				ssl_ctx = SSL_CTX_new(SSLv23_server_method());
				if (!ssl_ctx)
				{
					set_ssl_error (mio, ERR_get_error());
					return -1;
				}

				if (SSL_CTX_use_certificate_file(ssl_ctx, bnd->ssl_certfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_use_PrivateKey_file(ssl_ctx, bnd->ssl_keyfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_check_private_key(ssl_ctx) == 0  /*||
				    SSL_CTX_use_certificate_chain_file(ssl_ctx, bnd->chainfile) == 0*/)
				{
					set_ssl_error (mio, ERR_get_error());
					SSL_CTX_free (ssl_ctx);
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /*SSL_MODE_ENABLE_PARTIAL_WRITE |*/
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

				SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2); /* no outdated SSLv2 by default */
			#else
				mio_seterrnum (mio, MIO_ENOIMPL);
				return -1;
			#endif
			}

			x = bind(rdev->hnd, (struct sockaddr*)&bnd->localaddr, mio_skad_size(&bnd->localaddr));
			if (x == -1)
			{
				mio_seterrwithsyserr (mio, 0, errno);
			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free (ssl_ctx);
			#endif
				return -1;
			}

			rdev->localaddr = bnd->localaddr;

		#if defined(USE_SSL)
			rdev->ssl_ctx = ssl_ctx;
		#endif

			return 0;
		}

		case MIO_DEV_SCK_CONNECT:
		{
			mio_dev_sck_connect_t* conn = (mio_dev_sck_connect_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&conn->remoteaddr;
			mio_scklen_t sl;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = MIO_NULL;
		#endif

			if (MIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't connect again */
				mio_seterrbfmt (mio, MIO_EPERM, "operation in progress. disallowed to connect again");
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				mio_seterrbfmt (mio, MIO_EPERM, "disallowed to connect stateless device");
				return -1;
			}

			if (sa->sa_family == AF_INET) sl = MIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = MIO_SIZEOF(struct sockaddr_in6);
			else 
			{
				mio_seterrbfmt (mio, MIO_EINVAL, "unknown address family %d", sa->sa_family);
				return -1;
			}

		#if defined(USE_SSL)
			if (rdev->ssl_ctx)
			{
				if (rdev->ssl)
				{
					SSL_free (rdev->ssl);
					rdev->ssl = MIO_NULL;
				}

				SSL_CTX_free (rdev->ssl_ctx);
				rdev->ssl_ctx = MIO_NULL;
			}

			if (conn->options & MIO_DEV_SCK_CONNECT_SSL)
			{
				ssl_ctx = SSL_CTX_new(SSLv23_client_method());
				if (!ssl_ctx)
				{
					set_ssl_error (mio, ERR_get_error());
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /* SSL_MODE_ENABLE_PARTIAL_WRITE | */
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
			}
		#endif
			/* the socket is already non-blocking */
/*{
int flags = fcntl (rdev->hnd, F_GETFL);
fcntl (rdev->hnd, F_SETFL, flags & ~O_NONBLOCK);
}*/
			x = connect(rdev->hnd, sa, sl);
/*{
int flags = fcntl (rdev->hnd, F_GETFL);
fcntl (rdev->hnd, F_SETFL, flags | O_NONBLOCK);
}*/
			if (x <= -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
				{
					if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_UPDATE, MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT) <= -1)
					{
						/* watcher update failure. it's critical */
						mio_stop (mio, MIO_STOPREQ_WATCHER_ERROR);
						goto oops_connect;
					}
					else
					{
						MIO_INIT_NTIME (&rdev->tmout, 0, 0); /* just in case */

						if (MIO_IS_POS_NTIME(&conn->connect_tmout))
						{
							if (schedule_timer_job_after(rdev, &conn->connect_tmout, connect_timedout) <= -1) 
							{
								goto oops_connect;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								MIO_ASSERT (mio, rdev->tmrjob_index != MIO_TMRIDX_INVALID);
								mio_gettmrjobdeadline (mio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						rdev->remoteaddr = conn->remoteaddr;
					#if defined(USE_SSL)
						rdev->ssl_ctx = ssl_ctx;
					#endif
						MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTING);
						return 0;
					}
				}

				mio_seterrwithsyserr (mio, 0, errno);

			oops_connect:
				if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_UPDATE, MIO_DEV_EVENT_IN) <= -1)
				{
					/* watcher update failure. it's critical */
					mio_stop (mio, MIO_STOPREQ_WATCHER_ERROR);
				}

			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free (ssl_ctx);
			#endif
				return -1;
			}
			else
			{
				/* connected immediately */

				/* don't call on_connect() callback even though the connection has been established.
				 * i don't want on_connect() to be called within the this function. */
				if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_UPDATE, MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT) <= -1)
				{
					/* watcher update failure. it's critical */
					mio_stop (mio, MIO_STOPREQ_WATCHER_ERROR);
					goto oops_connect;
				}

				/* as i know it's connected already,
				 * i don't schedule a connection timeout job */

				rdev->remoteaddr = conn->remoteaddr;
			#if defined(USE_SSL)
				rdev->ssl_ctx = ssl_ctx;
			#endif
				/* set progress CONNECTING so that the ready handler invokes on_connect() */
				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTING);
				return 0;
			}
		}

		case MIO_DEV_SCK_LISTEN:
		{
			mio_dev_sck_listen_t* lstn = (mio_dev_sck_listen_t*)arg;
			int x;

			if (MIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't listen again */
				mio_seterrbfmt (mio, MIO_EPERM, "operation in progress. disallowed to listen again");
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				mio_seterrbfmt (mio, MIO_EPERM, "disallowed to listen on stateless device");
				return -1;
			}

			x = listen(rdev->hnd, lstn->backlogs);
			if (x == -1) 
			{
				mio_seterrwithsyserr (mio, 0, errno);
				return -1;
			}

			rdev->tmout = lstn->accept_tmout;

			MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_LISTENING);
			return 0;
		}
	}

	return 0;
}

static mio_dev_mth_t dev_mth_sck_stateless = 
{
	dev_sck_make,
	dev_sck_kill,
	MIO_NULL,
	dev_sck_getsyshnd,

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_writev_stateless,
	MIO_NULL,          /* sendfile */
	dev_sck_ioctl,     /* ioctl */
};


static mio_dev_mth_t dev_mth_sck_stateful = 
{
	dev_sck_make,
	dev_sck_kill,
	MIO_NULL,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_writev_stateful,
	dev_sck_sendfile_stateful,
	dev_sck_ioctl,     /* ioctl */
};

static mio_dev_mth_t dev_mth_clisck =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_fail_before_make_client,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_writev_stateful,
	dev_sck_sendfile_stateful,
	dev_sck_ioctl
};

static mio_dev_mth_t dev_mth_sck_bpf = 
{
	dev_sck_make,
	dev_sck_kill,
	MIO_NULL,
	dev_sck_getsyshnd,

	dev_sck_read_bpf,
	dev_sck_write_bpf,
	dev_sck_writev_bpf,
	MIO_NULL,          /* sendfile */
	dev_sck_ioctl,     /* ioctl */
};

/* ========================================================================= */

static int harvest_outgoing_connection (mio_dev_sck_t* rdev)
{
	mio_t* mio = rdev->mio;
	int errcode;
	mio_scklen_t len;

	MIO_ASSERT (mio, !(rdev->state & MIO_DEV_SCK_CONNECTED));

	len = MIO_SIZEOF(errcode);
	if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
	{
		mio_seterrbfmtwithsyserr (mio, 0, errno, "unable to get SO_ERROR");
		return -1;
	}
	else if (errcode == 0)
	{
		mio_skad_t localaddr;
		mio_scklen_t addrlen;

		/* connected */

		if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
		{
			mio_deltmrjob (mio, rdev->tmrjob_index);
			MIO_ASSERT (mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);
		}

		addrlen = MIO_SIZEOF(localaddr);
		if (getsockname(rdev->hnd, (struct sockaddr*)&localaddr, &addrlen) == 0) rdev->localaddr = localaddr;

		if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN) <= -1) 
		{
			/* watcher update failure. it's critical */
			mio_stop (mio, MIO_STOPREQ_WATCHER_ERROR);
			return -1;
		}

	#if defined(USE_SSL)
		if (rdev->ssl_ctx)
		{
			int x;
			MIO_ASSERT (mio, !rdev->ssl); /* must not be SSL-connected yet */

			x = connect_ssl(rdev);
			if (x <= -1) return -1;
			if (x == 0)
			{
				/* underlying socket connected but not SSL-connected */
				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTING_SSL);

				MIO_ASSERT (mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);

				/* rdev->tmout has been set to the deadline of the connect task
				 * when the CONNECT IOCTL command has been executed. use the 
				 * same deadline here */
				if (MIO_IS_POS_NTIME(&rdev->tmout) &&
				    schedule_timer_job_at(rdev, &rdev->tmout, ssl_connect_timedout) <= -1)
				{
					MIO_DEBUG1 (mio, "SCK(%p) - ssl-connect timeout scheduling failed. halting\n", rdev);
					mio_dev_sck_halt (rdev);
				}

				return 0;
			}
			else
			{
				goto ssl_connected;
			}
		}
		else
		{
		ssl_connected:
	#endif
			MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTED);
			if (rdev->on_connect) rdev->on_connect (rdev);
	#if defined(USE_SSL)
		}
	#endif

		return 0;
	}
	else if (errcode == EINPROGRESS || errcode == EWOULDBLOCK)
	{
		/* still in progress */
		return 0;
	}
	else
	{
		mio_seterrwithsyserr (mio, 0, errcode);
		return -1;
	}
}

static int make_accepted_client_connection (mio_dev_sck_t* rdev, mio_syshnd_t clisck, mio_skad_t* remoteaddr, mio_dev_sck_type_t clisck_type)
{
	mio_t* mio = rdev->mio;
	mio_dev_sck_t* clidev;
	mio_scklen_t addrlen;

	if (rdev->on_raw_accept)
	{
		/* this is a special optional callback. If you don't want a client socket device 
		 * to be created upon accept, you may implement the on_raw_accept() handler. 
		 * the socket handle is delegated to the callback. */
		rdev->on_raw_accept (rdev, clisck, remoteaddr);
		return 0;
	}

	/* use rdev->dev_size when instantiating a client sck device
	 * instead of MIO_SIZEOF(mio_dev_sck_t). therefore, the  
	 * extension area as big as that of the master sck device
	 * is created in the client sck device */
	clidev = (mio_dev_sck_t*)mio_dev_make(mio, rdev->dev_size, &dev_mth_clisck, rdev->dev_evcb, &clisck); 
	if (MIO_UNLIKELY(!clidev))
	{
		/* [NOTE] 'clisck' is closed by callback methods called by mio_dev_make() upon failure */
		MIO_DEBUG3 (mio, "SCK(%p) - unable to make a new accepted device for %d - %js\n", rdev, (int)clisck, mio_geterrmsg(mio));
		return -1;
	}

	clidev->type = clisck_type;
	MIO_ASSERT (mio, clidev->hnd == clisck);

	clidev->dev_cap |= MIO_DEV_CAP_IN | MIO_DEV_CAP_OUT | MIO_DEV_CAP_STREAM;
	clidev->remoteaddr = *remoteaddr;

	addrlen = MIO_SIZEOF(clidev->localaddr);
	if (getsockname(clisck, (struct sockaddr*)&clidev->localaddr, &addrlen) == -1) clidev->localaddr = rdev->localaddr;

#if defined(SO_ORIGINAL_DST)
	/* if REDIRECT is used, SO_ORIGINAL_DST returns the original
	 * destination address. When REDIRECT is not used, it returnes
	 * the address of the local socket. In this case, it should
	 * be same as the result of getsockname(). */
	addrlen = MIO_SIZEOF(clidev->orgdstaddr);
	if (getsockopt(clisck, SOL_IP, SO_ORIGINAL_DST, &clidev->orgdstaddr, &addrlen) == -1) clidev->orgdstaddr = rdev->localaddr;
#else
	clidev->orgdstaddr = rdev->localaddr;
#endif

	if (!mio_equal_skads(&clidev->orgdstaddr, &clidev->localaddr, 0))
	{
		clidev->state |= MIO_DEV_SCK_INTERCEPTED;
	}
	else if (mio_skad_port(&clidev->localaddr) != mio_skad_port(&rdev->localaddr))
	{
		/* When TPROXY is used, getsockname() and SO_ORIGNAL_DST return
		 * the same addresses. however, the port number may be different
		 * as a typical TPROXY rule is set to change the port number.
		 * However, this check is fragile if the server port number is
		 * set to 0.
		 *
		 * Take note that the above assumption gets wrong if the TPROXY
		 * rule doesn't change the port number. so it won't be able
		 * to handle such a TPROXYed packet without port transformation. */
		clidev->state |= MIO_DEV_SCK_INTERCEPTED;
	}
	#if 0
	else if ((clidev->initial_ifindex = resolve_ifindex(fd, clidev->localaddr)) <= -1)
	{
		/* the local_address is not one of a local address.
		 * it's probably proxied. */
		clidev->state |= MIO_DEV_SCK_INTERCEPTED;
	}
	#endif

	/* inherit some event handlers from the parent.
	 * you can still change them inside the on_connect handler */
	clidev->on_connect = rdev->on_connect;
	clidev->on_disconnect = rdev->on_disconnect; 
	clidev->on_raw_accept = MIO_NULL; /* don't inherit this */
	clidev->on_write = rdev->on_write;
	clidev->on_read = rdev->on_read;

	/* inherit the contents of the extension area */
	MIO_ASSERT (mio, rdev->dev_size == clidev->dev_size);
	MIO_MEMCPY (mio_dev_sck_getxtn(clidev), mio_dev_sck_getxtn(rdev), rdev->dev_size - MIO_SIZEOF(mio_dev_sck_t));

	MIO_ASSERT (mio, clidev->tmrjob_index == MIO_TMRIDX_INVALID);

	if (rdev->ssl_ctx)
	{
		MIO_DEV_SCK_SET_PROGRESS (clidev, MIO_DEV_SCK_ACCEPTING_SSL);
		MIO_ASSERT (mio, clidev->state & MIO_DEV_SCK_ACCEPTING_SSL);
		/* actual SSL acceptance must be completed in the client device */

		/* let the client device know the SSL context to use */
		clidev->ssl_ctx = rdev->ssl_ctx;

		if (MIO_IS_POS_NTIME(&rdev->tmout) &&
		    schedule_timer_job_after(clidev, &rdev->tmout, ssl_accept_timedout) <= -1)
		{
			/* timer job scheduling failed. halt the device */
			MIO_DEBUG1 (mio, "SCK(%p) - ssl-accept timeout scheduling failed. halting\n", rdev);
			mio_dev_sck_halt (clidev);
		}
	}
	else
	{
		MIO_DEV_SCK_SET_PROGRESS (clidev, MIO_DEV_SCK_ACCEPTED);
		/*if (clidev->on_connect(clidev) <= -1) mio_dev_sck_halt (clidev);*/
		if (clidev->on_connect) clidev->on_connect (clidev);
	}

	return 0;
}

static int accept_incoming_connection (mio_dev_sck_t* rdev)
{
	mio_t* mio = rdev->mio;
	mio_syshnd_t clisck;
	mio_skad_t remoteaddr;
	mio_scklen_t addrlen;
	int flags;

	/* this is a server(lisening) socket */

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
	flags = SOCK_NONBLOCK | SOCK_CLOEXEC;

	addrlen = MIO_SIZEOF(remoteaddr);
	clisck = accept4(rdev->hnd, (struct sockaddr*)&remoteaddr, &addrlen, flags);
	if (clisck <= -1)
	{
		 if (errno != ENOSYS) goto accept_error;
		 /* go on for the normal 3-parameter accept */
	}
	else
	{
		 goto accept_done;
	}
#endif

	addrlen = MIO_SIZEOF(remoteaddr);
	clisck = accept(rdev->hnd, (struct sockaddr*)&remoteaddr, &addrlen);
	if (clisck <=  -1)
	{
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
	accept_error:
#endif
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
		if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

		mio_seterrwithsyserr (mio, 0, errno);
		return -1;
	}

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
accept_done:
#endif
	return make_accepted_client_connection(rdev, clisck, &remoteaddr, rdev->type);
}

static int dev_evcb_sck_ready_stateful (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (events & MIO_DEV_EVENT_ERR)
	{
		int errcode;
		mio_scklen_t len;

		len = MIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error MIO_EDEVERRR is used */
			mio_seterrbfmt (mio, MIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			mio_seterrwithsyserr (mio, 0, errcode);
		}
		return -1;
	}

	/* this socket can connect */
	switch (MIO_DEV_SCK_GET_PROGRESS(rdev))
	{
		case MIO_DEV_SCK_CONNECTING:
			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				mio_seterrnum (mio, MIO_EDEVHUP);
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN))
			{
				/* invalid event masks. generic device error */
				mio_seterrbfmt (mio, MIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & MIO_DEV_EVENT_OUT)
			{
				/* when connected, the socket becomes writable */
				return harvest_outgoing_connection(rdev);
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case MIO_DEV_SCK_CONNECTING_SSL:
		#if defined(USE_SSL)
			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				mio_seterrnum (mio, MIO_EDEVHUP);
				return -1;
			}
			else if (events & MIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				mio_seterrbfmt (mio, MIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT))
			{
				int x;

				x = connect_ssl(rdev);
				if (x <= -1) return -1;
				if (x == 0) return 0; /* not SSL-Connected */

				if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
				{
					mio_deltmrjob (rdev->mio, rdev->tmrjob_index);
					rdev->tmrjob_index = MIO_TMRIDX_INVALID;
				}

				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTED);
				if (rdev->on_connect) rdev->on_connect (rdev);
				return 0;
			}
			else
			{
				return 0; /* success. no actual I/O yet */
			}
		#else
			mio_seterrnum (mio, MIO_EINTERN);
			return -1;
		#endif

		case MIO_DEV_SCK_LISTENING:

			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				mio_seterrnum (mio, MIO_EDEVHUP);
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_OUT))
			{
				mio_seterrbfmt (mio, MIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & MIO_DEV_EVENT_IN)
			{
				if (rdev->state & MIO_DEV_SCK_LENIENT)
				{
					accept_incoming_connection(rdev);
					return 0; /* return ok to the core regardless of accept()'s result */
				}
				else
				{
					/* [NOTE] if the accept operation fails, the core also kills this listening device. */
					return accept_incoming_connection(rdev);
				}
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case MIO_DEV_SCK_ACCEPTING_SSL:
		#if defined(USE_SSL)
			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				mio_seterrnum (mio, MIO_EDEVHUP);
				return -1;
			}
			else if (events & MIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				mio_seterrbfmt (mio, MIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT))
			{
				int x;

				x = accept_ssl(rdev);
				if (x <= -1) return -1;
				if (x == 0) return 0; /* not SSL-accepted yet */

				if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
				{
					mio_deltmrjob (rdev->mio, rdev->tmrjob_index);
					rdev->tmrjob_index = MIO_TMRIDX_INVALID;
				}

				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_ACCEPTED);
				if (rdev->on_connect) rdev->on_connect (rdev);

				return 0;
			}
			else
			{
				return 0; /* no reading or writing yet */
			}
		#else
			mio_seterrnum (mio, MIO_EINTERN);
			return -1;
		#endif


		default:
			if (events & MIO_DEV_EVENT_HUP)
			{
				if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT)) 
				{
					/* probably half-open? */
					return 1;
				}

				mio_seterrnum (mio, MIO_EDEVHUP);
				return -1;
			}

			return 1; /* the device is ok. carry on reading or writing */
	}
}

static int dev_evcb_sck_ready_stateless (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (events & MIO_DEV_EVENT_ERR)
	{
		int errcode;
		mio_scklen_t len;

		len = MIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error MIO_EDEVERRR is used */
			mio_seterrbfmt (mio, MIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			mio_seterrwithsyserr (rdev->mio, 0, errcode);
		}
		return -1;
	}
	else if (events & MIO_DEV_EVENT_HUP)
	{
		mio_seterrnum (mio, MIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read_stateful (mio_dev_t* dev, const void* data, mio_iolen_t dlen, const mio_devaddr_t* srcaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_read(rdev, data, dlen, MIO_NULL);
}

static int dev_evcb_sck_on_write_stateful (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_write(rdev, wrlen, wrctx, MIO_NULL);
}

static int dev_evcb_sck_on_read_stateless (mio_dev_t* dev, const void* data, mio_iolen_t dlen, const mio_devaddr_t* srcaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_read(rdev, data, dlen, srcaddr->ptr);
}

static int dev_evcb_sck_on_write_stateless (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_write(rdev, wrlen, wrctx, dstaddr->ptr);
}

/* ========================================================================= */

static mio_dev_evcb_t dev_sck_event_callbacks_stateful =
{
	dev_evcb_sck_ready_stateful,
	dev_evcb_sck_on_read_stateful,
	dev_evcb_sck_on_write_stateful
};

static mio_dev_evcb_t dev_sck_event_callbacks_stateless =
{
	dev_evcb_sck_ready_stateless,
	dev_evcb_sck_on_read_stateless,
	dev_evcb_sck_on_write_stateless
};
/* ========================================================================= */

static int dev_evcb_sck_ready_qx (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (events & MIO_DEV_EVENT_ERR)
	{
		int errcode;
		mio_scklen_t len;

		len = MIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error MIO_EDEVERRR is used */
			mio_seterrbfmt (mio, MIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			mio_seterrwithsyserr (rdev->mio, 0, errcode);
		}
		return -1;
	}
	else if (events & MIO_DEV_EVENT_HUP)
	{
		mio_seterrnum (mio, MIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}


static int dev_evcb_sck_on_read_qx (mio_dev_t* dev, const void* data, mio_iolen_t dlen, const mio_devaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (rdev->type == MIO_DEV_SCK_QX)
	{
		mio_dev_sck_qxmsg_t* qxmsg;

		if (dlen != MIO_SIZEOF(*qxmsg))
		{
			mio_seterrbfmt (mio, MIO_EINVAL, "wrong qx packet size");
			return 0;
		}

		qxmsg = (mio_dev_sck_qxmsg_t*)data;
		if (qxmsg->cmd == MIO_DEV_SCK_QXMSG_NEWCONN)
		{
			if (make_accepted_client_connection(rdev, qxmsg->syshnd, &qxmsg->remoteaddr, qxmsg->scktype) <= -1)
			{
printf ("unable to accept new client connection %d\n", qxmsg->syshnd);
				return (rdev->state & MIO_DEV_SCK_LENIENT)? 0: -1;
			}
		}
		else
		{
			mio_seterrbfmt (mio, MIO_EINVAL, "wrong qx command code");
			return 0;
		}

		return 0;
	}


	/* this is not for a qx socket */
	return rdev->on_read(rdev, data, dlen, MIO_NULL);
}

static int dev_evcb_sck_on_write_qx (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (rdev->type == MIO_DEV_SCK_QX)
	{
		/* this should not be called */
		return 0;
	}
	
	return rdev->on_write(rdev, wrlen, wrctx, MIO_NULL);
}

static mio_dev_evcb_t dev_sck_event_callbacks_qx =
{
	dev_evcb_sck_ready_qx,
	dev_evcb_sck_on_read_qx,
	dev_evcb_sck_on_write_qx
};

/* ========================================================================= */

static int dev_evcb_sck_ready_bpf (mio_dev_t* dev, int events)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_seterrnum (mio, MIO_ENOIMPL);
	return -1;
}

static int dev_evcb_sck_on_read_bpf (mio_dev_t* dev, const void* data, mio_iolen_t dlen, const mio_devaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_seterrnum (mio, MIO_ENOIMPL);
	return -1;
}

static int dev_evcb_sck_on_write_bpf (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_seterrnum (mio, MIO_ENOIMPL);
	return -1;
}

static mio_dev_evcb_t dev_sck_event_callbacks_bpf =
{
	dev_evcb_sck_ready_bpf,
	dev_evcb_sck_on_read_bpf,
	dev_evcb_sck_on_write_bpf
};

/* ========================================================================= */

mio_dev_sck_t* mio_dev_sck_make (mio_t* mio, mio_oow_t xtnsize, const mio_dev_sck_make_t* info)
{
	mio_dev_sck_t* rdev;

	if (info->type < 0 && info->type >= MIO_COUNTOF(sck_type_map))
	{
		mio_seterrnum (mio, MIO_EINVAL);
		return MIO_NULL;
	}

	if (info->type == MIO_DEV_SCK_QX)
	{
		rdev = (mio_dev_sck_t*)mio_dev_make(
			mio, MIO_SIZEOF(mio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateless, &dev_sck_event_callbacks_qx, (void*)info);
	}
	else if (info->type == MIO_DEV_SCK_BPF)
	{
		rdev = (mio_dev_sck_t*)mio_dev_make(
			mio, MIO_SIZEOF(mio_dev_sck_t) + xtnsize,
			&dev_mth_sck_bpf, &dev_sck_event_callbacks_bpf, (void*)info);
	}
	else if (sck_type_map[info->type].extra_dev_cap & MIO_DEV_CAP_STREAM) /* can't use the IS_STATEFUL() macro yet */
	{
		rdev = (mio_dev_sck_t*)mio_dev_make(
			mio, MIO_SIZEOF(mio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateful, &dev_sck_event_callbacks_stateful, (void*)info);
	}
	else
	{
		rdev = (mio_dev_sck_t*)mio_dev_make(
			mio, MIO_SIZEOF(mio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateless, &dev_sck_event_callbacks_stateless, (void*)info);
	}

	return rdev;
}

int mio_dev_sck_bind (mio_dev_sck_t* dev, mio_dev_sck_bind_t* info)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_SCK_BIND, info);
}

int mio_dev_sck_connect (mio_dev_sck_t* dev, mio_dev_sck_connect_t* info)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_SCK_CONNECT, info);
}

int mio_dev_sck_listen (mio_dev_sck_t* dev, mio_dev_sck_listen_t* info)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_SCK_LISTEN, info);
}

int mio_dev_sck_write (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_devaddr_t devaddr;
	return mio_dev_write((mio_dev_t*)dev, data, dlen, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

int mio_dev_sck_writev (mio_dev_sck_t* dev, mio_iovec_t* iov, mio_iolen_t iovcnt, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_devaddr_t devaddr;
	return mio_dev_writev((mio_dev_t*)dev, iov, iovcnt, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

int mio_dev_sck_timedwrite (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_ntime_t* tmout, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_devaddr_t devaddr;
	return mio_dev_timedwrite((mio_dev_t*)dev, data, dlen, tmout, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

int mio_dev_sck_timedwritev (mio_dev_sck_t* dev, mio_iovec_t* iov, mio_iolen_t iovcnt, const mio_ntime_t* tmout, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_devaddr_t devaddr;
	return mio_dev_timedwritev((mio_dev_t*)dev, iov, iovcnt, tmout, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

/* ========================================================================= */
int mio_dev_sck_setsockopt (mio_dev_sck_t* dev, int level, int optname, void* optval, mio_scklen_t optlen)
{
	return setsockopt(dev->hnd, level, optname, optval, optlen);
}

int mio_dev_sck_getsockopt (mio_dev_sck_t* dev, int level, int optname, void* optval, mio_scklen_t* optlen)
{
	return getsockopt(dev->hnd, level, optname, optval, optlen);
}

int mio_dev_sck_getsockaddr (mio_dev_sck_t* dev, mio_skad_t* skad)
{
	mio_scklen_t addrlen = MIO_SIZEOF(*skad);
	if (getsockname(dev->hnd, (struct sockaddr*)skad, &addrlen) <= -1)
	{
		mio_seterrwithsyserr (dev->mio, 0, errno);
		return -1;
	}
	return 0;
}

int mio_dev_sck_getpeeraddr (mio_dev_sck_t* dev, mio_skad_t* skad)
{
	mio_scklen_t addrlen = MIO_SIZEOF(*skad);
	if (getpeername(dev->hnd, (struct sockaddr*)skad, &addrlen) <= -1)
	{
		mio_seterrwithsyserr (dev->mio, 0, errno);
		return -1;
	}
	return 0;
}

int mio_dev_sck_shutdown (mio_dev_sck_t* dev, int how)
{
	switch (how & (MIO_DEV_SCK_SHUTDOWN_READ | MIO_DEV_SCK_SHUTDOWN_WRITE))
	{
		case (MIO_DEV_SCK_SHUTDOWN_READ | MIO_DEV_SCK_SHUTDOWN_WRITE):
			how = SHUT_RDWR;
			break;

		case MIO_DEV_SCK_SHUTDOWN_READ:
			how = SHUT_RD;
			break;

		case MIO_DEV_SCK_SHUTDOWN_WRITE:
			how = SHUT_WR;
			break;

		default:
			mio_seterrnum (dev->mio, MIO_EINVAL);
			return -1;
	}

	if (shutdown(dev->hnd, how) <= -1)
	{
		mio_seterrwithsyserr (dev->mio, 0, errno);
		return -1;
	}

	return 0;
}

int mio_dev_sck_sendfileok (mio_dev_sck_t* dev)
{
#if defined(USE_SSL)
	return !(dev->ssl);
#else
	return 1;
#endif
}

int mio_dev_sck_writetosidechan (mio_dev_sck_t* dev, const void* dptr, mio_oow_t dlen)
{
	if (write(dev->side_chan, dptr, dlen) <= -1) return -1; /* this doesn't set the error information. if you may check errno, though */
	return 0;
}

/* ========================================================================= */

mio_uint16_t mio_checksum_ip (const void* hdr, mio_oow_t len)
{
	mio_uint32_t sum = 0;
	mio_uint16_t *ptr = (mio_uint16_t*)hdr;

	while (len > 1)
	{
		sum += *ptr++;
		if (sum & 0x80000000)
		sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}
 
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

	return (mio_uint16_t)~sum;
}

