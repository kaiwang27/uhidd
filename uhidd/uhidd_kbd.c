/*-
 * Copyright (c) 2009, 2010, 2012, 2015 Kai Wang
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
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/kbio.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/vkbd/vkbd_var.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "uhidd.h"
#include "kbdmap_lex.h"

#define	HUG_NUM_LOCK		0x0001
#define	HUG_CAPS_LOCK		0x0002
#define	HUG_SCROLL_LOCK		0x0003

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
/* Keypad 00                    B0 */ -1,   /* Unassigned */
/* Keypad 000                   B1 */ -1,   /* Unassigned */
/* Thousands Separator          B2 */ -1,   /* Unassigned */
/* Decimal Separator            B3 */ -1,   /* Unassigned */
/* Currency Unit                B4 */ -1,   /* Unassigned */
/* Currency Sub-unit            B5 */ -1,   /* Unassigned */
/* Keypad (                     B6 */ -1,   /* Unassigned */
/* Keypad )                     B7 */ -1,   /* Unassigned */
/* Keypad {                     B8 */ -1,   /* Unassigned */
/* Keypad }                     B9 */ -1,   /* Unassigned */
/* Keypad Tab                   BA */ -1,   /* Unassigned */
/* Keypad Backspace             BB */ -1,   /* Unassigned */
/* Keypad A                     BC */ -1,   /* Unassigned */
/* Keypad B                     BD */ -1,   /* Unassigned */
/* Keypad C                     BE */ -1,   /* Unassigned */
/* Keypad D                     BF */ -1,   /* Unassigned */
/* Keypad E                     C0 */ -1,   /* Unassigned */
/* Keypad F                     C1 */ -1,   /* Unassigned */
/* Keypad XOR                   C2 */ -1,   /* Unassigned */
/* Keypad ^                     C3 */ -1,   /* Unassigned */
/* Keypad %                     C4 */ -1,   /* Unassigned */
/* Keypad <                     C5 */ -1,   /* Unassigned */
/* Keypad >                     C6 */ -1,   /* Unassigned */
/* Keypad &                     C7 */ -1,   /* Unassigned */
/* Keypad &&                    C8 */ -1,   /* Unassigned */
/* Keypad |                     C9 */ -1,   /* Unassigned */
/* Keypad ||                    CA */ -1,   /* Unassigned */
/* Keypad :                     CB */ -1,   /* Unassigned */
/* Keypad #                     CC */ -1,   /* Unassigned */
/* Keypad Space                 CD */ -1,   /* Unassigned */
/* Keypad @                     CE */ -1,   /* Unassigned */
/* Keypad !                     CF */ -1,   /* Unassigned */
/* Keypad Memory Store          D0 */ -1,   /* Unassigned */
/* Keypad Memory Recall         D1 */ -1,   /* Unassigned */
/* Keypad Memory Clear          D2 */ -1,   /* Unassigned */
/* Keypad Memory Add            D3 */ -1,   /* Unassigned */
/* Keypad Memory Subtract       D4 */ -1,   /* Unassigned */
/* Keypad Memory Multiply       D5 */ -1,   /* Unassigned */
/* Keypad Memory Divide         D6 */ -1,   /* Unassigned */
/* Keypad +/-                   D7 */ -1,   /* Unassigned */
/* Keypad Clear                 D8 */ -1,   /* Unassigned */
/* Keypad Clear Entry           D9 */ -1,   /* Unassigned */
/* Keypad Binary                DA */ -1,   /* Unassigned */
/* Keypad Octal                 DB */ -1,   /* Unassigned */
/* Keypad Decimal               DC */ -1,   /* Unassigned */
/* Keypad Hexadecimal           DD */ -1,   /* Unassigned */
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

/*
 * Ordinary keypad is mapped properly by the HID to PS/2 translation
 * table. Extended keypad (e.g. keypad = found on Apple keyboards)
 * is supported by mapping the keypad keys to equivalent keyboard
 * keys. Two-char/three-char keypad keys (e.g. Keypad 00, Keypad &&,
 * etc) is simulated by pressing equivalent keyboard keys multiple
 * times.
 *
 * (Note, Keypad keys like Memory Store, Clear are not supported.
 * Also, since the keypad keys are emulated by sending modifier
 * keys and keyboard keys, modifier key status from other keyboard(s)
 * will cause wrong keys been generated)
 */

