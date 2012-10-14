PROG=	uhidd
SRCS=	uhidd.c uhidd_vhid_cuse.c uhidd_hidcore.c uhidd_hidump.c uhidd_kbd.c \
	uhidd_cc.c lex.l uhidd_mouse.c parser.y y.tab.h usage_in_page.c \
	usage_page.c uhidd_drivers.c uhidd_hidaction.c uhidd_cuse4bsd.c \
	usage_consumer.c

GENSRCS=	usage_in_page.c usage_page.c
CLEANFILES=	${GENSRCS}
MAN=		uhidd.conf.5 uhidd.8

WARNS?=	6

PREFIX?=	/usr/local
LOCALBASE?=	/usr/local
LIBDIR=		${PREFIX}/lib

DPADD=	${LIBUSB} ${LIBPTHREAD} ${LIBUTIL}
LDADD=	-lcuse4bsd -lusb -lpthread -lutil

CFLAGS+= -I. -I${.CURDIR} -I${LOCALBASE}/include -g
LDFLAGS+= -L${LIBDIR}

.SUFFIXES:	.awk .c
.awk.c:
	awk -f ${.IMPSRC} usb_hid_usages > ${.TARGET}

usage_in_page.c:	usb_hid_usages usage_in_page.awk
usage_page.c:		usb_hid_usages usage_page.awk

.include <bsd.prog.mk>
