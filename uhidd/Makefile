# Get __FreeBSD_version
.if !defined(OSVERSION)
.if exists(/usr/include/sys/param.h)
OSVERSION!= awk '/^\#define[[:blank:]]__FreeBSD_version/ {print $$3}' < /usr/include/sys/param.h
.else
OSVERSION!= sysctl -n kern.osreldate
.endif
.endif

PROG=	uhidd
SRCS=	uhidd.c uhidd_vhid_cuse.c uhidd_hidcore.c uhidd_hidump.c uhidd_kbd.c \
	uhidd_cc.c lex.l uhidd_mouse.c parser.y y.tab.h usage_in_page.c \
	usage_page.c uhidd_drivers.c uhidd_hidaction.c uhidd_cuse4bsd.c \
	uhidd_evdev.c uhidd_evdev_utils.c usage_consumer.c lex.kbdmap.c \
	drv_microsoft.c

GENSRCS=	usage_in_page.c usage_page.c lex.kbdmap.c
CLEANFILES=	${GENSRCS}
MAN=		uhidd.conf.5 uhidd.8

WARNS?=	5

PREFIX?=	/usr/local
LOCALBASE?=	/usr/local
LIBDIR=		${PREFIX}/lib

DPADD=	${LIBUSB} ${LIBPTHREAD} ${LIBUTIL}
LDADD=	-lusb -lpthread -lutil

.if ${OSVERSION} >= 1100023
LDADD+=	-lcuse
.else
LDADD+=	-lcuse4bsd
.endif

CFLAGS+= -I. -I${.CURDIR} -I${LOCALBASE}/include -g
LDFLAGS+= -L${LIBDIR}

lex.kbdmap.c:	kbdmap_lex.l
	${LEX} ${LFLAGS} -Pkbdmap ${.ALLSRC}

.SUFFIXES:	.awk .c
.awk.c:
	awk -f ${.IMPSRC} usb_hid_usages > ${.TARGET}

usage_in_page.c:	usb_hid_usages usage_in_page.awk
usage_page.c:		usb_hid_usages usage_page.awk

.include <bsd.prog.mk>
