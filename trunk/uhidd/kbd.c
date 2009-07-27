/*-
 * Copyright (c) 2009 Kai Wang
 * All rights reserved.
 * Copyright (c) 2006 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD $");

#include <sys/param.h>
#include <sys/stat.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define	KBD	hc->u.kd

/*
 * HID code to PS/2 set 1 code translation table.
 *
 * http://www.microsoft.com/whdc/device/input/Scancode.mspx
 *
 * The table only contains "make" (key pressed) codes.
 * The "break" (key released) code is generated as "make" | 0x80
 */

#define E0PREFIX	(1 << 31)
#define NOBREAK		(1 << 30)
#define CODEMASK	(~(E0PREFIX|NOBREAK))

static int32_t const	x[] =
{
/*==================================================*/
/* Name                   HID code    Make     Break*/
/*==================================================*/
/* No Event                     00 */ -1,   /* None */
/* Overrun Error                01 */ NOBREAK|0xFF, /* None */
/* POST Fail                    02 */ NOBREAK|0xFC, /* None */
/* ErrorUndefined               03 */ -1,   /* Unassigned */
/* a A                          04 */ 0x1E, /* 9E */
/* b B                          05 */ 0x30, /* B0 */
/* c C                          06 */ 0x2E, /* AE */
/* d D                          07 */ 0x20, /* A0 */
/* e E                          08 */ 0x12, /* 92 */
/* f F                          09 */ 0x21, /* A1 */
/* g G                          0A */ 0x22, /* A2 */
/* h H                          0B */ 0x23, /* A3 */
/* i I                          0C */ 0x17, /* 97 */
/* j J                          0D */ 0x24, /* A4 */
/* k K                          0E */ 0x25, /* A5 */
/* l L                          0F */ 0x26, /* A6 */
/* m M                          10 */ 0x32, /* B2 */
/* n N                          11 */ 0x31, /* B1 */
/* o O                          12 */ 0x18, /* 98 */
/* p P                          13 */ 0x19, /* 99 */
/* q Q                          14 */ 0x10, /* 90 */
/* r R                          15 */ 0x13, /* 93 */
/* s S                          16 */ 0x1F, /* 9F */
/* t T                          17 */ 0x14, /* 94 */
/* u U                          18 */ 0x16, /* 96 */
/* v V                          19 */ 0x2F, /* AF */
/* w W                          1A */ 0x11, /* 91 */
/* x X                          1B */ 0x2D, /* AD */
/* y Y                          1C */ 0x15, /* 95 */
/* z Z                          1D */ 0x2C, /* AC */
/* 1 !                          1E */ 0x02, /* 82 */
/* 2 @                          1F */ 0x03, /* 83 */
/* 3 #                          20 */ 0x04, /* 84 */
/* 4 $                          21 */ 0x05, /* 85 */
/* 5 %                          22 */ 0x06, /* 86 */
/* 6 ^                          23 */ 0x07, /* 87 */
/* 7 &                          24 */ 0x08, /* 88 */
/* 8 *                          25 */ 0x09, /* 89 */
/* 9 (                          26 */ 0x0A, /* 8A */
/* 0 )                          27 */ 0x0B, /* 8B */
/* Return                       28 */ 0x1C, /* 9C */
/* Escape                       29 */ 0x01, /* 81 */
/* Backspace                    2A */ 0x0E, /* 8E */
/* Tab                          2B */ 0x0F, /* 8F */
/* Space                        2C */ 0x39, /* B9 */
/* - _                          2D */ 0x0C, /* 8C */
/* = +                          2E */ 0x0D, /* 8D */
/* [ {                          2F */ 0x1A, /* 9A */
/* ] }                          30 */ 0x1B, /* 9B */
/* \ |                          31 */ 0x2B, /* AB */
/* Europe 1                     32 */ 0x2B, /* AB */
/* ; :                          33 */ 0x27, /* A7 */
/* " '                          34 */ 0x28, /* A8 */
/* ` ~                          35 */ 0x29, /* A9 */
/* comma <                      36 */ 0x33, /* B3 */
/* . >                          37 */ 0x34, /* B4 */
/* / ?                          38 */ 0x35, /* B5 */
/* Caps Lock                    39 */ 0x3A, /* BA */
/* F1                           3A */ 0x3B, /* BB */
/* F2                           3B */ 0x3C, /* BC */
/* F3                           3C */ 0x3D, /* BD */
/* F4                           3D */ 0x3E, /* BE */
/* F5                           3E */ 0x3F, /* BF */
/* F6                           3F */ 0x40, /* C0 */
/* F7                           40 */ 0x41, /* C1 */
/* F8                           41 */ 0x42, /* C2 */
/* F9                           42 */ 0x43, /* C3 */
/* F10                          43 */ 0x44, /* C4 */
/* F11                          44 */ 0x57, /* D7 */
/* F12                          45 */ 0x58, /* D8 */
/* Print Screen                 46 */ E0PREFIX|0x37, /* E0 B7 */
/* Scroll Lock                  47 */ 0x46, /* C6 */
#if 0
/* Break (Ctrl-Pause)           48 */ E0 46 E0 C6, /* None */
/* Pause                        48 */ E1 1D 45 E1 9D C5, /* None */
#else
/* Break (Ctrl-Pause)/Pause     48 */ NOBREAK /* Special case */, /* None */
#endif
/* Insert                       49 */ E0PREFIX|0x52, /* E0 D2 */
/* Home                         4A */ E0PREFIX|0x47, /* E0 C7 */
/* Page Up                      4B */ E0PREFIX|0x49, /* E0 C9 */
/* Delete                       4C */ E0PREFIX|0x53, /* E0 D3 */
/* End                          4D */ E0PREFIX|0x4F, /* E0 CF */
/* Page Down                    4E */ E0PREFIX|0x51, /* E0 D1 */
/* Right Arrow                  4F */ E0PREFIX|0x4D, /* E0 CD */
/* Left Arrow                   50 */ E0PREFIX|0x4B, /* E0 CB */
/* Down Arrow                   51 */ E0PREFIX|0x50, /* E0 D0 */
/* Up Arrow                     52 */ E0PREFIX|0x48, /* E0 C8 */
/* Num Lock                     53 */ 0x45, /* C5 */
/* Keypad /                     54 */ E0PREFIX|0x35, /* E0 B5 */
/* Keypad *                     55 */ 0x37, /* B7 */
/* Keypad -                     56 */ 0x4A, /* CA */
/* Keypad +                     57 */ 0x4E, /* CE */
/* Keypad Enter                 58 */ E0PREFIX|0x1C, /* E0 9C */
/* Keypad 1 End                 59 */ 0x4F, /* CF */
/* Keypad 2 Down                5A */ 0x50, /* D0 */
/* Keypad 3 PageDn              5B */ 0x51, /* D1 */
/* Keypad 4 Left                5C */ 0x4B, /* CB */
/* Keypad 5                     5D */ 0x4C, /* CC */
/* Keypad 6 Right               5E */ 0x4D, /* CD */
/* Keypad 7 Home                5F */ 0x47, /* C7 */
/* Keypad 8 Up                  60 */ 0x48, /* C8 */
/* Keypad 9 PageUp              61 */ 0x49, /* C9 */
/* Keypad 0 Insert              62 */ 0x52, /* D2 */
/* Keypad . Delete              63 */ 0x53, /* D3 */
/* Europe 2                     64 */ 0x56, /* D6 */
/* App                          65 */ E0PREFIX|0x5D, /* E0 DD */
/* Keyboard Power               66 */ E0PREFIX|0x5E, /* E0 DE */
/* Keypad =                     67 */ 0x59, /* D9 */
/* F13                          68 */ 0x64, /* E4 */
/* F14                          69 */ 0x65, /* E5 */
/* F15                          6A */ 0x66, /* E6 */
/* F16                          6B */ 0x67, /* E7 */
/* F17                          6C */ 0x68, /* E8 */
/* F18                          6D */ 0x69, /* E9 */
/* F19                          6E */ 0x6A, /* EA */
/* F20                          6F */ 0x6B, /* EB */
/* F21                          70 */ 0x6C, /* EC */
/* F22                          71 */ 0x6D, /* ED */
/* F23                          72 */ 0x6E, /* EE */
/* F24                          73 */ 0x76, /* F6 */
/* Keyboard Execute             74 */ -1,   /* Unassigned */
/* Keyboard Help                75 */ -1,   /* Unassigned */
/* Keyboard Menu                76 */ -1,   /* Unassigned */
/* Keyboard Select              77 */ -1,   /* Unassigned */
/* Keyboard Stop                78 */ -1,   /* Unassigned */
/* Keyboard Again               79 */ -1,   /* Unassigned */
/* Keyboard Undo                7A */ -1,   /* Unassigned */
/* Keyboard Cut                 7B */ -1,   /* Unassigned */
/* Keyboard Copy                7C */ -1,   /* Unassigned */
/* Keyboard Paste               7D */ -1,   /* Unassigned */
/* Keyboard Find                7E */ -1,   /* Unassigned */
/* Keyboard Mute                7F */ -1,   /* Unassigned */
/* Keyboard Volume Up           80 */ -1,   /* Unassigned */
/* Keyboard Volume Dn           81 */ -1,   /* Unassigned */
/* Keyboard Locking Caps Lock   82 */ -1,   /* Unassigned */
/* Keyboard Locking Num Lock    83 */ -1,   /* Unassigned */
/* Keyboard Locking Scroll Lock 84 */ -1,   /* Unassigned */
/* Keypad comma                 85 */ 0x7E, /* FE */
/* Keyboard Equal Sign          86 */ -1,   /* Unassigned */
/* Keyboard Int'l 1             87 */ 0x73, /* F3 */
/* Keyboard Int'l 2             88 */ 0x70, /* F0 */
/* Keyboard Int'l 2             89 */ 0x7D, /* FD */
/* Keyboard Int'l 4             8A */ 0x79, /* F9 */
/* Keyboard Int'l 5             8B */ 0x7B, /* FB */
/* Keyboard Int'l 6             8C */ 0x5C, /* DC */
/* Keyboard Int'l 7             8D */ -1,   /* Unassigned */
/* Keyboard Int'l 8             8E */ -1,   /* Unassigned */
/* Keyboard Int'l 9             8F */ -1,   /* Unassigned */
/* Keyboard Lang 1              90 */ NOBREAK|0xF2, /* None */
/* Keyboard Lang 2              91 */ NOBREAK|0xF1, /* None */
/* Keyboard Lang 3              92 */ 0x78, /* F8 */
/* Keyboard Lang 4              93 */ 0x77, /* F7 */
/* Keyboard Lang 5              94 */ 0x76, /* F6 */
/* Keyboard Lang 6              95 */ -1,   /* Unassigned */
/* Keyboard Lang 7              96 */ -1,   /* Unassigned */
/* Keyboard Lang 8              97 */ -1,   /* Unassigned */
/* Keyboard Lang 9              98 */ -1,   /* Unassigned */
/* Keyboard Alternate Erase     99 */ -1,   /* Unassigned */
/* Keyboard SysReq/Attention    9A */ -1,   /* Unassigned */
/* Keyboard Cancel              9B */ -1,   /* Unassigned */
/* Keyboard Clear               9C */ -1,   /* Unassigned */
/* Keyboard Prior               9D */ -1,   /* Unassigned */
/* Keyboard Return              9E */ -1,   /* Unassigned */
/* Keyboard Separator           9F */ -1,   /* Unassigned */
/* Keyboard Out                 A0 */ -1,   /* Unassigned */
/* Keyboard Oper                A1 */ -1,   /* Unassigned */
/* Keyboard Clear/Again         A2 */ -1,   /* Unassigned */
/* Keyboard CrSel/Props         A3 */ -1,   /* Unassigned */
/* Keyboard ExSel               A4 */ -1,   /* Unassigned */
/* Reserved                     A5 */ -1,   /* Reserved */
/* Reserved                     A6 */ -1,   /* Reserved */
/* Reserved                     A7 */ -1,   /* Reserved */
/* Reserved                     A8 */ -1,   /* Reserved */
/* Reserved                     A9 */ -1,   /* Reserved */
/* Reserved                     AA */ -1,   /* Reserved */
/* Reserved                     AB */ -1,   /* Reserved */
/* Reserved                     AC */ -1,   /* Reserved */
/* Reserved                     AD */ -1,   /* Reserved */
/* Reserved                     AE */ -1,   /* Reserved */
/* Reserved                     AF */ -1,   /* Reserved */
/* Reserved                     B0 */ -1,   /* Reserved */
/* Reserved                     B1 */ -1,   /* Reserved */
/* Reserved                     B2 */ -1,   /* Reserved */
/* Reserved                     B3 */ -1,   /* Reserved */
/* Reserved                     B4 */ -1,   /* Reserved */
/* Reserved                     B5 */ -1,   /* Reserved */
/* Reserved                     B6 */ -1,   /* Reserved */
/* Reserved                     B7 */ -1,   /* Reserved */
/* Reserved                     B8 */ -1,   /* Reserved */
/* Reserved                     B9 */ -1,   /* Reserved */
/* Reserved                     BA */ -1,   /* Reserved */
/* Reserved                     BB */ -1,   /* Reserved */
/* Reserved                     BC */ -1,   /* Reserved */
/* Reserved                     BD */ -1,   /* Reserved */
/* Reserved                     BE */ -1,   /* Reserved */
/* Reserved                     BF */ -1,   /* Reserved */
/* Reserved                     C0 */ -1,   /* Reserved */
/* Reserved                     C1 */ -1,   /* Reserved */
/* Reserved                     C2 */ -1,   /* Reserved */
/* Reserved                     C3 */ -1,   /* Reserved */
/* Reserved                     C4 */ -1,   /* Reserved */
/* Reserved                     C5 */ -1,   /* Reserved */
/* Reserved                     C6 */ -1,   /* Reserved */
/* Reserved                     C7 */ -1,   /* Reserved */
/* Reserved                     C8 */ -1,   /* Reserved */
/* Reserved                     C9 */ -1,   /* Reserved */
/* Reserved                     CA */ -1,   /* Reserved */
/* Reserved                     CB */ -1,   /* Reserved */
/* Reserved                     CC */ -1,   /* Reserved */
/* Reserved                     CD */ -1,   /* Reserved */
/* Reserved                     CE */ -1,   /* Reserved */
/* Reserved                     CF */ -1,   /* Reserved */
/* Reserved                     D0 */ -1,   /* Reserved */
/* Reserved                     D1 */ -1,   /* Reserved */
/* Reserved                     D2 */ -1,   /* Reserved */
/* Reserved                     D3 */ -1,   /* Reserved */
/* Reserved                     D4 */ -1,   /* Reserved */
/* Reserved                     D5 */ -1,   /* Reserved */
/* Reserved                     D6 */ -1,   /* Reserved */
/* Reserved                     D7 */ -1,   /* Reserved */
/* Reserved                     D8 */ -1,   /* Reserved */
/* Reserved                     D9 */ -1,   /* Reserved */
/* Reserved                     DA */ -1,   /* Reserved */
/* Reserved                     DB */ -1,   /* Reserved */
/* Reserved                     DC */ -1,   /* Reserved */
/* Reserved                     DD */ -1,   /* Reserved */
/* Reserved                     DE */ -1,   /* Reserved */
/* Reserved                     DF */ -1,   /* Reserved */
/* Left Control                 E0 */ 0x1D, /* 9D */
/* Left Shift                   E1 */ 0x2A, /* AA */
/* Left Alt                     E2 */ 0x38, /* B8 */
/* Left GUI                     E3 */ E0PREFIX|0x5B, /* E0 DB */
/* Right Control                E4 */ E0PREFIX|0x1D, /* E0 9D */
/* Right Shift                  E5 */ 0x36, /* B6 */
/* Right Alt                    E6 */ E0PREFIX|0x38, /* E0 B8 */
/* Right GUI                    E7 */ E0PREFIX|0x5C  /* E0 DC */
};

