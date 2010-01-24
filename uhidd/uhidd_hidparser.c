/*-
 * Copyright (c) 2009, 2010 Kai Wang
 * All rights reserved.
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
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uhidd.h"

static void	hid_clear_local(struct hid_state *c);
static void	hid_parser_init(struct hid_interface * p);
static void	hid_parser_dump(struct hid_interface * p);
static void	hid_appcol_recv_data(struct hid_appcol *, struct hid_report *,
    uint8_t *, int);
#if 0
static int	hid_get_item_raw(hid_data_t s, hid_item_t *h);
#endif

static STAILQ_HEAD(, hid_driver) hdlist = STAILQ_HEAD_INITIALIZER(hdlist);

#if 0
static int
min(int x, int y)
{

	return (x < y ? x : y);
}
#endif

struct hid_interface *
hid_interface_alloc(unsigned char *rdesc, int rsz)
{
	struct hid_interface *hi;

	hi = calloc(1, sizeof(*hi));
	if (hi == NULL)
		err(1, "calloc");
	memcpy(hi->rdesc, rdesc, rsz);
	hi->rsz = rsz;
	STAILQ_INIT(&hi->halist);
	hid_parser_init(hi);

	return (hi);
}

void
hid_interface_free(struct hid_interface *hi)
{

	free(hi);
}

void
hid_interface_input_data(struct hid_interface *hi, char *data, int len)
{
	struct hid_appcol *ha;
	struct hid_report *hr;

	STAILQ_FOREACH(ha, &hi->halist, ha_next) {
		STAILQ_FOREACH(hr, &ha->ha_hrlist, hr_next) {
			if (hr->hr_id == 0 || hr->hr_id == *data) {
				hid_appcol_recv_data(ha, hr, data, len);
				return;
			}
		}
	}

	if (verbose)
		printf("received data which doesn't belong to any appcol\n");
}

static struct hid_state *
hid_new_state(void)
{

	return (calloc(1, sizeof(struct hid_state)));
}

static struct hid_state *
hid_push_state(struct hid_state *cur_hs)
{
	struct hid_state *hs;

	assert(cur_hs != NULL);
	if ((hs = hid_new_state()) == NULL)
		return (NULL);

	*hs = *cur_hs;
	hs->stack_next = cur_hs;

	return (hs);
}

static struct hid_state *
hid_pop_state(struct hid_state *cur_hs)
{
	struct hid_state *hs;

	assert(cur_hs != NULL);
	hs = cur_hs->stack_next;
	free(cur_hs);

	return (hs);
}

static struct hid_appcol *
hid_add_appcol(struct hid_interface *hi, unsigned int usage)
{
	struct hid_appcol *ha;

	assert(hi != NULL);
	
	if ((ha = calloc(1, sizeof(*ha))) == NULL)
		return (NULL);

	ha->ha_usage = usage;
	STAILQ_INIT(&ha->ha_hrlist);
	STAILQ_INSERT_TAIL(&hi->halist, ha, ha_next);

	return (ha);
}

static void
hid_end_appcol(struct hid_appcol *ha, unsigned char *ha_start,
    unsigned char *ha_end)
{

	assert(ha != NULL && ha_start != NULL && ha_end != NULL);
	ha->ha_rsz = ha_end - ha_start;
	memcpy(ha->ha_rdesc, ha_start, ha->ha_rsz);
}

static struct hid_report *
hid_find_report(struct hid_appcol *ha, int report_id)
{
	struct hid_report *hr;

	assert(ha != NULL);

	STAILQ_FOREACH(hr, &ha->ha_hrlist, hr_next) {
		if (hr->hr_id == report_id)
			return (hr);
	}

	return (NULL);
}

static struct hid_report *
hid_add_report(struct hid_appcol *ha, int report_id)
{
	struct hid_report *hr;
	int i;

	assert(ha != NULL);
	if ((hr = calloc(1, sizeof(*hr))) == NULL)
		return (NULL);

	hr->hr_id = report_id;
	for (i = 0; i < 3; i++)
		STAILQ_INIT(&hr->hr_hflist[i]);
	STAILQ_INSERT_TAIL(&ha->ha_hrlist, hr, hr_next);

	return (hr);
}

static void
hid_add_field(struct hid_report *hr, struct hid_state *hs, enum hid_kind kind,
    int flags, int nusage, unsigned int usages[])
{
	struct hid_field *hf;
	int i;

	if ((hf = calloc(1, sizeof(*hf))) == NULL)
		err(1, "hid_parser: calloc");

	STAILQ_INSERT_TAIL(&hr->hr_hflist[kind], hf, hf_next);

	hf->hf_flags = flags;
	hf->hf_pos = hr->hr_pos[kind];
	hf->hf_count = hs->report_count;
	hf->hf_size = hs->report_size;
	if (hs->logical_minimum >= hs->logical_maximum) {
		if (hs->logminsize == 1)
			hs->logical_minimum = (int8_t) hs->logical_minimum;
		else if (hs->logminsize == 2)
			hs->logical_minimum = (int16_t) hs->logical_minimum;
	}
	hf->hf_logic_min = hs->logical_minimum;
	hf->hf_logic_max = hs->logical_maximum;
	hf->hf_usage = calloc(hf->hf_count, sizeof(*hf->hf_usage));
	hf->hf_value = calloc(hf->hf_count, sizeof(*hf->hf_value));
	if (hf->hf_usage == NULL || hf->hf_value == NULL)
		err(1, "hid_parser: calloc");

	hf->hf_usage_page = hs->usage_page;
	for (i = 0; i < nusage; i++)
		hf->hf_nusage[i] = usages[i];
	hf->hf_usage_min = hs->usage_minimum;
	hf->hf_usage_max = hs->usage_maximum;

	hr->hr_pos[kind] += hf->hf_count * hf->hf_size;
}

static void
hid_parser_init(struct hid_interface *hi)
{
	struct hid_state *hs;
	struct hid_appcol *ha;
	struct hid_report *hr;
	struct hid_driver *hd, *mhd;
	unsigned char *b, *data, *ha_start;
	unsigned int bTag, bType, bSize;
	int dval, nusage, collevel, minset, i, match, old_match;
	unsigned int usages[MAXUSAGE];

#define	CHECK_REPORT_0							\
	do {								\
		if (hr == NULL) {					\
			assert(ha != NULL);				\
			hr = hid_add_report(ha, 0);			\
			if (hr == NULL)					\
				errx(1, "hid_add_report failed");	\
		}							\
	} while(0)
	
	assert(hi != NULL);

	ha = NULL;
	hr = NULL;
	if ((hs = hid_new_state()) == NULL)
		err(1, "calloc");
	minset = 0;
	nusage = 0;
	collevel = 0;
	ha_start = hi->rdesc;

	b = hi->rdesc;
	while (b < hi->rdesc + hi->rsz) {
		bSize = *b++;

		/* Skip long item */
		if (bSize == 0xfe) {
			bSize = *b++;
			bTag = *b++;
			b += bSize;
			continue;
		}

		/* Short item otherwise. */
		bTag = bSize >> 4;
		bType = (bSize >> 2) & 3;
		bSize &= 3;
		if (bSize == 3)
			bSize = 4;
		data = b;
		b += bSize;

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
			errx(1, "hid_parser: internal error bSize==%u", bSize);
		}

		switch (bType) {
		case 0:		/* Main */
			switch (bTag) {
			case 8:		/* Input */
				CHECK_REPORT_0;
				hid_add_field(hr, hs, HID_INPUT, dval, nusage,
				    usages);
				nusage = 0;
				hid_clear_local(hs);
				break;
			case 9:		/* Output */
				CHECK_REPORT_0;
				hid_add_field(hr, hs, HID_OUTPUT, dval, nusage,
				    usages);
				nusage = 0;
				hid_clear_local(hs);
				break;
			case 10:	/* Collection */
				if (dval == 0x01 && collevel == 0) {
					/* Top-Level Application Collection */
					ha = hid_add_appcol(hi, hs->usage);
				}
				collevel++;
				hid_clear_local(hs);
				nusage = 0;
				break;
			case 11:	/* Feature */
				CHECK_REPORT_0;
				hid_add_field(hr, hs, HID_FEATURE, dval, nusage,
				    usages);
				nusage = 0;
				hid_clear_local(hs);
				break;
			case 12:	/* End collection */
				collevel--;
				/*hid_clear_local(c);*/
				nusage = 0;
				if (collevel == 0 && ha != NULL) {
					hid_end_appcol(ha, ha_start, b);
					ha_start = b;
				}
				break;
			default:
				warnx("hid_parser: unknown Main item(%u)",
				    bTag);
			}
			break;

		case 1:		/* Global */
			switch (bTag) {
			case 0:
				hs->usage_page = dval << 16;
				break;
			case 1:
				hs->logical_minimum = dval;
				hs->logminsize = bSize;
				break;
			case 2:
				hs->logical_maximum = dval;
				break;
			case 3:
				hs->physical_minimum = dval;
				break;
			case 4:
				hs->physical_maximum = dval;
				break;
			case 5:
				hs->unit_exponent = dval;
				break;
			case 6:
				hs->unit = dval;
				break;
			case 7:
				hs->report_size = dval;
				break;
			case 8:
				assert(ha != NULL);
				hr = hid_find_report(ha, dval);
				if (hr == NULL) {
					hr = hid_add_report(ha, dval);
					if (hr == NULL)
						errx(1, "hid_parser: "
						    "hid_add_report failed");
				}
				break;
			case 9:
				hs->report_count = dval;
				break;
			case 10: /* Push */
				hs = hid_push_state(hs);
				if (hs == NULL)
					errx(1, "hid_parser: "
					    "hid_push_state failed");
				break;
			case 11: /* Pop */
				hs = hid_pop_state(hs);
				if (hs == NULL)
					errx(1, "hid_parser: "
					    "hid_pop state failed");
				break;
			default:
				warnx("hid_parser: unknown Global item(%u)",
				    bTag);
				break;
			}
			break;
		case 2:		/* Local */
			switch (bTag) {
			case 0:
				hs->usage = hs->usage_page | dval;
				if (nusage < MAXUSAGE)
					usages[nusage++] = hs->usage;
				/* else XXX */
				break;
			case 1:
				hs->usage_minimum = hs->usage_page | dval;
				minset = 1;
				break;
			case 2:
				hs->usage_maximum = hs->usage_page | dval;
				if (minset) {
					for (i = hs->usage_minimum;
					     i <= hs->usage_maximum &&
					     nusage < MAXUSAGE;
					     i++)
						usages[nusage++] = i;
					minset = 0;
				}
				break;
			case 3:
				hs->designator_index = dval;
				break;
			case 4:
				hs->designator_minimum = dval;
				break;
			case 5:
				hs->designator_maximum = dval;
				break;
			case 7:
				hs->string_index = dval;
				break;
			case 8:
				hs->string_minimum = dval;
				break;
			case 9:
				hs->string_maximum = dval;
				break;
			case 10:
				hs->set_delimiter = dval;
				break;
			default:
				warnx("hid_parser: unknown Local item(%u)",
				    bTag);
				break;
			}
			break;
		default:
			warnx("hid_parser: unknown bType(%u)", bType);
		}

	}

	if (verbose)
		hid_parser_dump(hi);

	/*
	 * Attach drivers.
	 */
	STAILQ_FOREACH(ha, &hi->halist, ha_next) {
		mhd = NULL;
		old_match = 0;
		STAILQ_FOREACH(hd, &hdlist, hd_next) {
			match = hd->hd_match(ha);
			if (match != HID_MATCH_NONE) {
				if (mhd == NULL || match > old_match) {
					mhd = hd;
					old_match= match;
				}
			}
		}
		if (mhd != NULL) {
			if (verbose)
				printf("find matching driver\n");
			mhd->hd_attach(ha);
			ha->ha_hd = mhd;
		} else {
			if (verbose)
				printf("no matching driver\n");
		}
	}

