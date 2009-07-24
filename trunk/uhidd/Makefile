PROG=	uhidd
SRCS=	hidparser.c hidump.c main.c usage_in_page.c usage_page.c

NO_MAN=

WARNS?=6

DPADD=	${LIBUSB} ${LIBPTHREAD}
LDADD=	-lusb -lpthread

CFLAGS+=-I. -I${.CURDIR} -g

.include <bsd.prog.mk>