#define xsize	((int32_t)(sizeof(x)/sizeof(x[0])))

struct kbd_mods {
	uint32_t mask, key;
};

#define	KBD_NMOD	8
static struct kbd_mods kbd_mods[KBD_NMOD] = {
	{MOD_CONTROL_L, 0xe0},
	{MOD_CONTROL_R, 0xe4},
	{MOD_SHIFT_L, 0xe1},
	{MOD_SHIFT_R, 0xe5},
	{MOD_ALT_L, 0xe2},
	{MOD_ALT_R, 0xe6},
	{MOD_WIN_L, 0xe3},
	{MOD_WIN_R, 0xe7},
};

/*
 * Translate HID code into PS/2 code and put codes into buffer b.
 * Returns the number of codes put in b.
 */

#undef  PUT
#define PUT(c, n, b)		\
do {				\
	*(b) = (c);		\
	(b) ++;			\
	(n) ++;			\
} while (0)

static void
kbd_write(struct hid_child *hc, int hid_key, int make)
{
	struct hid_parent *hp;
	int buf[32], *b, c, n;

	hp = hc->parent;
	assert(hp != NULL);

	if (hid_key >= xsize) {
		printf("%s: iface(%d) invalid keycode(%d) received",
		    hp->dev, hp->ndx, hid_key);
		return;
	}

	if ((c = x[hid_key]) == -1)
		return; /* HID code translation is not defined */

	n = 0;
	b = buf;
	if (make) {
		if (c & E0PREFIX)
			PUT(0xe0, n, b);
		PUT((c & CODEMASK), n, b);
	} else if (!(c & NOBREAK)) {
		if (c & E0PREFIX)
			PUT(0xe0, n, b);
		PUT((0x80|(c & CODEMASK)), n, b);
	}

	if (n > 0)
		write(KBD.vkbd_fd, buf, n * sizeof(buf[0]));	
}

