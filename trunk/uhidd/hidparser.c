/*-
 * Copyright (c) 2009 Kai Wang
 * Copyright (c) 1999, 2001 Lennart Augustsson <augustss@netbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: trunk/uhidd/hidparser.c 19 2009-06-28 19:16:31Z kaiw27 $");

#include <sys/param.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/usb/usbhid.h>

#include "extern.h"

static void	hid_clear_local(hid_item_t *c);
static void	hid_parser_init(hid_parser_t p);
static int	hid_get_item_raw(hid_data_t s, hid_item_t *h);

static int
min(int x, int y)
{

	return (x < y ? x : y);
}

hid_parser_t
hid_parser_alloc(unsigned char *rdesc, int rsz)
{
	struct hid_parser *p;

	p = calloc(1, sizeof(*p));
	if (p == NULL)
		err(1, "calloc");
	memcpy(p->rdesc, rdesc, rsz);
	p->rsz = rsz;
	hid_parser_init(p);

	return (p);
}

void
hid_parser_free(hid_parser_t p)
{

	free(p);
}

int
hid_get_report_id_num(hid_parser_t p)
{

	return (p->nr);
}

void
hid_get_report_ids(hid_parser_t p, int *rid, int size)
{
	int s;

	s = min(size, p->nr);
	memcpy(rid, p->rid, s*sizeof(int));
}

static void
hid_parser_init(hid_parser_t p)
{
	unsigned char *b, *data;
	unsigned int bTag, bType, bSize;
	int dval, found, i;

	/* Collect all the report id(s) in the report desc. */
	b = p->rdesc;
	while (b < p->rdesc + p->rsz) {
		bSize = *b++;
		if (bSize == 0xfe) {
			/* long item */
			bSize = *b++;
			bTag = *b++;
			b += bSize;
		} else {
			/* short item */
			bTag = bSize >> 4;
			bType = (bSize >> 2) & 3;
			bSize &= 3;
			if (bSize == 3)
				bSize = 4;
			data = b;
			b += bSize;
			/* only interested in REPORT ID. */
			if (bType == 1 && bTag == 8) {
				switch (bSize) {
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
					continue;
				}
				found = 0;
				for (i = 0; i < p->nr; i++)
					if (dval == p->rid[i]) {
						found = 1;
						break;
					}
				if (!found)
					p->rid[p->nr++] = dval;
			}
		}
	}
}

static void
hid_clear_local(hid_item_t *c)
{
	c->usage = 0;
	c->usage_minimum = 0;
	c->usage_maximum = 0;
	c->designator_index = 0;
	c->designator_minimum = 0;
	c->designator_maximum = 0;
	c->string_index = 0;
	c->string_minimum = 0;
	c->string_maximum = 0;
	c->set_delimiter = 0;
}

hid_data_t
hid_start_parse(hid_parser_t p, int kindset)
{
	struct hid_data *s;

	s = calloc(1, sizeof(*s));
	if (s == NULL)
		err(1, "calloc");
	s->start = s->p = p->rdesc;
	s->end = p->rdesc + p->rsz;
	s->kindset = kindset;
	return (s);
}

void
hid_end_parse(hid_data_t s)
{
	while (s->cur.next) {
		hid_item_t *hi = s->cur.next->next;
		free(s->cur.next);
		s->cur.next = hi;
	}
	free(s);
}

int
hid_get_item(hid_data_t s, hid_item_t *h, int id)
{
	int r;

	for (;;) {
		r = hid_get_item_raw(s, h);
		if (r <= 0)
			break;
		/* -1 reprents any report id. */
		if (id == -1 || h->report_ID == id)
			break;
	}
	return (r);
}

