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

static STAILQ_HEAD(, hid_driver) hdlist = STAILQ_HEAD_INITIALIZER(hdlist);

struct hid_interface *
hid_interface_alloc(unsigned char *rdesc, int rsz, void *data)
{
	struct hid_interface *hi;

	hi = calloc(1, sizeof(*hi));
	if (hi == NULL)
		err(1, "calloc");
	memcpy(hi->rdesc, rdesc, rsz);
	hi->rsz = rsz;
	hi->hi_data = data;
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

void
hid_interface_output_data(struct hid_interface *hi, int report_id, char *data,
    int len)
{

	if (hi->hi_write_callback == NULL)
		return;

	hi->hi_write_callback(hi->hi_data, report_id, data, len);
}

void
hid_interface_set_private(struct hid_interface *hi, void *data)
{

	assert(hi != NULL);
	hi->hi_data = data;
}

void *
hid_interface_get_private(struct hid_interface *hi)
{

	assert(hi != NULL);
	return (hi->hi_data);
}

void
hid_interface_set_write_callback(struct hid_interface *hi,
    int (*write_callback)(void *, int, char *, int)) {

	assert(hi != NULL);
	hi->hi_write_callback = write_callback;
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

	ha->ha_hi = hi;
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
	for (i = 0; i < nusage; i++) {
		hf->hf_nusage[i] = usages[i];
		if (hf->hf_flags & HIO_VARIABLE)
			hf->hf_usage[i] = hf->hf_nusage[i];
	}
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
				up = HID_PAGE(hf->hf_nusage[j]);	\
				u = HID_USAGE(hf->hf_nusage[j]);	\
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
hid_appcol_get_interface_private(struct hid_appcol *ha)
{

	assert(ha != NULL && ha->ha_hi != NULL);
	return (ha->ha_hi->hi_data);
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

void
hid_appcol_recv_data(struct hid_appcol *ha, struct hid_report *hr, uint8_t *data,
    int len)
{
	struct hid_field *hf;
	int start, range, value, m, i, j, ndx;

	/* Discard data if no driver attached. */
	if (ha->ha_hd == NULL)
		return;

	assert(hr->hr_id == 0 || hr->hr_id == *data);

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

	/* Skip report id. */
	if (hr->hr_id != 0)
		data++;

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

void
hid_appcol_xfer_data(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_field *hf;
	char buf[4096];
	int data, i, j, end, off, mask, pos, size, total;

	total = 0;

	STAILQ_FOREACH(hf, &hr->hr_hflist[HID_OUTPUT], hf_next) {
		for (i = 0; i < hf->hf_count; i++) {
			if (hf->hf_flags & HIO_CONST)
				data = 0;
			else
				data = hf->hf_value[i];
			pos = hf->hf_pos + i * hf->hf_size;
			size = hf->hf_size;
			if (size != 32) {
				mask = (1 << size) - 1;
				data &= mask;
			} else
				mask = ~0;
			data <<= (pos % 8);
			mask <<= (pos % 8);
			mask = ~mask;
			off = pos / 8;
			end = (pos + size) / 8 - off;
			for (j = 0; j <= end; j++)
				buf[off + j] = (buf[off + j] & (mask >> (j*8)))
				    | ((data >> (j*8)) & 0xff);
		}
		total += hf->hf_count * hf->hf_size;
	}
	total = (total + 7) / 8;

	hid_interface_output_data(ha->ha_hi, hr->hr_id, buf, total);
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
hid_field_set_value(struct hid_field *hf, int i, int value)
{

	assert(hf != NULL);
	assert(i >= 0 && i < hf->hf_count);
	hf->hf_value[i] = value;
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
static void
repair_report_desc(struct hid_child *hc)
{
	struct hid_parent *hp;
	struct hid_child *phc;
	unsigned int bTag, bType, bSize;
	unsigned char *b, *pos;
	hid_item_t env;
	int bytes, i;

	hp = hc->parent;
	assert(hp != NULL);

	/* First child does not need repairing. */
	phc = STAILQ_FIRST(&hp->hclist);
	if (phc == hc)
		return;

	while (STAILQ_NEXT(phc, next) != hc)
		phc = STAILQ_NEXT(phc, next);
	env = phc->env;

	/* First step: insert USAGE PAGE before USAGE if need. */
	b = hc->rdesc;
	while (b < hc->rdesc + hc->rsz) {
		pos = b;
		bSize = *b++;
		if (bSize == 0xfe) {
			/* long item */
			bSize = *b++;
			bTag = *b++;
			b += bSize;
			continue;
		}

		/* short item */
		bTag = bSize >> 4;
		bType = (bSize >> 2) & 3;
		bSize &= 3;
		if (bSize == 3)
			bSize = 4;
		b += bSize;

		/* If we found a USAGE PAGE, no need to continue. */
		if (bType == 1 && bTag == 0)
			break;

		/* Check if it is USAGE item. */
		if (bType == 2 && bTag == 0) {

			/*
			 * We need to insert USAGE PAGE before this
			 * USAGE. USAGE PAGE needs 3-byte space.
			 */
			if (env._usage_page < 256)
				bytes = 2;
			else
				bytes = 3;
			memmove(pos + bytes, pos, hc->rsz - (pos - hc->rdesc));
			pos[0] = (1 << 2) | (bytes - 1);
			pos[1] = env._usage_page & 0xff;
			if (bytes == 3)
				pos[2] = (env._usage_page & 0xff00) >> 8;
			hc->rsz += bytes;
			if (verbose > 1) {
				printf("\tnr=%d repair: insert USAGE PAGE",
				    hc->nr);
				for (i = 0; i < bytes; i++)
					printf(" 0x%02x", pos[i]);
				putchar('\n');
			}
			break;
		}

	}

	/*
	 * Second step: insert missing REPORT COUNT before the first main
	 * item.
	 */
	b = hc->rdesc;
	while (b < hc->rdesc + hc->rsz) {
		pos = b;
		bSize = *b++;
		if (bSize == 0xfe) {
			/* long item */
			bSize = *b++;
			bTag = *b++;
			b += bSize;
			continue;
		}

		/* short item */
		bTag = bSize >> 4;
		bType = (bSize >> 2) & 3;
		bSize &= 3;
		if (bSize == 3)
			bSize = 4;
		b += bSize;

		/* Check if we already got REPORT COUNT. */
		if (bType == 1 && bTag == 9)
			break;

		/* Check if it is INPUT, OUTPUT or FEATURE. */
		if (bType == 0 && (bTag == 8 || bTag == 9 || bTag == 11)) {
			if (env.report_count < 256)
				bytes = 2;
			else if (env.report_count < 65536)
				bytes = 3;
			else
				bytes = 5;
			memmove(pos + bytes, pos, hc->rsz - (pos - hc->rdesc));
			if (bytes < 5)
				pos[0] = (9 << 4) | (1 << 2) | (bytes - 1);
			else
				pos[0] = (9 << 4) | (1 << 2) | 3;
			pos[1] = env.report_count & 0xff;
			if (bytes > 2)
				pos[2] = (env.report_count & 0xff00) >> 8;
			if (bytes > 3) {
				pos[3] = (env.report_count & 0xff0000) >> 16;
				pos[4] = env.report_count >> 24;
			}
			hc->rsz += bytes;
			if (verbose > 1) {
				printf("\tnr=%d repair: insert REPORT COUNT",
				    hc->nr);
				for (i = 0; i < bytes; i++)
					printf(" 0x%02x", pos[i]);
				putchar('\n');
			}
			break;
		}
	}
}
#endif
