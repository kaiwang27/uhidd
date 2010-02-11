/*-
 * Copyright (c) 2010 Kai Wang
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
#include "uhidd.h"

static struct hid_appcol_driver _hid_appcol_driver_list[] = {
	/* General Keyboard Driver. */
	{
		kbd_match,
		kbd_attach,
		kbd_recv,
		NULL
	},

	/* General Mouse Driver. */
	{
		mouse_match,
		mouse_attach,
		mouse_recv,
		NULL
	},

	/* Virtual HID Driver. */
	{
		vhid_match,
		vhid_attach,
		NULL,
		vhid_recv_raw
	},

	/* General Consumer Control Driver. */
	{
		cc_match,
		cc_attach,
		cc_recv,
		NULL
	}
};

int hid_appcol_driver_num = sizeof(_hid_appcol_driver_list) /
    sizeof(_hid_appcol_driver_list[0]);
struct hid_appcol_driver *hid_appcol_driver_list = _hid_appcol_driver_list;
struct hid_appcol_driver *kbd_driver = &_hid_appcol_driver_list[0];
struct hid_appcol_driver *mouse_driver = &_hid_appcol_driver_list[1];
struct hid_appcol_driver *ghid_driver = &_hid_appcol_driver_list[2];
struct hid_appcol_driver *cc_driver = &_hid_appcol_driver_list[3];
