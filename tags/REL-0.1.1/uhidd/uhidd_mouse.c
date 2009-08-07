/*-
 * Copyright (c) 2009 Kai Wang
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
__FBSDID("$FreeBSD $");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mouse.h>
#include <sys/consio.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "uhidd.h"

int
mouse_attach(struct hid_child *hc)
{
	struct hid_parent *hp;
	int i;

	hp = hc->parent;
	assert(hp != NULL);

	hc->u.md.cons_fd = open("/dev/consolectl", O_RDWR);
	if (hc->u.md.cons_fd < 0) {
		PRINT2("could not open /dev/consolectl: %s", strerror(errno));
		return (-1);
	}

	/* Find X Axis. */
	if (hid_locate(hc->p, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X), hid_input,
	    &hc->u.md.x)) {
		if (verbose)
			PRINT2("has X AXIS(%d)\n", hc->u.md.x.pos);
	}

	/* Find Y Axis. */
	if (hid_locate(hc->p, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y), hid_input,
	    &hc->u.md.y)) {
		if (verbose)
			PRINT2("has Y AXIS(%d)\n", hc->u.md.y.pos);
	}

	/* HUG_WHEEL is the most common place for mouse wheel. */
	if (hid_locate(hc->p, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_WHEEL),
	    hid_input, &hc->u.md.wheel)) {
		if (verbose)
			PRINT2("wheel found (HUG_WHEEL(%d))\n",
			    hc->u.md.wheel.pos);
		goto next;
	}

	/*
	 * Some older Microsoft mouse (e.g. Microsoft Wireless Intellimouse 2.0)
	 * used HUG_TWHEEL(0x48) to report its wheel. Note that HUG_TWHEEL seems
	 * to be a temporary thing at the time. (The name TWHEEL is also a
	 * non-standard name) Later new HID usage spec defined 0x48 as usage
	 * "Resolution Multiplier" and newer M$ mouse stopped using 0x48 for
	 * mouse wheel.
	 */
	if (hid_locate(hc->p, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_TWHEEL),
	    hid_input, &hc->u.md.wheel)) {
		if (verbose)
			PRINT2("wheel found (HUG_TWHEEL(%d))\n",
			    hc->u.md.wheel.pos);
		goto next;
	}

	/* Try if the mouse is using HUG_Z to report its wheel. */
	if (hid_locate(hc->p, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Z),
		hid_input, &hc->u.md.wheel)) {
		if (verbose)
			PRINT2("wheel found (HUG_Z(%d))\n", hc->u.md.wheel.pos);
	}

	/* Otherwise we have no wheel. */

next:
	for (i = 0; i < BUTTON_MAX; i++) {
		if (!hid_locate(hc->p, HID_USAGE2(HUP_BUTTON, (i + 1)),
		    hid_input, &hc->u.md.btn[i]))
			break;
	}
	hc->u.md.btn_cnt = i;
	if (verbose)
		PRINT2("%d buttons\n", hc->u.md.btn_cnt);

	return (0);
}

void
mouse_recv(struct hid_child *hc, char *buf, int len)
{
	struct hid_parent *hp;
	struct mouse_info mi;
	int b, btn, dx, dy, dw, i;

	(void) len;

	hp = hc->parent;
	assert(hp != NULL);

	dx = hid_get_data(buf, &hc->u.md.x);
	dy = hid_get_data(buf, &hc->u.md.y);
	dw = -hid_get_data(buf, &hc->u.md.wheel);
	btn = 0;
	for (i = 0; i < hc->u.md.btn_cnt; i++) {
		if (i == 1)
			b = 2;
		else if (i == 2)
			b = 1;
		else
			b = i;
		if (hid_get_data(buf, &hc->u.md.btn[i]))
			btn |= (1 << b);
	}

	if (verbose)
		PRINT2("mouse received data: dx(%d) dy(%d) dw(%d) btn(%#x)\n",
		    dx, dy, dw, btn);

	mi.operation = MOUSE_ACTION;
	mi.u.data.x = dx;
	mi.u.data.y = dy;
	mi.u.data.z = dw;
	mi.u.data.buttons = btn;

	if (ioctl(hc->u.md.cons_fd, CONS_MOUSECTL, &mi) < 0)
		PRINT2("could not submit mouse data: ioctl failed: %s\n",
		    strerror(errno));
}
