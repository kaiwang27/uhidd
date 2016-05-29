/*-
 * Copyright (c) 2009, 2010, 2012 Kai Wang
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Generic consumer control device (multimedia keys) support.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "uhidd.h"

static uint8_t free_key[] =
{
	/* Free Keys. */
	0x54, 0x5A, 0x5F, 0x60, 0x62, 0x63, 0x6F, 0x71, 0x72, 0x74,
	0x75, 0x7A, 0x7C, 0x7F,

	/* F13 - F24 */
	0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
	0x6E, 0x76,

	/* Keyboard International 1-6 */
	0x73, 0x70, 0x7D, 0x79, 0x7B, 0x5C,

	/* Keyboard LANG 1-5 */
	0xF2, 0xF1, 0x78, 0x77, 0x76
};

#define	_FREE_KEY_COUNT	((int)(sizeof(free_key)/sizeof(free_key[0])))

static void
cc_write_keymap_file(struct hid_interface *hi)
{
	char fpath[256];
	FILE *fp;
	int i;

	snprintf(fpath, sizeof(fpath), "/var/run/uhidd.%s/cc_keymap",
	    hi->dev);
	fp = fopen(fpath, "w+");
	if (fp == NULL) {
		syslog(LOG_ERR, "%s[%d] fopen %s failed: %m",
		    hi->dev, hi->ndx, fpath);
		return;
	}
	fprintf(fp, "0x%04x:0x%04x={\n", hi->vendor_id, hi->product_id);
	fprintf(fp, "\tcc_keymap={\n");
	for (i = 0; i < usage_consumer_num && i < _MAX_MM_KEY; i++) {
		if (hi->cc_keymap[i]) {
			if (!strcasecmp(usage_consumer[i], "Reserved"))
				fprintf(fp, "\t\t0x%X=", (unsigned) i);
			else
				fprintf(fp, "\t\t%s=", usage_consumer[i]);
			fprintf(fp, "\"0x%02X\"\n", hi->cc_keymap[i]);
		}
	}
	fprintf(fp, "\t}\n}\n");
	fclose(fp);
}

static int
cc_tr(struct hid_appcol *ha, struct hid_key hk, int make,
    struct hid_scancode *c, int len)
{
	struct hid_interface *hi;
	struct device_config *dconfig;

	assert(c != NULL && len > 0);

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	/*
	 * Some consumer controls report regular(HUP_KEYBOARD) keyboard
	 * keys.
	 */
	if (hk.up == HUP_KEYBOARD)
		return (kbd_hid2key(ha, hk, make, c, len));

	if (hk.up != HUP_CONSUMER)
		return (0);

	(*c).make = make;

	/*
	 * Check if there is a user provided keymap.
	 */
	dconfig = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dconfig != NULL && dconfig->cc_keymap_set) {
		if (dconfig->cc_keymap[hk.code] != 0) {
			(*c).sc = dconfig->cc_keymap[hk.code];
			return (1);
		} else
			return (0);
	}
	if (uconfig.gconfig.cc_keymap_set) {
		if (uconfig.gconfig.cc_keymap[hk.code] != 0) {
			(*c).sc = uconfig.gconfig.cc_keymap[hk.code];
			return (1);
		} else
			return (0);
	}

	/*
	 * Check if there is a key translation in the in-memory keymap.
	 */
	if (hi->cc_keymap[hk.code] != 0) {
		(*c).sc = hi->cc_keymap[hk.code];
		return (1);
	}

	/*
	 * Try allocating a free key for this "HID key".
	 */
	if (hi->free_key_pos < _FREE_KEY_COUNT) {
		hi->cc_keymap[hk.code] = free_key[hi->free_key_pos];
		hi->free_key_pos++;
		cc_write_keymap_file(hi);
		PRINT1(1, "remembered new hid key map: 0x%x => 0x%02x\n",
		    hk.code, hi->cc_keymap[hk.code]);
		(*c).sc = hi->cc_keymap[hk.code];
		return (1);
	} else {
		PRINT1(1, "no more free key for hid key: 0x%x\n", hk.code);
		return (0);
	}
}

int
cc_match(struct hid_appcol *ha)
{
	struct hid_interface *hi;
	struct hid_report *hr;
	struct hid_field *hf;
	unsigned int u, up;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if (config_cc_attach(hi) <= ATTACH_NO)
		return (HID_MATCH_NONE);

	u = hid_appcol_get_usage(ha);
	if (u != HID_USAGE2(HUP_CONSUMER, HUG_CONSUMER_CONTROL))
		return (HID_MATCH_NONE);

	hr = NULL;
	while ((hr = hid_appcol_get_next_report(ha, hr)) != NULL) {
		hf = NULL;
		while ((hf = hid_report_get_next_field(hr, hf, HID_INPUT)) !=
		    NULL) {
			up = hid_field_get_usage_page(hf);
			if (up == HUP_CONSUMER)
				return (HID_MATCH_GENERAL);
		}
	}

	return (HID_MATCH_NONE);
}

