/*-
 * Copyright (c) 2008,2009 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "extern.h"

static void	inoutstr(int dval);
static const char *get_coll(int dval);
static void	get_unit(int dval, unsigned int sz);

static const char *coll[] = {"Physical", "Application", "Logical", "Report",
    "Named Array", "Usage Switch", "Usage Modifier" };
static const char *unit[][5] = {
    {"None", "SI_Linear", "SI_Rotation", "English_Linear", "English_Rotation"},
    {"None", "Centimeter", "Radians", "Inch", "Degrees"},
    {"None", "Gram", "Gram", "Slug", "Slug"},
    {"None", "Seconds", "Seconds", "Seconds", "Seconds"},
    {"None", "Kelvin", "Kelvin", "Fahrenheit", "Fahrenheit"},
    {"None", "Ampere", "Ampere", "Ampere", "Ampere"},
    {"None", "Candela", "Candela", "Candela", "Candela"},
    {"None", "None", "None", "None", "None"}};

static int indent;
static int upage;

/* pretty print */
#define P(CALL) do {				\
	putchar('\t');				\
	for(i = 0; i < indent; i++)		\
		printf(" ");			\
	(CALL);					\
} while (0)

void
hexdump(unsigned char *rdesc, int size)
{
	unsigned char *p;
	int i;

	printf("[hexdump]");
	for (i = 0, p = rdesc; p - rdesc < size; p++, i++) {
		if (i % 16 == 0)
			printf("\n%04X", i);
		printf(" %02hhX", *p);
	}
	printf("\n");
}

void
dump_report_desc(unsigned char *rdesc, int size)
{
	unsigned char *p, *data;
	unsigned int bTag, bType, bSize;
	int sdval, dval, i, rs;

	bTag = 0;
	bType = 0;

	/* star parse */
	for (p = rdesc; p - rdesc < size;) {
		bSize = *p++;
		if (bSize == 0xfe) {
			/*
			 * long item
			 * XXX bSize probably only
			 * possess 1 byte, refer to spec.
			 */
			bSize = *p++;
			bSize |= *p++ << 8;
			bTag = *p++;
			data = p;
			p += bSize;
		} else {
			/* short item */
			bTag = bSize >> 4;
			bType = (bSize >> 2) & 3;
			bSize &= 3;
			if (bSize == 3) bSize = 4;
			data = p;
			p += bSize;
		}

		switch(bSize) {
		case 0:
			dval = 0;
			break;
		case 1:
			dval = *data++;
			break;
		case 2:
			dval = *data++;
			dval |= *data++ << 8;
			break;
		case 4:
			dval = *data++;
			dval |= *data++ << 8;
			dval |= *data++ << 16;
			dval |= *data++ << 24;
			break;
		default:
			printf("illegal bSize\n");
			return;
		}

		/* sign extend */
		rs = (sizeof(dval) - bSize) * 8;
		sdval = (dval << rs) >> rs;

		switch (bType) {
		case 0:			/* Main */
			switch (bTag) {
			case 8:		/* Input */
				P(printf("INPUT "));
				inoutstr(dval);
				printf(" (%d)\n", dval);
				break;
			case 9:
				P(printf("OUTPUT "));
				inoutstr(dval);
				printf(" (%d)\n", dval);
				break;
			case 10:
				P(printf("COLLECTION %s(%d)\n", get_coll(dval),
				    dval));
				indent += 2;
				break;
			case 11:
				P(printf("FEATURE "));
				inoutstr(dval);
				printf(" (%d)\n", dval);
				break;
			case 12:
				indent -= 2;
				P(printf("END COLLECTION\n"));
				break;
			default:
				printf("illegal Main Item (bTag=%u)\n", bTag);
				return;
			}
			break;
		case 1:		/* Global */
			switch (bTag) {
			case 0:
				P(printf("USAGE PAGE %s(%#x)\n",
				    usage_page(dval), dval));
				upage = dval;
				break;
			case 1:
				P(printf("LOGICAL MINIMUM %d\n", sdval));
				break;
			case 2:
				P(printf("LOGICAL MAXIMUM %d\n", sdval));
				break;
			case 3:
				P(printf("PHYSICAL MINIMUM %d\n", sdval));
				break;
			case 4:
				P(printf("PHYSICAL MAXIMUM %d\n", sdval));
				break;
			case 5:
				P(printf("UNIT EXPONENT %d\n", dval));
				break;
			case 6:
				P(printf("UNIT "));
				get_unit(dval, bSize);
				printf("(%d)\n", dval);
/* 				P(printf("UNIT %s(%d)\n", get_unit(dval, bSize), */
/* 				    dval)); */
				break;
			case 7:
				P(printf("REPORT SIZE %d\n", dval));
				break;
			case 8:
				P(printf("REPORT ID %d\n", dval));
				break;
			case 9:
				P(printf("REPORT COUNT %d\n", dval));
				break;
			case 10:
				P(printf("PUSH\n"));
				break;
			case 11:
				P(printf("POP\n"));
				break;
			default:
				printf("illegal Global Item (bTag=%u)\n", bTag);
				return;
			}
			break;
		case 2:		/* Local */
			switch (bTag) {
			case 0:
				P(printf("USAGE %s(%#x)[%s(%#x)]\n",
				    usage_in_page(upage, dval), dval,
				    usage_page(upage), upage));
				break;
			case 1:
				P(printf("USAGE MINIMUM %s(%d)\n",
				    usage_in_page(upage, dval), dval));
				break;
			case 2:
				P(printf("USAGE MAXIMUM %s(%d)\n",
				    usage_in_page(upage, dval), dval));
				break;
			case 3:
				P(printf("DESIGNATOR INDEX %d\n", dval));
				break;
			case 4:
				P(printf("DESIGNATOR MINIMUM %d\n", dval));
				break;
			case 5:
				P(printf("DESIGNATOR MAXIMUM %d\n", dval));
				break;
			case 6:
				printf("undefined local item: 0110");
				return;
			case 7:
				P(printf("STRING INDEX %d\n", dval));
				break;
			case 8:
				P(printf("STRING MINIMUM %d\n", dval));
				break;
			case 9:
				P(printf("STRING MAXIMUM %d\n", dval));
				break;
			case 10:
				P(printf("DELIMITER %d\n", dval));
				break;
			default:
				printf("illegal Local Item (bTag=%u)\n", bTag);
				return;
			}
			break;
		default:
			printf("illegal type (bType=%u)\n", bType);
		}
	}
}

