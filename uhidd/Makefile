PROG=	uhidd
SRCS=	hidparser.c hidump.c kbd.c main.c mouse.c lex.l \
	parser.y y.tab.h usage_in_page.c usage_page.c

GENSRCS=	usage_in_page.c usage_page.c
CLEANFILES=	${GENSRCS}
NO_MAN=

WARNS?=	6

DPADD=	${LIBUSB} ${LIBPTHREAD}
LDADD=	-lusb -lpthread

CFLAGS+=-I. -I${.CURDIR} -g

.SUFFIXES:	.awk .c
.awk.c:
	awk -f ${.IMPSRC} usb_hid_usages > ${.TARGET}

usage_in_page.c:	usb_hid_usages usage_in_page.awk
usage_page.c:		usb_hid_usages usage_page.awk

.include <bsd.prog.mk>
