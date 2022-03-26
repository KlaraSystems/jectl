# $FreeBSD$

.include <src.opts.mk>

PROG=	jectl
MAN=

SRCS=	jectl.c

LIBADD+=nvpair \
	zfs

CFLAGS+= -DIN_BASE
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/include
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/lib/libspl/include/
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/lib/libspl/include/os/freebsd
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/lib/libzfs
CFLAGS+= -include ${SRCTOP}/sys/contrib/openzfs/include/os/freebsd/spl/sys/ccompile.h
CFLAGS.jectl.c=	-Wno-cast-qual

.include <bsd.prog.mk>
