PROG=	uhidd
SRCS=	hidparser.c main.c

NO_MAN=

WARNS?=6

DPADD=	${LIBUSB}
LDADD=	-lusb

CFLAGS+=-I. -I${.CURDIR} -g

.include <bsd.prog.mk>
