PROG=	uhidd
#SRCS=	uhidd_hid.c uhidd_hidaction.c uhidd_hidparser.c uhidd_hidump.c	\
#	uhidd_kbd.c uhidd.c uhidd_mouse.c lex.l parser.y y.tab.h	\
#	usage_in_page.c usage_page.c
SRCS=	uhidd.c uhidd_hidparser.c uhidd_hidump.c uhidd_kbd.c lex.l \
	parser.y y.tab.h usage_in_page.c usage_page.c

GENSRCS=	usage_in_page.c usage_page.c
CLEANFILES=	${GENSRCS}
MAN=		uhidd.8

WARNS?=	6

DPADD=	${LIBUSB} ${LIBPTHREAD} ${LIBUTIL}
LDADD=	-lusb -lpthread -lutil

CFLAGS+=-I. -I${.CURDIR} -g

.SUFFIXES:	.awk .c
.awk.c:
	awk -f ${.IMPSRC} usb_hid_usages > ${.TARGET}

usage_in_page.c:	usb_hid_usages usage_in_page.awk
usage_page.c:		usb_hid_usages usage_page.awk

.include <bsd.prog.mk>