static struct {
	unsigned char sym;
	int rpt;
} kx[] = {
/* Keypad =                     67 */ { .sym = '=',  .rpt = 1 },
/* Keypad 00                    B0 */ { .sym = '0',  .rpt = 2 },
/* Keypad 000                   B1 */ { .sym = '0',  .rpt = 3 },
/* Thousands Separator          B2 */ { .sym = '\0', .rpt = 0 },
/* Decimal Separator            B3 */ { .sym = '\0', .rpt = 0 },
/* Currency Unit                B4 */ { .sym = '\0', .rpt = 0 },
/* Currency Sub-unit            B5 */ { .sym = '\0', .rpt = 0 },
/* Keypad (                     B6 */ { .sym = '(',  .rpt = 1 },
/* Keypad )                     B7 */ { .sym = ')',  .rpt = 1 },
/* Keypad {                     B8 */ { .sym = '{',  .rpt = 1 },
/* Keypad }                     B9 */ { .sym = '}',  .rpt = 1 },
/* Keypad Tab                   BA */ { .sym = '\t', .rpt = 1 },
/* Keypad Backspace             BB */ { .sym = '\b', .rpt = 1 },
/* Keypad A                     BC */ { .sym = 'A',  .rpt = 1 },
/* Keypad B                     BD */ { .sym = 'B',  .rpt = 1 },
/* Keypad C                     BE */ { .sym = 'C',  .rpt = 1 },
/* Keypad D                     BF */ { .sym = 'D',  .rpt = 1 },
/* Keypad E                     C0 */ { .sym = 'E',  .rpt = 1 },
/* Keypad F                     C1 */ { .sym = 'F',  .rpt = 1 },
/* Keypad XOR                   C2 */ { .sym = '\0', .rpt = 0 },
/* Keypad ^                     C3 */ { .sym = '^',  .rpt = 1 },
/* Keypad %                     C4 */ { .sym = '%',  .rpt = 1 },
/* Keypad <                     C5 */ { .sym = '<',  .rpt = 1 },
/* Keypad >                     C6 */ { .sym = '>',  .rpt = 1 },
/* Keypad &                     C7 */ { .sym = '&',  .rpt = 1 },
/* Keypad &&                    C8 */ { .sym = '&',  .rpt = 2 },
/* Keypad |                     C9 */ { .sym = '|',  .rpt = 1 },
/* Keypad ||                    CA */ { .sym = '|',  .rpt = 2 },
/* Keypad :                     CB */ { .sym = ':',  .rpt = 1 },
/* Keypad #                     CC */ { .sym = '#',  .rpt = 1 },
/* Keypad Space                 CD */ { .sym = ' ',  .rpt = 1 },
/* Keypad @                     CE */ { .sym = '@',  .rpt = 1 },
/* Keypad !                     CF */ { .sym = '!',  .rpt = 1 },
/* Keypad Memory Store          D0 */ { .sym = '\0', .rpt = 0 },
/* Keypad Memory Recall         D1 */ { .sym = '\0', .rpt = 0 },
/* Keypad Memory Clear          D2 */ { .sym = '\0', .rpt = 0 },
/* Keypad Memory Add            D3 */ { .sym = '\0', .rpt = 0 },
/* Keypad Memory Subtract       D4 */ { .sym = '\0', .rpt = 0 },
/* Keypad Memory Multiply       D5 */ { .sym = '\0', .rpt = 0 },
/* Keypad Memory Divide         D6 */ { .sym = '\0', .rpt = 0 },
/* Keypad +/-                   D7 */ { .sym = '\0', .rpt = 0 },
/* Keypad Clear                 D8 */ { .sym = '\0', .rpt = 0 },
/* Keypad Clear Entry           D9 */ { .sym = '\0', .rpt = 0 },
/* Keypad Binary                DA */ { .sym = '\0', .rpt = 0 },
/* Keypad Octal                 DB */ { .sym = '\0', .rpt = 0 },
/* Keypad Decimal               DC */ { .sym = '\0', .rpt = 0 },
/* Keypad Hexadecimal           DD */ { .sym = '\0', .rpt = 0 },
};

#define kxsize	((int32_t)(sizeof(kx)/sizeof(kx[0])))

/*
 * Keyboard device.
 */

#define	MAX_KEYCODE	256

struct kbd_data {
	uint8_t mod;

#define	MOD_CONTROL_L	0x01
#define	MOD_CONTROL_R	0x10
#define	MOD_SHIFT_L	0x02
#define	MOD_SHIFT_R	0x20
#define	MOD_ALT_L	0x04
#define	MOD_ALT_R	0x40
#define	MOD_WIN_L	0x08
#define	MOD_WIN_R	0x80