static int
hid_get_item_raw(hid_data_t s, hid_item_t *h)
{
	hid_item_t *c;
	unsigned int bTag = 0, bType = 0, bSize;
	unsigned char *data;
	int dval;
	unsigned char *p;
	hid_item_t *hi;
	int i;
	hid_kind_t retkind;

	c = &s->cur;

top:
	if (s->multimax) {
		if (c->logical_minimum >= c->logical_maximum) {
			if (s->logminsize == 1)
				c->logical_minimum =(int8_t)c->logical_minimum;
			else if (s->logminsize == 2)
				c->logical_minimum =(int16_t)c->logical_minimum;
		}
		if (s->multi < s->multimax) {
			c->usage = s->usages[min(s->multi, s->nusage-1)];
			s->multi++;
			*h = *c;
			/*
			 * 'multimax' is only non-zero if the current
                         *  item kind is input/output/feature
			 */
			h->pos = s->kindpos[c->report_ID][c->kind];
			s->kindpos[c->report_ID][c->kind] += c->report_size;
			h->next = 0;
			return (1);
		} else {
			c->report_count = s->multimax;
			s->multimax = 0;
			s->nusage = 0;
			hid_clear_local(c);
		}
	}
	for (;;) {
		p = s->p;
		if (p >= s->end)
			return (0);

		bSize = *p++;
		if (bSize == 0xfe) {
			/* long item */
			bSize = *p++;
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
		s->p = p;
		/*
		 * The spec is unclear if the data is signed or unsigned.
		 */
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
			return (-1);
		}

		switch (bType) {
		case 0:			/* Main */
			switch (bTag) {
			case 8:		/* Input */
				retkind = hid_input;
			ret:
				if (!(s->kindset & (1 << retkind))) {
					/* Drop the items of this kind */
					s->nusage = 0;
					continue;
				}
				c->kind = retkind;
				c->flags = dval;
				if (c->flags & HIO_VARIABLE) {
					s->multimax = c->report_count;
					s->multi = 0;
					c->report_count = 1;
					if (s->minset) {
						for (i = c->usage_minimum;
						     i <= c->usage_maximum;
						     i++) {
							s->usages[s->nusage] = i;
							if (s->nusage < MAXUSAGE-1)
								s->nusage++;
						}
						c->usage_minimum = 0;
						c->usage_maximum = 0;
						s->minset = 0;
					}
					goto top;
				} else {
					if (s->minset)
						c->usage = c->usage_minimum;
					*h = *c;
					h->next = 0;
					h->pos = s->kindpos[c->report_ID][c->kind];
					s->kindpos[c->report_ID][c->kind] +=
					    c->report_size * c->report_count;
					hid_clear_local(c);
					s->minset = 0;
					return (1);
				}
			case 9:		/* Output */
				retkind = hid_output;
				goto ret;
			case 10:	/* Collection */
				c->kind = hid_collection;
				c->collection = dval;
				c->collevel++;
				*h = *c;
				hid_clear_local(c);
				s->nusage = 0;
				return (1);
			case 11:	/* Feature */
				retkind = hid_feature;
				goto ret;
			case 12:	/* End collection */
				c->kind = hid_endcollection;
				c->collevel--;
				*h = *c;
				/*hid_clear_local(c);*/
				s->nusage = 0;
				return (1);
			default:
				return (-2);
			}
			break;

		case 1:		/* Global */
			switch (bTag) {
			case 0:
				c->_usage_page = dval << 16;
				break;
			case 1:
				c->logical_minimum = dval;
				s->logminsize = bSize;
				break;
			case 2:
				c->logical_maximum = dval;
				break;
			case 3:
				c->physical_minimum = dval;
				break;
			case 4:
				c->physical_maximum = dval;
				break;
			case 5:
				c->unit_exponent = dval;
				break;
			case 6:
				c->unit = dval;
				break;
			case 7:
				c->report_size = dval;
				break;
			case 8:
				c->report_ID = dval;
				break;
			case 9:
				c->report_count = dval;
				break;
			case 10: /* Push */
				hi = malloc(sizeof(*hi));
				if (hi == NULL)
					err(1, "malloc");
				*hi = s->cur;
				c->next = hi;
				break;
			case 11: /* Pop */
				hi = c->next;
				s->cur = *hi;
				free(hi);
				break;
			default:
				return (-3);
			}
			break;
		case 2:		/* Local */
			switch (bTag) {
			case 0:
				c->usage = c->_usage_page | dval;
				if (s->nusage < MAXUSAGE)
					s->usages[s->nusage++] = c->usage;
				/* else XXX */
				break;
			case 1:
				s->minset = 1;
				c->usage_minimum = c->_usage_page | dval;
				break;
			case 2:
				c->usage_maximum = c->_usage_page | dval;
				break;
			case 3:
				c->designator_index = dval;
				break;
			case 4:
				c->designator_minimum = dval;
				break;
			case 5:
				c->designator_maximum = dval;
				break;
			case 7:
				c->string_index = dval;
				break;
			case 8:
				c->string_minimum = dval;
				break;
			case 9:
				c->string_maximum = dval;
				break;
			case 10:
				c->set_delimiter = dval;
				break;
			default:
				return (-4);
			}
			break;
		default:
			return (-5);
		}
	}
}

