/*-
 * Copyright (c) 2009 Kai Wang
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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "uhidd.h"

#define	HUG_CONSUMER_CONTROL		0x0001
#define	HUG_MENU			0x0040
#define	HUG_RECALL_LAST			0x0083
#define	HUG_MEDIA_SEL_PC		0x0088
#define	HUG_MEDIA_SEL_TV		0x0089
#define	HUG_MEDIA_SEL_WWW		0x008A
#define	HUG_MEDIA_SEL_DVD		0x008B
#define	HUG_MEDIA_SEL_PHONE		0x008C
#define	HUG_MEDIA_SEL_PROGRAM		0x008D
#define	HUG_MEDIA_SEL_VIDEOPHONE	0x008E
#define	HUG_MEDIA_SEL_GAMES		0x008F
#define	HUG_MEDIA_SEL_MEMO		0x0090
#define	HUG_MEDIA_SEL_CD		0x0091
#define	HUG_MEDIA_SEL_VCR		0x0092
#define	HUG_MEDIA_SEL_TUNER		0x0093
#define	HUG_QUIT			0x0094
#define	HUG_HELP			0x0095
#define	HUG_MEDIA_SEL_TAPE		0x0096
#define	HUG_MEDIA_SEL_CABLE		0x0097
#define	HUG_MEDIA_SEL_SATELLITE		0x0098
#define	HUG_MEDIA_SEL_HOME		0x009A
#define	HUG_CHANNEL_UP			0x009C
#define	HUG_CHANNEL_DOWN		0x009D
#define	HUG_VCR_PLUS			0x00A0
#define	HUG_PLAY			0x00B0
#define	HUG_PAUSE			0x00B1
#define	HUG_RECORD			0x00B2
#define	HUG_FAST_FORWARD		0x00B3
#define	HUG_REWIND			0x00B4
#define	HUG_NEXT_TRACK			0x00B5
#define	HUG_PREVIOUS_TRACK		0x00B6
#define	HUG_STOP			0x00B7
#define	HUG_EJECT			0x00B8
#define	HUG_RANDOM_PLAY			0x00B9
#define	HUG_REPEAT			0x00BC
#define	HUG_PLAYPAUSE			0x00CD
#define	HUG_VOLUME			0x00E0
#define	HUG_BALANCE			0x00E1
#define	HUG_MUTE			0x00E2
#define	HUG_BASE			0x00E3
#define	HUG_TREBLE			0x00E4
#define	HUG_BASE_BOOST			0x00E5
#define	HUG_VOLUME_UP			0x00E9
#define	HUG_VOLUME_DOWN			0x00EA
#define	HUG_AL_WORDPROCESSOR		0x0184
#define	HUG_AL_TEXTEDITOR		0x0185
#define	HUG_AL_SPREADSHEET		0x0186
#define	HUG_AL_GRAPHICSEDITOR		0x0187
#define	HUG_AL_PRESENTATION		0x0188
#define	HUG_AL_DATABASE			0x0189
#define	HUG_AL_EMAIL			0x018A
#define	HUG_AL_NEWS			0x018B
#define	HUG_AL_VOICEMAIL		0x018C
#define	HUG_AL_ADDRESSBOOK		0x018D
#define	HUG_AL_CALENDAR			0x018E
#define	HUG_AL_PROJECT			0x018F
#define	HUG_AL_JOURNAL			0x0190
#define	HUG_AL_FINANCE			0x0191
#define	HUG_AL_CALCULATOR		0x0192
#define	HUG_AL_CAPTURE			0x0193
#define	HUG_AL_FILEBROWSER		0x0194
#define	HUG_AL_LANBROWSER		0x0195
#define	HUG_AL_INTERNETBROWSER		0x0196
#define	HUG_AL_LOGOFF			0x019C
#define	HUG_AL_TERMINALLOCK		0x019E
#define	HUG_AL_HELP			0x01A6
#define	HUG_AL_DOCUMENTS		0x01A7
#define	HUG_AL_SPELLCHECK		0x01AB
#define	HUG_AL_IMAGE_BROWSER		0x01B6
#define	HUG_AL_SOUND_BROWSER		0x01B7
#define	HUG_AL_MESSENGER		0x01BC
#define	HUG_AC_NEW			0x0201
#define	HUG_AC_OPEN			0x0202
#define	HUG_AC_CLOSE			0x0203
#define	HUG_AC_EXIT			0x0204
#define	HUG_AC_MAXIMIZE			0x0205
#define	HUG_AC_MINIMIZE			0x0206
#define	HUG_AC_SAVE			0x0207
#define	HUG_AC_PRINT			0x0208
#define	HUG_AC_PROPERTIES		0x0209
#define	HUG_AC_UNDO			0x021A
#define	HUG_AC_COPY			0x021B
#define	HUG_AC_CUT			0x021C
#define	HUG_AC_PASTE			0x021D
#define	HUG_AC_FIND			0x021F
#define	HUG_AC_SEARCH			0x0221
#define	HUG_AC_GOTO			0x0222
#define	HUG_AC_HOME			0x0223
#define	HUG_AC_BACK			0x0224
#define	HUG_AC_FORWARD			0x0225
#define	HUG_AC_STOP			0x0226
#define	HUG_AC_REFRESH			0x0227
#define	HUG_AC_BOOKMARKS		0x022A
#define	HUG_AC_HISTORY			0x022B
#define	HUG_AC_ZOOMIN			0x022D
#define	HUG_AC_ZOOMOUT			0x022E
#define	HUG_AC_ZOOM			0x022F
#define	HUG_AC_SCROLL_UP		0x0233
#define	HUG_AC_SCROLL_DOWN		0x0234
#define	HUG_AC_PAN			0x0238
#define	HUG_AC_CANCEL			0x025F
#define	HUG_AC_REDO			0x0279
#define	HUG_AC_REPLY			0x0289
#define	HUG_AC_FORWARDMSG		0x028B
#define	HUG_AC_SEND			0x028C

#if 0
static uint8_t free_keys[] =
{
	0x54, 0x55, 0x5A, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x6F, 0x71, 0x72, 0x74,
	0x75, 0x7A, 0x7C, 0x7F
};
#endif

/* Free KEY: 0x54 0x5A 0x5F 0x60 0x62 */