	struct hid_key keycode[MAX_KEYCODE];
	uint32_t time[MAX_KEYCODE];
};

struct keypad_map {
	int sc;
	uint8_t mod;
	uint8_t state;
};

struct kbd_dev {
	int vkbd_fd;
	int key_cnt;
	struct kbd_data ndata;
	struct kbd_data odata;
	pthread_t kbd_task;
	pthread_t kbd_status_task;
	pthread_mutex_t kbd_mtx;
	void *kbd_context;
	uint32_t now;
	int delay1;
	int delay2;
	struct keypad_map kpm[kxsize];
	/* Keycode translator. */
	int (*kbd_tr)(struct hid_appcol *, struct hid_key, int,
	    struct hid_scancode *, int);
	struct hid_appcol *ha;

#define KB_DELAY1	500
#define KB_DELAY2	100
};

#define	KBD		hc->u.kd
#define KBD_LOCK	pthread_mutex_lock(&kd->kbd_mtx);
#define KBD_UNLOCK	pthread_mutex_unlock(&kd->kbd_mtx);

struct kbd_mods {
	uint32_t mask;
	struct hid_key key;
};

#define	KBD_NMOD	8
static struct kbd_mods kbd_mods[KBD_NMOD] = {
	{.mask = MOD_CONTROL_L,	.key = {HUP_KEYBOARD, 0xe0}},
	{.mask = MOD_CONTROL_R,	.key = {HUP_KEYBOARD, 0xe4}},
	{.mask = MOD_SHIFT_L,	.key = {HUP_KEYBOARD, 0xe1}},
	{.mask = MOD_SHIFT_R,	.key = {HUP_KEYBOARD, 0xe5}},
	{.mask = MOD_ALT_L,	.key = {HUP_KEYBOARD, 0xe2}},
	{.mask = MOD_ALT_R,	.key = {HUP_KEYBOARD, 0xe6}},
	{.mask = MOD_WIN_L,	.key = {HUP_KEYBOARD, 0xe3}},
	{.mask = MOD_WIN_R,	.key = {HUP_KEYBOARD, 0xe7}},
};

static void	*kbd_task(void *arg);
static void	*kbd_status_task(void *arg);
static void	kbd_write(struct kbd_dev *kd, struct hid_key hk, int make);
static void	kbd_process_keys(struct kbd_dev *kd);
static int	keypad_hid2key(struct hid_appcol *ha, struct hid_key hk,
    int make, struct hid_scancode *c, int len);
static void	keypad_init(struct kbd_dev *kd);

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

int
kbd_hid2key(struct hid_appcol *ha, struct hid_key hk, int make,
    struct hid_scancode *c, int len)
{

	assert(c != NULL && len > 0);

	if (hk.code == 0x67 || (hk.code >= 0xB0 && hk.code <= 0xDD))
		return (keypad_hid2key(ha, hk, make, c, len));

	if (hk.code >= xsize) {
		printf("Invalid keycode(%d) received\n", hk.code);
		return (0);
	}

	(*c).sc = x[hk.code];
	(*c).make = make;

	return (1);
}

static void
kbd_write(struct kbd_dev *kd, struct hid_key hk, int make)
{
	struct hid_scancode c[8];
	int buf[32], *b, i, n, nk;

	assert(kd->kbd_tr != NULL);

	nk = kd->kbd_tr(kd->ha, hk, make, c, sizeof(c) / sizeof(c[0]));

	/* Ignore unmapped keys. */
	if (nk == 0)
		return;

	n = 0;
	b = buf;
	for (i = 0; i < nk; i++) {
		if ((size_t) n >= sizeof(buf) / sizeof(buf[0])) {
			syslog(LOG_ERR,
			    "Internal: not enough key input buffer,"
			    " %d keys discarded", nk - i);
			break;
		}
		if (c[i].make) {
			if (c[i].sc & E0PREFIX)
				PUT(0xe0, n, b);
			PUT((c[i].sc & CODEMASK), n, b);
		} else if (!(c[i].sc & NOBREAK)) {
			if (c[i].sc & E0PREFIX)
				PUT(0xe0, n, b);
			PUT((0x80|(c[i].sc & CODEMASK)), n, b);
		}
	}

	if (n > 0)
		write(kd->vkbd_fd, buf, n * sizeof(buf[0]));
}