int
cc_attach(struct hid_appcol *ha)
{
	struct hid_interface *hi;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if (kbd_attach(ha) < 0)
		return (-1);
	kbd_set_tr(ha, cc_tr);

	return (0);
}

#define MAX_KEYCODE 256

static void
cc_process_volume_usage(struct hid_appcol *ha, struct hid_report *hr, int value)
{
	struct hid_interface *hi;
	struct hid_field *hf;
	int i, flags, total;
	struct hid_key keycodes[MAX_KEYCODE];
	uint16_t key;

	/*
	 * HUG_VOLUME has Usage Type LC (linear control). Usually it has
	 * value range [-Min, Max]. Positive value n increments the volume
	 * by n. Negative value -n decrements the volumn by n. To fit in
	 * our key press/release model, HUG_VOLUME is simulated by
	 * pressing/releasing HUG_VOLUME_UP or HUG_VLOLUME_DOWN n times.
	 */

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	/* Do nothing if value is 0. */
	if (value == 0)
		return;

	/* The keyboard driver needs to know the total number of keys. */
	total = 0;
	hf = NULL;
	while ((hf = hid_report_get_next_field(hr, hf, HID_INPUT)) != NULL) {
		flags = hid_field_get_flags(hf);
		if (flags & HIO_CONST)
			continue;
		for (i = 0; i < hf->hf_count; i++)
			total++;
	}
	if (total >= MAX_KEYCODE)
		return;

	if (value < 0)
		key = HUG_VOLUME_DOWN;
	else
		key = HUG_VOLUME_UP;
	value = abs(value);

	memset(keycodes, 0, sizeof(keycodes));
	for (i = 0; i < value; i++) {
		/* Key press. */
		keycodes[0].code = key;
		keycodes[0].up = HUP_CONSUMER;
		kbd_input(ha, 0, keycodes, total);
		PRINT1(1, "hid codes: 0x%02X (HUG_VOLUME)\n",
		    keycodes[0].code);
		/* Key release. */
		keycodes[0].code = 0;
		kbd_input(ha, 0, keycodes, total);
		PRINT1(2, "hid codes: none (HUG_VOLUME)\n");
	}
}

static void
cc_process_key(struct hid_appcol *ha, struct hid_report *hr, unsigned usage,
    int value, int *total, int *cnt, struct hid_key *keycodes)
{

	/* Skip the keys this driver can't handle. */
	if (HID_PAGE(usage) != HUP_CONSUMER && HID_PAGE(usage) != HUP_KEYBOARD)
		return;

	/* "total" counts all keys, pressed and released. */
	if (*total >= MAX_KEYCODE)
		return;
	(*total)++;

	if (HID_PAGE(usage) == HUP_CONSUMER &&
	    HID_USAGE(usage) == HUG_VOLUME && value) {
		cc_process_volume_usage(ha, hr, value);
		return;
	}

	if (value) {
		/* "cnt" counts pressed keys. */
		if (*cnt >= MAX_KEYCODE)
			return;
		keycodes[*cnt].code = HID_USAGE(usage);
		keycodes[*cnt].up = HID_PAGE(usage);
		(*cnt)++;
	}
}

void
cc_recv(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_interface *hi;
	struct hid_field *hf;
	struct hid_key keycodes[MAX_KEYCODE];
	unsigned usage, rusage[8];
	int i, j, value, cnt, flags, total, r, len;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	total = 0;
	cnt = 0;
	memset(keycodes, 0, sizeof(keycodes));

	hf = NULL;
	while ((hf = hid_report_get_next_field(hr, hf, HID_INPUT)) != NULL) {
		flags = hid_field_get_flags(hf);
		if (flags & HIO_CONST)
			continue;
		for (i = 0; i < hf->hf_count; i++) {
			hid_field_get_usage_value(hf, i, &usage, &value);

			/* Driver-specific consumer control key filter. */
			if (value && hi->cc_recv_filter != NULL) {
				len = sizeof(rusage);
				r = hi->cc_recv_filter(ha, usage, value,
				    rusage, &len);
				if (r == HID_FILTER_DISCARD)
					continue;
				if (r == HID_FILTER_REPLACE) {
					assert(len > 0);
					for (j = 0; j < len; j++)
						cc_process_key(ha, hr,
						    rusage[j], 1, &total,
						    &cnt, keycodes);
					continue;
				}
			}

			cc_process_key(ha, hr, usage, value, &total, &cnt,
			    keycodes);
		}
	}

	if (total > 0 && verbose > 1) {
		PRINT1(2, "hid codes: ");
		if (cnt == 0)
			printf("none");
		for (i = 0; i < cnt; i++)
			printf("0x%02X(0x%02X) ", keycodes[i].code,
			    keycodes[i].up);
		putchar('\n');
	}

	kbd_input(ha, 0, keycodes, total);
}