static int
cc_tr(int hid_key)
{

	/*
	 * A pre-defined key translation table.
	 */

	switch (hid_key) {
	case HUG_VOLUME_UP:
		return (0x5A);
	case HUG_VOLUME_DOWN:
		return (0x5F);
	case HUG_PLAYPAUSE:
		return (0x60);
	case HUG_STOP:
		return (0x62);
	case HUG_MUTE:
		return (0x63);
	case HUG_PREVIOUS_TRACK:
		return (0x6F);
	case HUG_NEXT_TRACK:
		return (0x71);
	default:
		return (-1);
	}
}

int
cc_match(struct hid_appcol *ha)
{
	struct hid_parent *hp;
	struct hid_report *hr;
	struct hid_field *hf;
	unsigned int u, up;

	hp = hid_appcol_get_interface_private(ha);
	assert(hp != NULL);

	if (!config_attach_cc(hp))
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

	if (kbd_attach(ha) < 0)
		return (-1);
	kbd_set_tr(ha, cc_tr);

	return (0);
}

#define MAX_KEYCODE 256

void
cc_recv(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_parent *hp;
	struct hid_field *hf;
	unsigned int usage, up;
	int i, value, cnt, flags, total;
	uint16_t keycodes[MAX_KEYCODE];

	hp = hid_appcol_get_interface_private(ha);
	assert(hp != NULL);

	total = 0;
	cnt = 0;
	memset(keycodes, 0, sizeof(keycodes));

	hf = NULL;
	while ((hf = hid_report_get_next_field(hr, hf, HID_INPUT)) != NULL) {
		flags = hid_field_get_flags(hf);
		if (flags & HIO_CONST)
			continue;
		for (i = 0; i < hf->hf_count; i++) {
			up = hid_field_get_usage_page(hf);
			if (up != HUP_CONSUMER)
				continue;
			hid_field_get_usage_value(hf, i, &usage, &value);
			if (total >= MAX_KEYCODE)
				continue;
			total++;
			if (value) {
				if (cnt >= MAX_KEYCODE)
					continue;
				keycodes[cnt++] = HID_USAGE(usage);
			}
		}
	}

	if (total > 0 && verbose) {
		PRINT1("hid coeds: ");
		if (cnt == 0)
			printf("none");
		for (i = 0; i < cnt; i++)
			printf("0x%02X ", keycodes[i]);
		putchar('\n');
		kbd_input(ha, 0, keycodes, total);
	}
}