static void
kbd_process_keys(struct kbd_dev *kd)
{
	uint32_t n_mod;
	uint32_t o_mod;
	uint16_t key, up;
	int dtime, i, j;

	n_mod = kd->ndata.mod;
	o_mod = kd->odata.mod;
	if (n_mod != o_mod) {
		for (i = 0; i < KBD_NMOD; i++) {
			if ((n_mod & kbd_mods[i].mask) !=
			    (o_mod & kbd_mods[i].mask)) {
				kbd_write(kd, kbd_mods[i].key,
				    (n_mod & kbd_mods[i].mask));
			}
		}
	}

	/* Check for released keys. */
	for (i = 0; i < kd->key_cnt; i++) {
		key = kd->odata.keycode[i].code;
		up = kd->odata.keycode[i].up;
		if (key == 0) {
			continue;
		}
		for (j = 0; j < kd->key_cnt; j++) {
			if (kd->ndata.keycode[j].code == 0) {
				continue;
			}
			if (key == kd->ndata.keycode[j].code &&
			    up == kd->ndata.keycode[j].up) {
				goto rfound;
			}
		}
		kbd_write(kd, kd->odata.keycode[i], 0);
	rfound:
		;
	}

	/* Check for pressed keys. */
	for (i = 0; i < kd->key_cnt; i++) {
		key = kd->ndata.keycode[i].code;
		up = kd->ndata.keycode[i].up;
		if (key == 0) {
			continue;
		}
		kd->ndata.time[i] = kd->now + kd->delay1;
		for (j = 0; j < kd->key_cnt; j++) {
			if (kd->odata.keycode[j].code == 0) {
				continue;
			}
			if (key == kd->odata.keycode[j].code &&
			    up == kd->odata.keycode[j].up) {
				/*
				 * Key is still pressed.
				 */
				kd->ndata.time[i] = kd->odata.time[j];
				dtime = (kd->odata.time[j] - kd->now);
				if (!(dtime & 0x80000000)) {
					/* time has not elapsed */
					goto pfound;
				}
				kd->ndata.time[i] = kd->now + kd->delay2;
				break;
			}
		}
		kbd_write(kd, kd->ndata.keycode[i], 1);

		/*
                 * If any other key is presently down, force its repeat to be
                 * well in the future (100s).  This makes the last key to be
                 * pressed do the autorepeat.
                 */
		for (j = 0; j < kd->key_cnt; j++) {
			if (j != i)
				kd->ndata.time[j] = kd->now + (100 * 1000);
		}
	pfound:
		;
	}

	kd->odata = kd->ndata;
}

int
kbd_match(struct hid_appcol *ha)
{
	struct hid_interface *hi;
	unsigned int u;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if (config_kbd_attach(hi) <= ATTACH_NO)
		return (HID_MATCH_NONE);

	u = hid_appcol_get_usage(ha);
	if (u == HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD))
		return (HID_MATCH_GENERAL);

	return (HID_MATCH_NONE);
}

int
kbd_attach(struct hid_appcol *ha)
{
	struct hid_interface *hi;
	struct kbd_dev *kd;
	struct stat sb;
	unsigned int u;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if ((kd = calloc(1, sizeof(*kd))) == NULL) {
		syslog(LOG_ERR, "calloc failed in kbd_attach: %m");
		return (-1);
	}

	hid_appcol_set_private(ha, kd);
	kd->ha = ha;

	/* Open /dev/vkbdctl. */
	if ((kd->vkbd_fd = open("/dev/vkbdctl", O_RDWR)) < 0) {
		syslog(LOG_ERR, "%s[%d] could not open /dev/vkbdctl: %m",
		    basename(hi->dev), hi->ndx);
		if (errno == ENOENT)
			PRINT1(1, "vkbd.ko kernel module not loaded?\n");
		return (-1);
	}

	if (verbose) {
		if (fstat(kd->vkbd_fd, &sb) < 0) {
			syslog(LOG_ERR, "%s[%d] fstat: /dev/vkbdctl: %m",
			    basename(hi->dev), hi->ndx);
			return (-1);
		}
		PRINT1(1, "kbd device name: %s\n",
		    devname(sb.st_rdev, S_IFCHR));
	}

	/*
	 * Search the currently installed keymap for the scancodes to which
	 * we should map the extended keypad keys.
	 */
	keypad_init(kd);

	/* TODO: These should be tunable. */
	kd->delay1 = KB_DELAY1;
	kd->delay2 = KB_DELAY2;

	kbd_set_tr(ha, kbd_hid2key);

	pthread_mutex_init(&kd->kbd_mtx, NULL);
	pthread_create(&kd->kbd_task, NULL, kbd_task, kd);

	/*
	 * Only start keyborad status task if it's a real keyboard.
	 * (e.g. should not start task for comsumer control device)
	 */
	u = hid_appcol_get_usage(ha);
	if (u == HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD))
		pthread_create(&kd->kbd_status_task, NULL, kbd_status_task, ha);

	return (0);
}

