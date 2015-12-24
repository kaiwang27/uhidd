/*-
 * Copyright (c) 2015 Kai Wang
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
#include <dev/usb/usbhid.h>
#include <libusb20_desc.h>
#include <assert.h>
#include <stdio.h>

#include "uhidd.h"
#include "uhidd_devid.h"

static struct uhidd_devid ms_devid[] = {
	{USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_NATURAL4000},
};

static int
cc_recv_filter(struct hid_appcol *ha, unsigned usage, int value,
    unsigned *rusage, int *len)
{
	struct hid_interface *hi;
	int c;

	assert(*len >= 5);

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	/*
	 * The natural4000 keyboard seems to always report 0xFE03 when
	 * multimedia keys are pressed, with unknown purpose. 
	 */
	if (HID_PAGE(usage) == 0xFF00 && HID_USAGE(usage) == 0xFE03)
		return (HID_FILTER_DISCARD);

	/*
	 * "My Favorites" key events is in the value of usage 0xFF05.
	 * By default we map the 5 my favorite keys to F13, F14, F15,
	 * F16 and F18. (F17 mapped to Scroll Lock)
	 *
	 * Although we support multiple "My Favorites" key presses here,
	 * the keyboard seems to always report key press for only one key.
	 */
	c = 0;
	if (HID_PAGE(usage) == 0xFF00 && HID_USAGE(usage) == 0xFF05) {
		if (value & 0x1)
			rusage[c++] = HID_CUSAGE(HUP_KEYBOARD, 0x68);
		if (value & 0x2)
			rusage[c++] = HID_CUSAGE(HUP_KEYBOARD, 0x69);
		if (value & 0x4)
			rusage[c++] = HID_CUSAGE(HUP_KEYBOARD, 0x6A);
		if (value & 0x8)
			rusage[c++] = HID_CUSAGE(HUP_KEYBOARD, 0x6B);
		if (value & 0x10)
			rusage[c++] = HID_CUSAGE(HUP_KEYBOARD, 0x6D);

		if (c > 0) {
			*len = c;
			return (HID_FILTER_REPLACE);
		} else {
			PRINT1(0, "unknown value %d for 0xFE05 usage\n",
			    value);
			return (HID_FILTER_DISCARD);
		}
	}

	return (HID_FILTER_KEEP);
}

int
microsoft_match(struct hid_interface *hi)
{

	if (!hid_match_interface(hi, LIBUSB20_CLASS_HID, 0, -1))
		return (HID_MATCH_NONE);

	if (hid_match_devid(hi, ms_devid, sizeof(ms_devid)))
		return (HID_MATCH_DEVICE);

	return (HID_MATCH_NONE);
}

int
microsoft_attach(struct hid_interface *hi)
{

	PRINT1(1, "Load drv_microsoft driver\n");
	
	hi->cc_recv_filter = cc_recv_filter;

	return (0);
}