#undef CHECK_REPORT_0
}

static void
hid_clear_local(struct hid_state *hs)
{
	hs->usage = 0;
	hs->usage_minimum = 0;
	hs->usage_maximum = 0;
	hs->designator_index = 0;
	hs->designator_minimum = 0;
	hs->designator_maximum = 0;
	hs->string_index = 0;
	hs->string_minimum = 0;
	hs->string_maximum = 0;
	hs->set_delimiter = 0;
}

static void
hid_parser_dump(struct hid_interface *hi)
{
	struct hid_appcol *ha;
	struct hid_report *hr;
	struct hid_field *hf;
	unsigned int up, u;
	int i, j;

#define PRINT_FIELD							\
	do {								\
		printf("      POS:%d SIZE:%d COUNT:%d ",		\
		    hf->hf_pos, hf->hf_size, hf->hf_count);		\
		if (hf->hf_flags & HIO_CONST)				\
			printf("[CONST]\n");				\
		else if (hf->hf_flags & HIO_VARIABLE) {			\
			printf("[VARIABLE]\n");				\
			for (j = 0; j < hf->hf_count; j++) {		\
				up = HID_PAGE(hf->hf_usage[j]);		\
				u = HID_USAGE(hf->hf_usage[j]);		\
				printf("        USAGE %s\n",		\
				    usage_in_page(up, u));		\
			}						\
		} else {						\
			printf("[ARRAY]\n");				\
			printf("        USAGE [%u -> %u] ",		\
			    HID_USAGE(hf->hf_usage_min),		\
			    HID_USAGE(hf->hf_usage_max));		\
			printf("(%s)\n",				\
			    usage_page(HID_PAGE(hf->hf_usage_min)));	\
		}							\
	} while (0)
		

	STAILQ_FOREACH(ha, &hi->halist, ha_next) {
		up = HID_PAGE(ha->ha_usage);
		u = HID_USAGE(ha->ha_usage);
		printf("HID APPLICATION COLLECTION (%s) size(%d)\n",
		    usage_in_page(up, u), ha->ha_rsz);
		STAILQ_FOREACH(hr, &ha->ha_hrlist, hr_next) {
			printf("  HID REPORT: ID %d\n", hr->hr_id);
			for (i = 0; i < 3; i++) {
				if (!STAILQ_EMPTY(&hr->hr_hflist[i]))
					switch (i) {
					case HID_INPUT:
						printf("    INPUT: \n");
						break;
					case HID_OUTPUT:
						printf("    OUTPUT: \n");
						break;
					case HID_FEATURE:
						printf("    FEATURE: \n");
						break;
					}
				STAILQ_FOREACH(hf, &hr->hr_hflist[i], hf_next) {
					PRINT_FIELD;
				}
			}
		}
	}

#undef PRINT_FIELD
}