void
kbd_recv(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_interface *hi;
	struct hid_field *hf;
	struct hid_key keycodes[MAX_KEYCODE];
	unsigned int usage;
	int cnt, flags, i;
	uint8_t mod;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);
	mod = 0;
	cnt = 0;
	hf = NULL;
	while ((hf = hid_report_get_next_field(hr, hf, HID_INPUT)) != NULL) {
		flags = hid_field_get_flags(hf);
		if (flags & HIO_CONST)
			continue;
		usage = hid_field_get_usage_min(hf);
		if (usage == HID_USAGE2(HUP_KEYBOARD, 224)) {
			for (i = 0; i < hf->hf_count; i++)
				mod |= hf->hf_value[i] << i;
		}
		if (usage == HID_USAGE2(HUP_KEYBOARD, 0)) {
			cnt = hf->hf_count;
			for (i = 0; i < cnt; i++) {
				keycodes[i].code = HID_USAGE(hf->hf_usage[i]);
				keycodes[i].up = HUP_KEYBOARD;
			}
		}
	}

	if (verbose > 1) {
		PRINT1(2, "mod(0x%02x) key codes: ", mod);
		for (i = 0; i < cnt; i++)
			printf("0x%02x ", keycodes[i].code);
		putchar('\n');
	}

	kbd_input(ha, mod, keycodes, cnt);
}

void
kbd_input(struct hid_appcol *ha, uint8_t mod, struct hid_key *keycodes,
    int key_cnt)
{
	struct kbd_dev *kd;
	int i;

	kd = hid_appcol_get_private(ha);
	assert(kd != NULL);

	KBD_LOCK;
	kd->ndata.mod = mod;
	for (i = 0; i < key_cnt; i++)
		kd->ndata.keycode[i] = keycodes[i];
	kd->key_cnt = key_cnt;
	/*
	 * Note that this call to kbd_process_keys is needed. If two adjacent
	 * events are generated within 25ms, kbd_task may miss one of them.
	 */
	kbd_process_keys(kd);
	KBD_UNLOCK;
}

void
kbd_set_tr(struct hid_appcol *ha, hid_translator tr)
{
	struct kbd_dev *kd;

	kd = hid_appcol_get_private(ha);
	assert(kd != NULL);

	kd->kbd_tr = tr;
}

static void *
kbd_task(void *arg)
{
	struct kbd_dev *kd;

	kd = arg;
	assert(kd != NULL);

	for (kd->now = 0; ; kd->now += 25) {
		KBD_LOCK;
		kbd_process_keys(kd);
		KBD_UNLOCK;
		usleep(25 * 1000);	/* wake up every 25ms. */
	}

	return (NULL);
}

static void *
kbd_status_task(void *arg)
{
	struct hid_interface *hi;
	struct hid_appcol *ha;
	struct hid_report *hr;
	struct hid_field *hf;
	struct kbd_dev *kd;
	vkbd_status_t vs;
	unsigned int usage;
	int flags, found, len, i;

	ha = arg;
	assert(ha != NULL);
	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);
	kd = hid_appcol_get_private(ha);
	assert(kd != NULL);

	for (;;) {
		len = read(kd->vkbd_fd, &vs, sizeof(vs));
		if (len < 0) {
			if (errno != EINTR)
				break;
			continue;
		}
		PRINT1(1, "kbd status changed: leds=0x%x\n", vs.leds);
		hr = NULL;
		while ((hr = hid_appcol_get_next_report(ha, hr)) != NULL) {
			found = 0;
			hf = NULL;
			while ((hf = hid_report_get_next_field(hr, hf,
			    HID_OUTPUT)) != NULL) {
				flags = hid_field_get_flags(hf);
				if (flags & HIO_CONST)
					continue;
				usage = hid_field_get_usage_page(hf);
				if (usage != HUP_LEDS)
					continue;
				found = 1;
				for (i = 0; i < hf->hf_count; i++) {
					hid_field_get_usage_value(hf, i, &usage,
					    NULL);
					switch (HID_USAGE(usage)) {
					case HUG_NUM_LOCK:
						if (vs.leds & LED_NUM)
							hid_field_set_value(hf,
							    i, 1);
						else
							hid_field_set_value(hf,
							    i, 0);
						break;
					case HUG_CAPS_LOCK:
						if (vs.leds & LED_CAP)
							hid_field_set_value(hf,
							    i, 1);
						else
							hid_field_set_value(hf,
							    i, 0);
						break;
					case HUG_SCROLL_LOCK:
						if (vs.leds & LED_SCR)
							hid_field_set_value(hf,
							    i, 1);
						else
							hid_field_set_value(hf,
							    i, 0);
						break;
					default:
						hid_field_set_value(hf, i, 0);
						break;
					}

				}
			}
			if (found)
				hid_appcol_xfer_data(ha, hr);
		}
	}

	return (NULL);
}

