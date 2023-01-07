# $FreeBSD$

.include <src.opts.mk>

PROG=	jectl
MAN=

SRCS=	jectl.c 		\
	jectl_activate.c	\
	jectl_util.c		\
	jectl_dump.c		\
	jectl_import.c 		\
	jectl_mount.c 		\
	jectl_unmount.c 	\
	jectl_update.c

LIBADD+=nvpair \
	zfs

CFLAGS+= -DIN_BASE
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/include
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/lib/libspl/include/
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/lib/libspl/include/os/freebsd
CFLAGS+= -I${SRCTOP}/sys/contrib/openzfs/lib/libzfs
CFLAGS+= -include ${SRCTOP}/sys/contrib/openzfs/include/os/freebsd/spl/sys/ccompile.h
CFLAGS.jectl.c=			-Wno-cast-qual
CFLAGS.jectl_activate.c=	-Wno-cast-qual
CFLAGS.jectl_util.c=		-Wno-cast-qual
CFLAGS.jectl_dump.c=		-Wno-cast-qual
CFLAGS.jectl_import.c=		-Wno-cast-qual
CFLAGS.jectl_mount.c=		-Wno-cast-qual
CFLAGS.jectl_unmount.c=		-Wno-cast-qual
CFLAGS.jectl_update.c=		-Wno-cast-qual

.include <bsd.prog.mk>
