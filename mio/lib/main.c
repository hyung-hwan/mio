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


#include <mio.h>
#include <mio-sck.h>
#include <mio-pro.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <net/if.h>

#include <assert.h>

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

struct mmgr_stat_t
{
	mio_oow_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (mio_mmgr_t* mmgr, mio_oow_t size)
{
	void* x;

	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 300)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return MIO_NULL;
	}

	x = malloc (size);
	if (x) ((mmgr_stat_t*)mmgr->ctx)->total_count++;
	return x;
}

static void* mmgr_realloc (mio_mmgr_t* mmgr, void* ptr, mio_oow_t size)
{
	return realloc (ptr, size);
}

static void mmgr_free (mio_mmgr_t* mmgr, void* ptr)
{
	((mmgr_stat_t*)mmgr->ctx)->total_count--;
	return free (ptr);
}


static mio_mmgr_t mmgr = 
{
	mmgr_alloc,
	mmgr_realloc,
	mmgr_free,
	&mmgr_stat
};

/* ========================================================================= */

#if defined(USE_SSL)
static void cleanup_openssl ()
{
	/* ERR_remove_state() should be called for each thread if the application is thread */
	ERR_remove_state (0);
#if defined(HAVE_ENGINE_CLEANUP)
	ENGINE_cleanup ();
#endif
	ERR_free_strings ();
	EVP_cleanup ();
#if defined(HAVE_CRYPTO_CLEANUP_ALL_EX_DATA)
	CRYPTO_cleanup_all_ex_data ();
#endif
}
#endif

struct tcp_server_t
{
	int tally;
};
typedef struct tcp_server_t tcp_server_t;