unsigned int
hid_appcol_get_usage(struct hid_appcol *ha)
{

	assert(ha != NULL);
	return (ha->ha_usage);
}

void
hid_appcol_set_private(struct hid_appcol *ha, void *data)
{

	assert(ha != NULL);
	ha->ha_data = data;
}

void *
hid_appcol_get_private(struct hid_appcol *ha)
{

	assert(ha != NULL);
	return (ha->ha_data);
}

struct hid_report *
hid_appcol_get_next_report(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_report *nhr;

	assert(ha != NULL);
	if (hr == NULL)
		nhr = STAILQ_FIRST(&ha->ha_hrlist);
	else
		nhr = STAILQ_NEXT(hr, hr_next);

	return (nhr);
}

static void
hid_appcol_recv_data(struct hid_appcol *ha, struct hid_report *hr, uint8_t *data,
    int len)
{
	struct hid_field *hf;
	int start, range, value, m, i, j, ndx;

	/* Discard data if no driver attached. */
	if (ha->ha_hd == NULL)
		return;

	assert(hr->hr_id == 0 || hr->hr_id == *data);

	/* Skip report id. */
	if (hr->hr_id != 0)
		data++;

	if (verbose > 1)
		printf("received data len(%d)\n", len);
	if (verbose > 2) {
		printf("received data: ");
		for (i = 0; i < len; i++)
			printf("0x%02x ", data[i]);
		printf("\n");
	}

	if (ha->ha_hd->hd_recv_raw != NULL)
		ha->ha_hd->hd_recv_raw(ha, data, len);

	if (ha->ha_hd->hd_recv == NULL)
		return;

	/*
	 * "Extract" data to each hid_field of this hid_report.
	 */
	STAILQ_FOREACH(hf, &hr->hr_hflist[HID_INPUT], hf_next) {
		for (i = 0; i < hf->hf_count; i++) {
			start = (hf->hf_pos + i * hf->hf_size) / 8;
			range = (hf->hf_pos + (i + 1) * hf->hf_size) / 8 -
			    start;
			value = 0;
			for (j = 0; j <= range; j++)
				value |= data[start + j] << (j * 8);
			value >>= (hf->hf_pos + i * hf->hf_size) % 8;
			value &= (1 << hf->hf_size) - 1;
			if (hf->hf_logic_min < 0) {
				/* Sign extend. */
				m = sizeof(value) * 8 - hf->hf_size;
				value = (value << m) >> m;
			}
			if (hf->hf_flags & HIO_VARIABLE) {
				hf->hf_usage[i] = hf->hf_nusage[i];
				hf->hf_value[i] = value;
			} else {
				/* Array. */
				if (value < hf->hf_logic_min ||
				    value > hf->hf_logic_max) {
					hf->hf_usage[i] = 0;
					hf->hf_value[i] = 0;
					continue;
				}
				ndx = value - hf->hf_logic_min;
				if (ndx >= 0 && ndx < MAXUSAGE) {
					hf->hf_usage[i] = hf->hf_nusage[ndx];
					if (value != 0) {
						hf->hf_value[i] = 1;
					} else
						hf->hf_value[i] = 0;
				}
			}
		}
		if (verbose > 1 && ((hf->hf_flags & HIO_CONST) == 0)) {
			printf("data: (%s)\n", hf->hf_flags & HIO_VARIABLE ?
			    "variable" : "array");
			for (i = 0; i < hf->hf_count; i++) {
				printf("  usage=%#x value=%d\n",
				    hf->hf_usage[i], hf->hf_value[i]);
			}
		}
	}

	/*
	 * Pass data to driver recv method.
	 */
	ha->ha_hd->hd_recv(ha, hr);
}

