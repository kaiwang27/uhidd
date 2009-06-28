PROG=	uhidd
SRCS=	hidparser.c main.c

NO_MAN=

WARNS?=6

DPADD=	${LIBUSB} ${LIBPTHREAD}
LDADD=	-lusb -lpthread

CFLAGS+=-I. -I${.CURDIR} -g

.include <bsd.prog.mk>