static void tcp_sck_on_disconnect (mio_dev_sck_t* tcp)
{
	switch (MIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case MIO_DEV_SCK_CONNECTING:
			printf ("OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			printf ("OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_LISTENING:
			printf ("SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_CONNECTED:
			printf ("OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			printf ("INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->sck);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			printf ("INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->sck);
			break;

		default:
			printf ("SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->sck, (unsigned int)tcp->state);
			break;
	}
}
static int tcp_sck_on_connect (mio_dev_sck_t* tcp)
{

	mio_sckfam_t fam;
	mio_scklen_t len;
	mio_bch_t buf1[128], buf2[128];

	memset (buf1, 0, MIO_SIZEOF(buf1));
	memset (buf2, 0, MIO_SIZEOF(buf2));

	mio_getsckaddrinfo (tcp->mio, &tcp->localaddr, &len, &fam);
	inet_ntop (fam, tcp->localaddr.data, buf1, MIO_COUNTOF(buf1));

	mio_getsckaddrinfo (tcp->mio, &tcp->remoteaddr, &len, &fam);
	inet_ntop (fam, tcp->remoteaddr.data, buf2, MIO_COUNTOF(buf2));

	if (tcp->state & MIO_DEV_SCK_CONNECTED)
	{

printf ("device connected to a remote server... LOCAL %s:%d REMOTE %s:%d.", buf1, mio_getsckaddrport(&tcp->localaddr), buf2, mio_getsckaddrport(&tcp->remoteaddr));

	}
	else if (tcp->state & MIO_DEV_SCK_ACCEPTED)
	{
printf ("device accepted client device... .LOCAL %s:%d REMOTE %s:%d\n", buf1, mio_getsckaddrport(&tcp->localaddr), buf2, mio_getsckaddrport(&tcp->remoteaddr));
	}

	return mio_dev_sck_write  (tcp, "hello", 5, MIO_NULL, MIO_NULL);
}

static int tcp_sck_on_write (mio_dev_sck_t* tcp, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	tcp_server_t* ts;

if (wrlen <= -1)
{
printf ("SEDING TIMED OUT...........\n");
	mio_dev_sck_halt(tcp);
}
else
{
	ts = (tcp_server_t*)(tcp + 1);
	printf (">>> SENT MESSAGE %d of length %ld\n", ts->tally, (long int)wrlen);

	ts->tally++;
//	if (ts->tally >= 2) mio_dev_sck_halt (tcp);

printf ("ENABLING READING..............................\n");
	mio_dev_sck_read (tcp, 1);

	//mio_dev_sck_timedread (tcp, 1, 1000);
}
	return 0;
}

static int tcp_sck_on_read (mio_dev_sck_t* tcp, const void* buf, mio_iolen_t len, const mio_sckaddr_t* srcaddr)
{
	int n;

	if (len <= 0)
	{
		printf ("STREAM DEVICE: EOF RECEIVED...\n");
		/* no outstanding request. but EOF */
		mio_dev_sck_halt (tcp);
		return 0;
	}

printf ("on read %d\n", (int)len);

{
mio_ntime_t tmout;

static char a ='A';
char* xxx = malloc (1000000);
memset (xxx, a++ ,1000000);

	//return mio_dev_sck_write  (tcp, "HELLO", 5, MIO_NULL);
	mio_inittime (&tmout, 5, 0);
	n = mio_dev_sck_timedwrite  (tcp, xxx, 1000000, &tmout, MIO_NULL, MIO_NULL);
free (xxx);


	if (n <= -1) return -1;
}


printf ("DISABLING READING..............................\n");
	mio_dev_sck_read (tcp, 0);

	/* post the write finisher */
	n = mio_dev_sck_write  (tcp, MIO_NULL, 0, MIO_NULL, MIO_NULL);
	if (n <= -1) return -1;

	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}

/* ========================================================================= */

static void pro_on_close (mio_dev_pro_t* dev, mio_dev_pro_sid_t sid)
{
printf (">>>>>>>>>>>>> ON CLOSE OF SLAVE %d.\n", sid);
}

static int pro_on_read (mio_dev_pro_t* dev, const void* data, mio_iolen_t dlen, mio_dev_pro_sid_t sid)
{
printf ("PROCESS READ DATA on SLAVE[%d]... [%.*s]\n", (int)sid, (int)dlen, (char*)data);
	return 0;
}


static int pro_on_write (mio_dev_pro_t* dev, mio_iolen_t wrlen, void* wrctx)
{
printf ("PROCESS WROTE DATA...\n");
	return 0;
}

/* ========================================================================= */

static int arp_sck_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	mio_etharp_pkt_t* eap;


	if (dlen < MIO_SIZEOF(*eap)) return 0; /* drop */

	eap = (mio_etharp_pkt_t*)data;

	printf ("ARP ON IFINDEX %d OPCODE: %d", mio_getsckaddrifindex(srcaddr), ntohs(eap->arphdr.opcode));

	printf (" SHA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.sha[0], eap->arppld.sha[1], eap->arppld.sha[2], eap->arppld.sha[3], eap->arppld.sha[4], eap->arppld.sha[5]);
	printf (" SPA: %d.%d.%d.%d", eap->arppld.spa[0], eap->arppld.spa[1], eap->arppld.spa[2], eap->arppld.spa[3]);
	printf (" THA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.tha[0], eap->arppld.tha[1], eap->arppld.tha[2], eap->arppld.tha[3], eap->arppld.tha[4], eap->arppld.tha[5]);
	printf (" TPA: %d.%d.%d.%d", eap->arppld.tpa[0], eap->arppld.tpa[1], eap->arppld.tpa[2], eap->arppld.tpa[3]);
	printf ("\n");
	return 0;
}

static int arp_sck_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	return 0;
}

static void arp_sck_on_disconnect (mio_dev_sck_t* dev)
{
printf ("SHUTTING DOWN ARP SOCKET %d...\n", dev->sck);
}

static int setup_arp_tester (mio_t* mio)
{
	mio_sckaddr_t ethdst;
	mio_etharp_pkt_t etharp;
	mio_dev_sck_make_t sck_make;
	mio_dev_sck_t* sck;

	memset (&sck_make, 0, MIO_SIZEOF(sck_make));
	sck_make.type = MIO_DEV_SCK_ARP;
	//sck_make.type = MIO_DEV_SCK_ARP_DGRAM;
	sck_make.on_write = arp_sck_on_write;
	sck_make.on_read = arp_sck_on_read;
	sck_make.on_disconnect = arp_sck_on_disconnect;
	sck = mio_dev_sck_make (mio, 0, &sck_make);
	if (!sck)
	{
		printf ("Cannot make socket device\n");
		return -1;
	}

	//mio_sckaddr_initforeth (&ethdst, if_nametoindex("enp0s25.3"), (mio_ethaddr_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
	mio_sckaddr_initforeth (&ethdst, if_nametoindex("enp0s25.3"), (mio_ethaddr_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");

	memset (&etharp, 0, sizeof(etharp));

	memcpy (etharp.ethhdr.source, "\xB8\x6B\x23\x9C\x10\x76", MIO_ETHADDR_LEN);
	//memcpy (etharp.ethhdr.dest, "\xFF\xFF\xFF\xFF\xFF\xFF", MIO_ETHADDR_LEN);
	memcpy (etharp.ethhdr.dest, "\xAA\xBB\xFF\xCC\xDD\xFF", MIO_ETHADDR_LEN);
	etharp.ethhdr.proto = MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP);

	etharp.arphdr.htype = MIO_CONST_HTON16(MIO_ARPHDR_HTYPE_ETH);
	etharp.arphdr.ptype = MIO_CONST_HTON16(MIO_ARPHDR_PTYPE_IP4);
	etharp.arphdr.hlen = MIO_ETHADDR_LEN;
	etharp.arphdr.plen = MIO_IP4ADDR_LEN;
	etharp.arphdr.opcode = MIO_CONST_HTON16(MIO_ARPHDR_OPCODE_REQUEST);

	memcpy (etharp.arppld.sha, "\xB8\x6B\x23\x9C\x10\x76", MIO_ETHADDR_LEN);

	if (mio_dev_sck_write (sck, &etharp, sizeof(etharp), NULL, &ethdst) <= -1)
	//if (mio_dev_sck_write (sck, &etharp.arphdr, sizeof(etharp) - sizeof(etharp.ethhdr), NULL, &ethaddr) <= -1)
	{
		printf ("CANNOT WRITE ARP...\n");
	}


	return 0;
}

/* ========================================================================= */

struct icmpxtn_t
{
	mio_uint16_t icmp_seq;
	mio_tmridx_t tmout_jobidx;
	int reply_received;
};

typedef struct icmpxtn_t icmpxtn_t;

static int schedule_icmp_wait (mio_dev_sck_t* dev);

static void send_icmp (mio_dev_sck_t* dev, mio_uint16_t seq)
{
	mio_sckaddr_t dstaddr;
	mio_ip4addr_t ia;
	mio_icmphdr_t* icmphdr;
	mio_uint8_t buf[512];

	inet_pton (AF_INET, "192.168.1.131", &ia);
	mio_sckaddr_initforip4 (&dstaddr, 0, &ia);

	memset(buf, 0, MIO_SIZEOF(buf));
	icmphdr = (mio_icmphdr_t*)buf;
	icmphdr->type = MIO_ICMP_ECHO_REQUEST;
	icmphdr->u.echo.id = MIO_CONST_HTON16(100);
	icmphdr->u.echo.seq = mio_hton16(seq);

	memset (&buf[MIO_SIZEOF(*icmphdr)], 'A', MIO_SIZEOF(buf) - MIO_SIZEOF(*icmphdr));
	icmphdr->checksum = mio_checksumip (icmphdr, MIO_SIZEOF(buf));

	if (mio_dev_sck_write (dev, buf, MIO_SIZEOF(buf), NULL, &dstaddr) <= -1)
	{
		printf ("CANNOT WRITE ICMP...\n");
		mio_dev_sck_halt (dev);
	}

	if (schedule_icmp_wait (dev) <= -1)
	{
		printf ("CANNOT SCHEDULE ICMP WAIT...\n");
		mio_dev_sck_halt (dev);
	}
}

static void on_icmp_due (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* tmrjob)
{
	mio_dev_sck_t* dev;
	icmpxtn_t* icmpxtn;

	dev = tmrjob->ctx;
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (icmpxtn->reply_received)
		icmpxtn->reply_received = 0;
	else
		printf ("NO ICMP REPLY RECEIVED....\n");

	send_icmp (dev, ++icmpxtn->icmp_seq);
}

static int schedule_icmp_wait (mio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;
	mio_tmrjob_t tmrjob;
	mio_ntime_t fire_after;

	icmpxtn = (icmpxtn_t*)(dev + 1);
	mio_inittime (&fire_after, 2, 0);

	memset (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	mio_gettime (&tmrjob.when);
	mio_addtime (&tmrjob.when, &fire_after, &tmrjob.when);
	tmrjob.handler = on_icmp_due;
	tmrjob.idxptr = &icmpxtn->tmout_jobidx;

	assert (icmpxtn->tmout_jobidx == MIO_TMRIDX_INVALID);

	return (mio_instmrjob (dev->mio, &tmrjob) == MIO_TMRIDX_INVALID)? -1: 0;
}

static int icmp_sck_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	icmpxtn_t* icmpxtn;
	mio_iphdr_t* iphdr;
	mio_icmphdr_t* icmphdr;

	/* when received, the data contains the IP header.. */
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (dlen < MIO_SIZEOF(*iphdr) + MIO_SIZEOF(*icmphdr))
	{
		printf ("INVALID ICMP PACKET.. TOO SHORT...%d\n", (int)dlen);
	}
	else
	{
		/* TODO: consider IP options... */
		iphdr = (mio_iphdr_t*)data;

		if (iphdr->ihl * 4 + MIO_SIZEOF(*icmphdr) > dlen)
		{
			printf ("INVALID ICMP PACKET.. WRONG IHL...%d\n", (int)iphdr->ihl * 4);
		}
		else
		{
			icmphdr = (mio_icmphdr_t*)((mio_uint8_t*)data + (iphdr->ihl * 4));

			/* TODO: check srcaddr against target */

			if (icmphdr->type == MIO_ICMP_ECHO_REPLY && 
			    mio_ntoh16(icmphdr->u.echo.seq) == icmpxtn->icmp_seq) /* TODO: more check.. echo.id.. */
			{
				icmpxtn->reply_received = 1;
				printf ("ICMP REPLY RECEIVED...ID %d SEQ %d\n", (int)mio_ntoh16(icmphdr->u.echo.id), (int)mio_ntoh16(icmphdr->u.echo.seq));
			}
			else
			{
				printf ("GARBAGE ICMP PACKET...LEN %d SEQ %d,%d\n", (int)dlen, (int)icmpxtn->icmp_seq, (int)mio_ntoh16(icmphdr->u.echo.seq));
			}
		}
	}
	return 0;
}


static int icmp_sck_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	/*icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1); */

	return 0;
}

static void icmp_sck_on_disconnect (mio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1);

printf ("SHUTTING DOWN ICMP SOCKET %d...\n", dev->sck);
	if (icmpxtn->tmout_jobidx != MIO_TMRIDX_INVALID)
	{

		mio_deltmrjob (dev->mio, icmpxtn->tmout_jobidx);
		icmpxtn->tmout_jobidx = MIO_TMRIDX_INVALID;
	}
}

static int setup_ping4_tester (mio_t* mio)
{
	mio_dev_sck_make_t sck_make;
	mio_dev_sck_t* sck;
	icmpxtn_t* icmpxtn;

	memset (&sck_make, 0, MIO_SIZEOF(sck_make));
	sck_make.type = MIO_DEV_SCK_ICMP4;
	sck_make.on_write = icmp_sck_on_write;
	sck_make.on_read = icmp_sck_on_read;
	sck_make.on_disconnect = icmp_sck_on_disconnect;

	sck = mio_dev_sck_make (mio, MIO_SIZEOF(icmpxtn_t), &sck_make);
	if (!sck)
	{
		printf ("Cannot make ICMP4 socket device\n");
		return -1;
	}

	icmpxtn = (icmpxtn_t*)(sck + 1);
	icmpxtn->tmout_jobidx = MIO_TMRIDX_INVALID;
	icmpxtn->icmp_seq = 0;

	/*TODO: mio_dev_sck_setbroadcast (sck, 1);*/

	send_icmp (sck, ++icmpxtn->icmp_seq);

	return 0;
}


/* ========================================================================= */

static mio_t* g_mio;

static void handle_signal (int sig)
{
	if (g_mio) mio_stop (g_mio, MIO_STOPREQ_TERMINATION);
}

int main ()
{
	int i;

	mio_t* mio;
	mio_dev_sck_t* tcp[3];

	struct sigaction sigact;
	mio_dev_sck_connect_t tcp_conn;
	mio_dev_sck_listen_t tcp_lstn;
	mio_dev_sck_bind_t tcp_bind;
	mio_dev_sck_make_t tcp_make;

	tcp_server_t* ts;

#if defined(USE_SSL)
	SSL_load_error_strings ();
	SSL_library_init ();
#endif

	mio = mio_open (&mmgr, 0, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		return -1;
	}

	g_mio = mio;

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = handle_signal;
	sigaction (SIGINT, &sigact, MIO_NULL);

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, MIO_NULL);

/*
	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigact, MIO_NULL);
*/

	/*memset (&sin, 0, MIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234); */
/*
	udp = (mio_dev_udp_t*)mio_makedev (mio, MIO_SIZEOF(*udp), &udp_mth, &udp_evcb, &sin);
	if (!udp)
	{
		printf ("Cannot make udp\n");
		goto oops;
	}
*/

	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcp[0] = mio_dev_sck_make (mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[0])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}

	ts = (tcp_server_t*)(tcp[0] + 1);
	ts->tally = 0;


	memset (&tcp_conn, 0, MIO_SIZEOF(tcp_conn));
{
	in_addr_t ia = inet_addr("192.168.1.119");
	mio_sckaddr_initforip4 (&tcp_conn.remoteaddr, 9999, (mio_ip4addr_t*)&ia);
}

	mio_inittime (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.on_connect = tcp_sck_on_connect;
	tcp_conn.options = MIO_DEV_SCK_CONNECT_SSL;
	if (mio_dev_sck_connect (tcp[0], &tcp_conn) <= -1)
	{
		printf ("mio_dev_sck_connect() failed....\n");
		/* carry on regardless of failure */
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[1] = mio_dev_sck_make (mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[1])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[1] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, MIO_SIZEOF(tcp_bind));
	mio_sckaddr_initforip4 (&tcp_bind.localaddr, 1234, MIO_NULL);
	tcp_bind.options = MIO_DEV_SCK_BIND_REUSEADDR;

	if (mio_dev_sck_bind (tcp[1],&tcp_bind) <= -1)
	{
		printf ("mio_dev_sck_bind() failed....\n");
		goto oops;
	}


	tcp_lstn.backlogs = 100;
	tcp_lstn.on_connect = tcp_sck_on_connect;
	if (mio_dev_sck_listen (tcp[1], &tcp_lstn) <= -1)
	{
		printf ("mio_dev_sck_listen() failed....\n");
		goto oops;
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, MIO_SIZEOF(&tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[2] = mio_dev_sck_make (mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[2])
	{
		printf ("Cannot make tcp\n");
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[2] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, MIO_SIZEOF(tcp_bind));
	mio_sckaddr_initforip4 (&tcp_bind.localaddr, 1235, MIO_NULL);
	tcp_bind.options = MIO_DEV_SCK_BIND_REUSEADDR | /*MIO_DEV_SCK_BIND_REUSEPORT |*/ MIO_DEV_SCK_BIND_SSL; 
	tcp_bind.ssl_certfile = "localhost.crt";
	tcp_bind.ssl_keyfile = "localhost.key";
	mio_inittime (&tcp_bind.accept_tmout, 5, 1);

	if (mio_dev_sck_bind (tcp[2],&tcp_bind) <= -1)
	{
		printf ("mio_dev_sck_bind() failed....\n");
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	tcp_lstn.on_connect = tcp_sck_on_connect;
	if (mio_dev_sck_listen (tcp[2], &tcp_lstn) <= -1)
	{
		printf ("mio_dev_sck_listen() failed....\n");
		goto oops;
	}

	//mio_dev_sck_sendfile (tcp[2], fd, offset, count);

	if (setup_arp_tester(mio) <= -1) goto oops;
	if (setup_ping4_tester(mio) <= -1) goto oops;


for (i = 0; i < 5; i++)
{
	mio_dev_pro_t* pro;
	mio_dev_pro_make_t pro_make;

	memset (&pro_make, 0, MIO_SIZEOF(pro_make));
	pro_make.flags = MIO_DEV_PRO_READOUT | MIO_DEV_PRO_READERR | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	//pro_make.cmd = "/bin/ls -laF /usr/bin";
	//pro_make.cmd = "/bin/ls -laF";
	pro_make.cmd = "./a";
	pro_make.on_read = pro_on_read;
	pro_make.on_write = pro_on_write;
	pro_make.on_close = pro_on_close;

	pro = mio_dev_pro_make (mio, 0, &pro_make);
	if (!pro)
	{
		printf ("CANNOT CREATE PROCESS PIPE\n");
		goto oops;
	}

	mio_dev_pro_write (pro, "MY MIO LIBRARY\n", 16, MIO_NULL);
//mio_dev_pro_killchild (pro); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_IN); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_OUT); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_ERR); 
}

	mio_loop (mio);

	g_mio = MIO_NULL;
	mio_close (mio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif

	return 0;

oops:
	g_mio = MIO_NULL;
	mio_close (mio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif
	return -1;
}