#undef PUTSC
#define PUTSC(s, m, n, b)	\
do {				\
	if (n >= len)		\
		break;		\
	(b)->sc = (s);		\
	(b)->make = m;		\
	(b) ++;			\
	(n) ++;			\
} while(0)

static int
keypad_hid2key(struct hid_appcol *ha, struct hid_key hk, int make,
    struct hid_scancode *c, int len)
{
	struct hid_scancode *buf;
	struct kbd_dev *kd;
	uint32_t n_mod;
	uint32_t o_mod;
	int i, kc, nk;

	kd = hid_appcol_get_private(ha);
	assert(kd != NULL);

	assert(c != NULL && len > 0);
	assert(hk.code == 0x67 || (hk.code >= 0xB0 && hk.code <= 0xDD));

	if (hk.code == 0x67)
		kc = 0;
	else
		kc = hk.code - 0xAF;

	if (kc < 0 || kc >= kxsize) {
		printf("Invalid keypad code(%d) received\n", hk.code);
		return (0);
	}

	/* Ignore unsupported keypad keys. */
	if (kx[kc].sym == '\0' || kx[kc].rpt == 0)
		return (0);

	/* Ignore keypad keys that are not mapped. */
	if (kd->kpm[kc].sc == 0)
		return (0);

	nk = 0;
	buf = c;

	/*
	 * TODO: The code can not detect modifier key status from other
	 * keyboard(s). One possible future improvement is:
	 * If the keypad key is generated by a comsuer controller,
	 * we should use the modfier key status from the keyboard interface
	 * instead, if it's us not ukbd(4) handling the keyboard interface.
	 */

	n_mod = kd->kpm[kc].mod;
	o_mod = kd->ndata.mod;

	if (make) {
		/*
		 * Press/Release the mod keys needed to produce the
		 * keypad key.
		 */
		if (n_mod != o_mod) {
			for (i = 0; i < KBD_NMOD; i++) {
				if ((n_mod & kbd_mods[i].mask) !=
				    (o_mod & kbd_mods[i].mask)) {
					PUTSC(x[kbd_mods[i].key.code],
					    (n_mod & kbd_mods[i].mask), nk,
					    buf);
				}

			}
		}

		/* Press the keypad key. */
		for (i = 0; i < kx[kc].rpt; i++)
			PUTSC(kd->kpm[kc].sc, 1, nk, buf);

	} else {

		/* Release the keypad key. */
		PUTSC(kd->kpm[kc].sc, 0, nk, buf);

		/* Restore the previous mod keys state. */
		if (n_mod != o_mod) {
			for (i = 0; i < KBD_NMOD; i++) {
				if ((n_mod & kbd_mods[i].mask) !=
				    (o_mod & kbd_mods[i].mask)) {
					PUTSC(x[kbd_mods[i].key.code],
					    (o_mod & kbd_mods[i].mask), nk,
					    buf);
				}

			}
		}
	}

	return (nk);
}

#define KEYMAP_PATH1 "/usr/share/syscons/keymaps"
#define KEYMAP_PATH2 "/usr/share/vt/keymaps"

int kbdmap_number;
char kbdmap_letter;

