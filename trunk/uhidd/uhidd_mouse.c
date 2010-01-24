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
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "uhidd.h"

/*
 * Mouse device.
 */

#define	BUTTON_MAX	31

struct mouse_dev {
	int cons_fd;
#if 0
	hid_item_t x;
	hid_item_t y;
	hid_item_t wheel;
	hid_item_t btn[BUTTON_MAX];
	int btn_cnt;
	int flags;
#endif
};

static int
mouse_match(struct hid_appcol *ha)
{
	unsigned int u;

	u = hid_appcol_get_usage(ha);
	if (u == HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE))
		return (HID_MATCH_GENERAL);

	return (HID_MATCH_NONE);
}

static int
mouse_attach(struct hid_appcol *ha)
{
	struct mouse_dev *md;

#if 0
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
#endif

	if ((md = calloc(1, sizeof(*md))) == NULL) {
		syslog(LOG_ERR, "calloc failed in mouse_attach: %m");
		return (-1);
	}

	hid_appcol_set_private(ha, md);

	md->cons_fd = open("/dev/consolectl", O_RDWR);
	if (md->cons_fd < 0) {
		syslog(LOG_ERR, "could not open /dev/consolectl: %m");
		return (-1);
	}

	return (0);
}

#if 0
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
#endif

static void
mouse_recv(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_field *hf;
	struct mouse_dev *md;
	struct mouse_info mi;
	unsigned int usage, up;
	int has_wheel, has_twheel, has_z, flags;
	int b, btn, dx, dy, dw, dt, dz, i;

	md = hid_appcol_get_private(ha);
	assert(md != NULL);

	dx = dy = dw = dt = dz = 0;
	has_wheel = has_twheel = has_z = 0;
	btn = 0;
	hf = NULL;
	while ((hf = hid_report_get_next_field(hr, hf, HID_INPUT)) != NULL) {
		flags = hid_field_get_flags(hf);
		if (flags & HIO_CONST || (flags & HIO_VARIABLE) == 0)
			continue;
		for (i = 0; i < hf->hf_count; i++) {
			usage = HID_USAGE(hf->hf_usage[i]);
			up = HID_PAGE(hf->hf_usage[i]);
			if (up == HUP_BUTTON) {
				if (usage < BUTTON_MAX && hf->hf_value[i]) {
					b = usage - 1;
					if (b == 1)
						b = 2;
					else if (b == 2)
						b = 1;
					btn |= (1 << b);
				}
				continue;
			}

			switch (hf->hf_usage[i]) {
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
				dx = hf->hf_value[i];
				break;
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
				dy = hf->hf_value[i];
				break;
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_WHEEL):
				has_wheel = 1;
				dw = -hf->hf_value[i];
				break;
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_TWHEEL):
				has_twheel = 1;
				dt = -hf->hf_value[i];
				break;
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Z):
				has_z = 1;
				dz = -hf->hf_value[i];
				break;
			}
		}
	}

	mi.operation = MOUSE_ACTION;
	mi.u.data.x = dx;
	mi.u.data.y = dy;
	mi.u.data.buttons = btn;
	if (has_wheel)
		mi.u.data.z = dw;
	else if (has_twheel)
		mi.u.data.z = dt;
	else if (has_z)
		mi.u.data.z = dz;
	else
		mi.u.data.z = 0;

	if (ioctl(md->cons_fd, CONS_MOUSECTL, &mi) < 0)
		syslog(LOG_ERR, "could not submit mouse data: ioctl failed:"
		    " %m");

	if (verbose > 1)
		printf("btn=%#x dx=%d dy=%d dw=%d\n", btn, dx, dy, dw);
}

void
mouse_driver_init(void)
{
	struct hid_driver hd;

	hd.hd_match = mouse_match;
	hd.hd_attach = mouse_attach;
	hd.hd_recv = mouse_recv;
	hd.hd_recv_raw = NULL;

	hid_driver_register(&hd);
}
