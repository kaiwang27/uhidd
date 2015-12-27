/*-
 * Copyright (c) 2010 Kai Wang
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include "uhidd.h"

struct hid_appcol_driver hid_appcol_driver_list[] = {
	/* General Keyboard Driver. */
	{
		"kbd",
		kbd_match,
		kbd_attach,
		kbd_recv,
		NULL,
	},

	/* General Mouse Driver. */
	{
		"mouse",
		mouse_match,
		mouse_attach,
		mouse_recv,
		NULL,
	},

	/* Virtual HID Driver. */
	{
		"vhid",
		vhid_match,
		vhid_attach,
		NULL,
		vhid_recv_raw,
	},

	/* General Consumer Control Driver. */
	{
		"cc",
		cc_match,
		cc_attach,
		cc_recv,
		NULL,
	}
};

const int hid_appcol_driver_num = sizeof(hid_appcol_driver_list) /
    sizeof(hid_appcol_driver_list[0]);

struct hid_interface_driver hid_interface_driver_list[] = {
	/* Microsoft input device driver */
	{
		microsoft_match,
		microsoft_attach,
	},
};

const int hid_interface_driver_num = sizeof(hid_interface_driver_list) /
    sizeof(hid_interface_driver_list[0]);
