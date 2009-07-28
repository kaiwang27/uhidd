PROG=	uhidd
SRCS=	hidparser.c hidump.c kbd.c main.c mouse.c usage_in_page.c \
	usage_page.c lex.l parser.y y.tab.h

NO_MAN=

WARNS?=6

DPADD=	${LIBUSB} ${LIBPTHREAD}
LDADD=	-lusb -lpthread

CFLAGS+=-I. -I${.CURDIR} -g

.include <bsd.prog.mk>