static int
keypad_search_key(struct kbd_dev *kd, int sc, int state, char letter)
{
	struct hid_interface *hi;
	struct hid_appcol *ha;
	int i, found, pr;

	ha = kd->ha;
	assert(ha != NULL);
	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	found = pr = 0;
	for (i = 0; i < kxsize; i++) {
		if (kx[i].sym == '\0')
			continue;

		if (kx[i].sym != (unsigned char) letter)
			continue;

		/*
		 * Prefer the one without modifiers or less modifiers,
		 * if found multiple mappings for the same letter.
		 * FIXME Could be wrong here.
		 */
		if (kd->kpm[i].sc != 0 && state >= (int) kd->kpm[i].state)
			continue;

		found = 1;
		kd->kpm[i].sc = sc;
		kd->kpm[i].state = (uint8_t) state;
		switch (state) {
		case 0:
			kd->kpm[i].mod = 0;
			break;
		case 1:
			kd->kpm[i].mod = MOD_SHIFT_L;
			break;
		case 2:
			kd->kpm[i].mod = MOD_CONTROL_L;
			break;
		case 3:
			kd->kpm[i].mod = MOD_SHIFT_L | MOD_CONTROL_L;
			break;
		case 4:
			kd->kpm[i].mod = MOD_ALT_L;
			break;
		case 5:
			kd->kpm[i].mod = MOD_ALT_L | MOD_SHIFT_L;
			break;
		case 6:
			kd->kpm[i].mod = MOD_ALT_L | MOD_CONTROL_L;
			break;
		case 7:
			kd->kpm[i].mod = MOD_ALT_L | MOD_CONTROL_L |
				MOD_SHIFT_L;
			break;
		default:
			/* What? */
			kd->kpm[i].mod = 0;
			break;
		}

		if (verbose > 2 && !pr) {
			pr = 1;
			if (kx[i].sym == '\t')
				PRINT1(3, "keypad '%s' mapped to scancode %d"
				    " mod 0x%02x\n", "\\t", kd->kpm[i].sc,
				    kd->kpm[i].mod);
			else if (kx[i].sym == '\b')
				PRINT1(3, "keypad '%s' mapped to scancode %d"
				    " mod 0x%02x\n", "\\b", kd->kpm[i].sc,
				    kd->kpm[i].mod);
			else
				PRINT1(3, "keypad '%c' mapped to scancode %d"
				    " mod 0x%02x\n", kx[i].sym, kd->kpm[i].sc,
				    kd->kpm[i].mod);
		}
	}

	return (found);
}

static int
keypad_parse_keymap(struct kbd_dev *kd, const char *keymap_file)
{
	struct hid_interface *hi;
	struct hid_appcol *ha;
	int token, i, sc, cnt, found;

	ha = kd->ha;
	assert(ha != NULL);
	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if ((kbdmapin = fopen(keymap_file, "r")) == NULL) {
		PRINT1(3, "could not open %s", keymap_file);
		return (-1);
	}

	cnt = 0;
	for (;;) {
		token = kbdmaplex();
		if (token == 0)
			break;
		if (token == TNEWLINE)
			continue;

		/* Only interested in key definition line. */
		if (token != TNUM) {
			for (;;) {
				token = kbdmaplex();
				if (token == 0)
					goto parse_done;
				if (token == TNEWLINE)
					break;
			}
		}

		/* Parse the line. */
		sc = kbdmap_number;
		if (sc < 0 || sc >= NUM_KEYS)
			break;
		found = 0;
		for (i = 0; i < NUM_STATES; i++) {
			token = kbdmaplex();
			if (token == 0)
				goto parse_done;
			if (token == TNEWLINE)
				break;
			if (token != TLET)
				continue;
			found = keypad_search_key(kd, sc, i, kbdmap_letter);
			if (found)
				cnt++;
		}
		if ((token = kbdmaplex()) != TFLAG)
			break;
	}

parse_done:
	fclose(kbdmapin);

	return (cnt);
}