int
hid_report_size(hid_parser_t p, enum hid_kind k, int id)
{
	struct hid_data *d;
	hid_item_t h;
	int size;

	memset(&h, 0, sizeof h);
	size = 0;
	for (d = hid_start_parse(p, 1<<k); hid_get_item(d, &h, id); ) {
		if (h.report_ID == id && h.kind == k)
			size = d->kindpos[id][k];
	}
	hid_end_parse(d);
	return ((size + 7) / 8);
}

int
hid_locate(hid_parser_t p, unsigned int u, enum hid_kind k, hid_item_t *h)
{
	hid_data_t d;

	for (d = hid_start_parse(p, 1<<k); hid_get_item(d, h, -1); ) {
		if (h->kind == k && !(h->flags & HIO_CONST) && h->usage == u) {
			hid_end_parse(d);
			return (1);
		}
	}
	hid_end_parse(d);
	h->report_size = 0;
	return (0);
}

int
hid_get_data(const void *p, const hid_item_t *h)
{
	const unsigned char *buf;
	unsigned int hpos;
	unsigned int hsize;
	int data;
	int i, end, offs;

	buf = p;
	if (h->report_ID > 0) {
		if (h->report_ID != *buf)
			return (0);
		buf++;
	}
	hpos = h->pos;			/* bit position of data */
	hsize = h->report_size;		/* bit length of data */

	if (hsize == 0)
		return (0);
	offs = hpos / 8;
	end = (hpos + hsize) / 8 - offs;
	data = 0;
	for (i = 0; i <= end; i++)
		data |= buf[offs + i] << (i*8);
	data >>= hpos % 8;
	data &= (1 << hsize) - 1;
	if (h->logical_minimum < 0) {
		/* Need to sign extend */
		hsize = sizeof data * 8 - hsize;
		data = (data << hsize) >> hsize;
	}
	return (data);
}

void
hid_set_data(void *p, const hid_item_t *h, int data)
{
	unsigned char *buf;
	unsigned int hpos;
	unsigned int hsize;
	int i, end, offs, mask;

	buf = p;
	hpos = h->pos;			/* bit position of data */
	hsize = h->report_size;		/* bit length of data */

	if (hsize != 32) {
		mask = (1 << hsize) - 1;
		data &= mask;
	} else
		mask = ~0;

	data <<= (hpos % 8);
	mask <<= (hpos % 8);
	mask = ~mask;

	offs = hpos / 8;
	end = (hpos + hsize) / 8 - offs;

	for (i = 0; i <= end; i++)
		buf[offs + i] = (buf[offs + i] & (mask >> (i*8))) |
			((data >> (i*8)) & 0xff);
}