static void
kbd_process_keys(struct hid_child *hc)
{
	uint32_t n_mod;
	uint32_t o_mod;
	uint8_t key;
	int i, j;

	n_mod = KBD.ndata.mod;
	o_mod = KBD.odata.mod;
	if (n_mod != o_mod) {
		for (i = 0; i < KBD_NMOD; i++) {
			if ((n_mod & kbd_mods[i].mask) !=
			    (o_mod & kbd_mods[i].mask)) {
				kbd_write(hc, kbd_mods[i].key,
				    (n_mod & kbd_mods[i].mask));
			}
		}
	}

	/* Check for released keys. */
	for (i = 0; i < KBD.key_cnt; i++) {
		key = KBD.odata.keycode[i];
		if (key == 0) {
			continue;
		}
		for (j = 0; j < KBD.key_cnt; j++) {
			if (KBD.ndata.keycode[j] == 0) {
				continue;
			}
			if (key == KBD.ndata.keycode[j]) {
				goto rfound;
			}
		}
		kbd_write(hc, key, 0);
	rfound:	;
	}

	/* Check for pressed keys. */
	for (i = 0; i < KBD.key_cnt; i++) {
		key = KBD.ndata.keycode[i];
		if (key == 0) {
			continue;
		}
/* 		sc->sc_ntime[i] = now + sc->sc_kbd.kb_delay1; */
		for (j = 0; j < KBD.key_cnt; j++) {
			if (KBD.odata.keycode[j] == 0) {
				continue;
			}
			if (key == KBD.odata.keycode[j]) {

				/* key is still pressed */

/* 				sc->sc_ntime[i] = sc->sc_otime[j]; */
/* 				dtime = (sc->sc_otime[j] - now); */

/* 				if (!(dtime & 0x80000000)) { */
/* 					/\* time has not elapsed *\/ */
/* 					goto pfound; */
/* 				} */
/* 				sc->sc_ntime[i] = now + sc->sc_kbd.kb_delay2; */
				break;
			}
		}
		kbd_write(hc, key, 1);

		/*
                 * If any other key is presently down, force its repeat to be
                 * well in the future (100s).  This makes the last key to be
                 * pressed do the autorepeat.
                 */
/* 		for (j = 0; j != UKBD_NKEYCODE; j++) { */
/* 			if (j != i) */
/* 				sc->sc_ntime[j] = now + (100 * 1000); */
/* 		} */
/* 	pfound:	; */
	}

	KBD.odata = KBD.ndata;
}

