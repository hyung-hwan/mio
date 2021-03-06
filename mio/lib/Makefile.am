AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_ALL_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_srcdir) \
	-I$(includedir)	

CFLAGS_ALL_COMMON  =
LDFLAGS_ALL_COMMON = -L$(abs_builddir) -L$(libdir)

EXTRA_DIST = utl-str.c.m4

##################################################
# MAIN LIBRARY 
##################################################

CPPFLAGS_LIB_COMMON = $(CPPFLAGS_ALL_COMMON)
CFLAGS_LIB_COMMON = $(CFLAGS_ALL_COMMON)
LDFLAGS_LIB_COMMON = $(LDFLAGS_ALL_COMMON) -version-info 1:0:0 -no-undefined

# $(LIBM) here for quadmath_snprintf() in libquadmath
# TODO: make this selective by moving MIO_ENABLE_FLTFMT to configure.ac
LIBADD_LIB_COMMON = $(LIBM)

#pkgincludedir = $(includedir)
#pkglibdir = $(libdir)
#pkgbindir = $(bindir)

include_HEADERS = \
	mio-cfg.h \
	mio-chr.h \
	mio-cmn.h \
	mio-dns.h \
	mio-ecs.h \
	mio-fmt.h \
	mio-htb.h \
	mio-htrd.h \
	mio-htre.h \
	mio-http.h \
	mio-json.h \
	mio-nwif.h \
	mio-opt.h \
	mio-pac1.h \
	mio-path.h \
	mio-pipe.h \
	mio-pro.h \
	mio-sck.h \
	mio-skad.h \
	mio-thr.h \
	mio-upac.h \
	mio-utl.h \
	mio.h

lib_LTLIBRARIES = libmio.la
libmio_la_SOURCES = \
	chr.c \
	dns.c \
	dns-cli.c \
	ecs.c \
	ecs-imp.h \
	err.c \
	fmt.c \
	fmt-imp.h \
	htb.c \
	htrd.c \
	htre.c \
	http.c \
	http-cgi.c \
	http-fil.c \
	http-prv.h \
	http-svr.c \
	http-thr.c \
	http-txt.c \
	json.c \
	mio-prv.h \
	mio.c \
	nwif.c \
	opt.c \
	opt-imp.h \
	path.c \
	pipe.c \
	pro.c \
	sck.c \
	skad.c \
	sys.c \
	sys-ass.c \
	sys-err.c \
	sys-log.c \
	sys-mux.c \
	sys-prv.h \
	sys-tim.c \
	thr.c \
	uch-case.h \
	uch-prop.h \
	tmr.c \
	utf8.c \
	utl.c \
	utl-siph.c \
	utl-str.c


libmio_la_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
libmio_la_CFLAGS = $(CFLAGS_LIB_COMMON)
libmio_la_LDFLAGS = $(LDFLAGS_LIB_COMMON)
libmio_la_LIBADD = $(LIBADD_LIB_COMMON) $(SSL_LIBS) $(SOCKET_LIBS) $(SENDFILE_LIBS)

if ENABLE_MARIADB
include_HEADERS += mio-mar.h
libmio_la_SOURCES += mar.c mar-cli.c
libmio_la_CFLAGS += $(MARIADB_CFLAGS)
libmio_la_LDFLAGS += $(MARIADB_LDFLAGS)
libmio_la_LIBADD += $(MARIADB_LIBS)
endif

if ENABLE_SSL
libmio_la_LIBADD += $(SSL_LIBS)
endif

install-data-hook:
	@echo "#ifndef _MIO_CFG_H_" > "$(DESTDIR)$(includedir)/mio-cfg.h"
	@echo "#define _MIO_CFG_H_" >> "$(DESTDIR)$(includedir)/mio-cfg.h"
	@$(EGREP) "#define[ ]+MIO_" "$(abs_builddir)/mio-cfg.h" >> "$(DESTDIR)$(includedir)/mio-cfg.h" 
	@echo "#endif" >> "$(DESTDIR)$(includedir)/mio-cfg.h"
	@rm -f "$(DESTDIR)$(includedir)/mio-cfg.h.in"
	@$(SED) 's|/\*#define MIO_HAVE_CFG_H\*/|#define MIO_HAVE_CFG_H|' "$(srcdir)/mio-cmn.h" > "$(DESTDIR)$(includedir)/mio-cmn.h"

uninstall-hook:
	@rm -f "$(DESTDIR)$(includedir)/mio-cfg.h"

utl-str.c: utl-str.c.m4
	m4 $(abs_srcdir)/utl-str.c.m4 > $(abs_srcdir)/utl-str.c