static const char *
get_coll(int dval)
{
	if (dval <= 0x06)
		return(coll[dval]);
	else if (dval <= 0x7F)
		return("Reserved");
	else if (dval <= 0xFF)
		return("Vendor-defined");
	else
		return("undefined");
}

static void
get_unit(int dval, unsigned int sz)
{
	int i, sys, nibble;

	sys = dval & 0x0F;
	if (sys <= 0x04)
		goto normal;
	if (sys <= 0x0E)
		printf("Reserved");
	else
		printf("Vendor-defined");
	return;

normal:
	for (i = 1; (unsigned int)i < sz * 2; i++) {
		nibble = (dval >> (i * 4)) && 0x0F;
		if (!nibble)
			continue;
		if (nibble > 7)
			nibble -= 16;
		printf("[%s", unit[i][sys]);
		if (nibble != 1)
			printf("^%d", nibble);
		printf("]");
	}
}

#define HIO_CONST	0x001
#define HIO_VARIABLE	0x002
#define HIO_RELATIVE	0x004
#define HIO_WRAP	0x008
#define HIO_NONLINEAR	0x010
#define HIO_NOPREF	0x020
#define HIO_NULLSTATE	0x040
#define HIO_VOLATILE	0x080
#define HIO_BUFBYTES	0x100

static void
inoutstr(int dval)
{
	printf("(");
	if (dval & HIO_CONST)
		printf(" Const");
	else
		printf(" Data");
	if (dval & HIO_VARIABLE)
		printf(" Variable");
	else
		printf(" Array");
	if (dval & HIO_RELATIVE)
		printf(" Relative");
	else
		printf(" Absolute");
	if (dval >= 8) {
		if (dval & HIO_WRAP)
			printf(" Wrap");
		if (dval & HIO_NONLINEAR)
			printf(" NonLinear");
		if (dval & HIO_NOPREF)
			printf(" NoPreferred");
		if (dval & HIO_NULLSTATE)
			printf(" NullState");
		if (dval & HIO_VOLATILE)
			printf(" Volatile");
		if (dval & HIO_BUFBYTES)
			printf(" BufferedBytes");
	}
	printf(" )");
}