void
kbd_attach(struct hid_child *hc)
{
	struct hid_parent *hp;
	struct stat sb;

	hp = hc->parent;
	assert(hp != NULL);

	/* Open /dev/vkbdctl. */
	if ((KBD.vkbd_fd = open("/dev/vkbdctl", O_RDWR)) < 0) {
		printf("%s: iface(%d) could not open /dev/vkbdctl: %s\n",
		    hp->dev, hp->ndx, strerror(errno));
		return;
	}

	if (debug) {
		if (fstat(KBD.vkbd_fd, &sb) < 0) {
			printf("%s: iface(%d) fstat failed: %s\n", hp->dev,
			    hp->ndx, strerror(errno));
			return;
		}
		printf("%s: iface(%d) kbd device name: %s\n", hp->dev, hp->ndx,
		    devname(sb.st_rdev, S_IFCHR));
	}

	/* Find the location of modifiers. (224 is LeftControl)*/
	if (hid_locate(hc->p, HID_USAGE2(HUP_KEYBOARD, 224), hid_input,
		&KBD.mods)) {
		if (debug)
			printf("%s: iface(%d) modifiers (%d)\n", hp->dev,
			    hp->ndx, KBD.mods.pos);
		/* We want the 8bit long data start from LeftControl. */
		KBD.mods.report_size = 8;
	} else
		printf("%s: iface(%d) warning: no modifiers\n", hp->dev, hp->ndx);


	

	/* Find the location of keycode array. */
	if (hid_locate(hc->p, HID_USAGE2(HUP_KEYBOARD, 0), hid_input,
		&KBD.keys)) {
		if (debug)
			printf("%s: iface(%d) keycode array (%d)\n", hp->dev,
			    hp->ndx, KBD.keys.pos);
	} else
		printf("%s: iface(%d) warning: no keycode array\n", hp->dev,
		    hp->ndx);

	KBD.key_cnt = KBD.keys.report_count;
}

void
kbd_recv(struct hid_child *hc, char *buf, int len)
{
	struct hid_parent *hp;
	int cnt, i;

	(void) len;

	hp = hc->parent;
	assert(hp != NULL);

	KBD.ndata.mod = (uint8_t) hid_get_data(buf, &KBD.mods);
	cnt = hid_get_array8(buf, KBD.ndata.keycode, &KBD.keys);
	if (debug) {
		printf("%s: iface(%d) keycode received data: mod(0x%02x)",
		    hp->dev, hp->ndx, KBD.ndata.mod);
		for (i = 0; i < cnt; i++)
			printf(" %d", KBD.ndata.keycode[i]);
		putchar('\n');
	}

	kbd_process_keys(hc);
}