static void
keypad_init(struct kbd_dev *kd)
{
	struct hid_interface *hi;
	struct hid_appcol *ha;
	struct stat sb;
	char buf[1024], mapfile[64];
	FILE *fp;
	int c, found;

	ha = kd->ha;
	assert(ha != NULL);
	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	/*
	 * In order to simulate the right key press with non-English
	 * keyboards, we load the keymap the user configures in /etc/rc.conf,
	 * and find the corresponding scancodes and modifiers for all
	 * the keypad keys.
	 */

	found = 0;
	snprintf(buf, sizeof(buf), "grep -m 1 keymap /etc/rc.conf |"
	    " cut -d'=' -f 2 | tr -d '\\040\\042\\011\\012\\015'");
	if ((fp = popen(buf, "r")) != NULL) {
		if ((c = fread(mapfile, 1, sizeof(mapfile) - 1, fp)) > 1) {
			mapfile[c] = '\0';
			PRINT1(3, "searching for keymap %s\n", mapfile);
			snprintf(buf, sizeof(buf), "%s/%s", KEYMAP_PATH1,
			    mapfile);
			if (stat(buf, &sb) == 0) {
				found = 1;
				goto parse_keymap;
			}
			snprintf(buf, sizeof(buf), "%s/%s", KEYMAP_PATH2,
			    mapfile);
			if (stat(buf, &sb) == 0) {
				found = 1;
				goto parse_keymap;
			}
			PRINT1(3, "keymap %s not exist\n", mapfile);
		}
	}

parse_keymap:

	if (fp != NULL)
		pclose(fp);

	if (found) {
		c = keypad_parse_keymap(kd, buf);
		if (c > 0) {
			PRINT1(3, "found %d keys for keymap\n", c);
			return;
		}
	}

	/* Use the default keymap for English keyboard. */
	PRINT1(3, "keypad uses default keymap for English keyboard\n");

	kd->kpm[0].sc = 0x0D; kd->kpm[0].mod = 0;		/* = */
	kd->kpm[1].sc = 0x0B; kd->kpm[1].mod = 0;		/* 00 */
	kd->kpm[2].sc = 0x0B; kd->kpm[2].mod = 0;		/* 000 */
	kd->kpm[7].sc = 0x0A; kd->kpm[7].mod = MOD_SHIFT_L;	/* ( */
	kd->kpm[8].sc = 0x0B; kd->kpm[8].mod = MOD_SHIFT_L;	/* ) */
	kd->kpm[9].sc = 0x1A; kd->kpm[9].mod = MOD_SHIFT_L;	/* [ */
	kd->kpm[10].sc = 0x1B; kd->kpm[10].mod = MOD_SHIFT_L;	/* ] */
	kd->kpm[11].sc = 0x0F; kd->kpm[11].mod = 0;		/* \t */
	kd->kpm[12].sc = 0x0E; kd->kpm[12].mod = 0;		/* \b */
	kd->kpm[13].sc = 0x1E; kd->kpm[13].mod = MOD_SHIFT_L;	/* A */
	kd->kpm[14].sc = 0x30; kd->kpm[14].mod = MOD_SHIFT_L;	/* B */
	kd->kpm[15].sc = 0x2E; kd->kpm[15].mod = MOD_SHIFT_L;	/* C */
	kd->kpm[16].sc = 0x20; kd->kpm[16].mod = MOD_SHIFT_L;	/* D */
	kd->kpm[17].sc = 0x12; kd->kpm[17].mod = MOD_SHIFT_L;	/* E */
	kd->kpm[18].sc = 0x21; kd->kpm[18].mod = MOD_SHIFT_L;	/* F */
	kd->kpm[20].sc = 0x07; kd->kpm[20].mod = MOD_SHIFT_L;	/* ^ */
	kd->kpm[21].sc = 0x06; kd->kpm[21].mod = MOD_SHIFT_L;	/* % */
	kd->kpm[22].sc = 0x33; kd->kpm[22].mod = MOD_SHIFT_L;	/* < */
	kd->kpm[23].sc = 0x34; kd->kpm[23].mod = MOD_SHIFT_L;	/* > */
	kd->kpm[24].sc = 0x08; kd->kpm[24].mod = MOD_SHIFT_L;	/* & */
	kd->kpm[25].sc = 0x08; kd->kpm[25].mod = MOD_SHIFT_L;	/* && */
	kd->kpm[26].sc = 0x2B; kd->kpm[26].mod = MOD_SHIFT_L;	/* | */
	kd->kpm[27].sc = 0x2B; kd->kpm[27].mod = MOD_SHIFT_L;	/* || */
	kd->kpm[28].sc = 0x27; kd->kpm[28].mod = MOD_SHIFT_L;	/* : */
	kd->kpm[29].sc = 0x04; kd->kpm[29].mod = MOD_SHIFT_L;	/* # */
	kd->kpm[30].sc = 0x39; kd->kpm[30].mod = 0;		/* SPACE */
	kd->kpm[31].sc = 0x03; kd->kpm[31].mod = MOD_SHIFT_L;	/* @ */
	kd->kpm[32].sc = 0x02; kd->kpm[32].mod = MOD_SHIFT_L;	/* ! */
}