int
hid_report_get_id(struct hid_report *hr)
{

	assert(hr != NULL);

	return (hr->hr_id);
}

struct hid_field *
hid_report_get_next_field(struct hid_report *hr, struct hid_field *hf,
    enum hid_kind kind)
{
	struct hid_field *nhf;

	assert(hr != NULL);
	if (hf == NULL)
		nhf = STAILQ_FIRST(&hr->hr_hflist[kind]);
	else
		nhf = STAILQ_NEXT(hf, hf_next);

	return (nhf);
}

int
hid_field_get_flags(struct hid_field *hf)
{

	assert(hf != NULL);

	return (hf->hf_flags);
}

unsigned
hid_field_get_usage_page(struct hid_field *hf)
{

	assert(hf != NULL);

	return (hf->hf_usage_page >> 16);
}

int
hid_field_get_usage_count(struct hid_field *hf)
{

	assert(hf != NULL);

	return (hf->hf_count);
}

int
hid_field_get_usage_min(struct hid_field *hf)
{

	assert(hf != NULL);

	return (hf->hf_usage_min);
}

int
hid_field_get_usage_max(struct hid_field *hf)
{

	assert(hf != NULL);

	return (hf->hf_usage_max);
}

void
hid_field_get_usage_value(struct hid_field *hf, int i, unsigned int *usage,
    int *value)
{

	assert(hf != NULL);
	assert(i >= 0 && i < hf->hf_count);

	if (usage != NULL)
		*usage = hf->hf_usage[i];
	if (value != NULL)
		*value = hf->hf_value[i];
}

void
hid_driver_register(struct hid_driver *hd)
{
	struct hid_driver *nhd;

	nhd = malloc(sizeof(*nhd));
	assert(nhd != NULL);
	*nhd = *hd;

	STAILQ_INSERT_TAIL(&hdlist, nhd, hd_next);
}

#if 0
hid_data_t
hid_start_parse(struct hid_interface * p, int kindset)
{
	struct hid_data *s;

	s = calloc(1, sizeof(*s));
	if (s == NULL)
		err(1, "calloc");
	s->start = s->p = hi->rdesc;
	s->end = hi->rdesc + hi->rsz;
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
hid_report_size(struct hid_interface * p, enum hid_kind k, int id)
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
hid_locate(struct hid_interface * p, unsigned int u, enum hid_kind k, hid_item_t *h)
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

int
hid_get_array8(const void *p, uint8_t *r, const hid_item_t *h)
{
	const uint8_t *buf;
	unsigned int hpos;
	unsigned int hsize;
	int i, offs;

	buf = p;
	if (h->report_ID > 0) {
		if (h->report_ID != *buf)
			return (0);
		buf++;
	}
	if (h->report_size != 8)
		return (0);

	hpos = h->pos;
	hsize = h->report_count; /* byte length of data */

	if (hsize == 0 || hpos % 8 != 0)
		return (0);

	offs = hpos / 8;

	for (i = 0; (unsigned int)i < hsize; i++)
		r[i] = buf[offs + i];

	return (hsize);
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
#endif
