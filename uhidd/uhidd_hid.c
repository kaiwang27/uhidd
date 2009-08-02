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
#include <sys/stat.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "../uvhid/uvhid_var.h"

#include "uhidd.h"

int
hid_attach(struct hid_child *hc)
{
	struct hid_parent *hp;
	struct stat sb;
	struct usb_gen_descriptor ugd;
	int rid;

	hp = hc->parent;
	assert(hp != NULL);

	/*
	 * Open a new virtual hid device.
	 */

	if ((hc->u.hd.hidctl_fd = open("/dev/uvhidctl", O_RDWR)) < 0) {
		syslog(LOG_ERR, "%s[iface:%d][c%d:%s]=> could not open "
		    "/dev/uvhidctl: %m", hp->dev, hp->ndx, hc->ndx,
		    type_name(hc->type));
		if (verbose && errno == ENOENT)
			PRINT2("uvhid.ko kernel moduel not loaded?\n")
		return (-1);
	}

	if (fstat(hc->u.hd.hidctl_fd, &sb) < 0) {
		syslog(LOG_ERR, "%s[iface:%d][c%d:%s]=> fstat: "
		    "/dev/uvhidctl: %m", hp->dev, hp->ndx, hc->ndx,
		    type_name(hc->type));
		return (-1);
	}

	if ((hc->u.hd.name = strdup(devname(sb.st_rdev, S_IFCHR))) == NULL) {
		syslog(LOG_ERR, "%s[iface:%d][c%d:%s]=> strdup failed: %m",
		    hp->dev, hp->ndx, hc->ndx, type_name(hc->type));
		return (-1);
	}
		
	if (verbose)
		PRINT2("hid device name: %s\n", devname(sb.st_rdev, S_IFCHR));

	/*
	 * Set the report descriptor of this virtual hid device.
	 */

	ugd.ugd_data = hc->rdesc;
	ugd.ugd_actlen = hc->rsz;

	if (ioctl(hc->u.hd.hidctl_fd, USB_SET_REPORT_DESC, &ugd) < 0) {
		syslog(LOG_ERR, "%s[iface:%d][c%d:%s]=> "
		    "ioctl(USB_SET_REPORT_DESC): %m", hp->dev, hp->ndx,
		    hc->ndx, type_name(hc->type));
		return (-1);
	}

	/*
	 * Report id is set to the first one if the child device has multiple,
	 * or 0 if none. XXX This is so because the USB hid device ioctl
	 * USB_GET_REPORT_ID is not able to handle multiple report IDs.
	 */

	if (hc->nr == 0)
		rid = 0;
	else
		rid = hc->rid[0];

	if (ioctl(hc->u.hd.hidctl_fd, USB_SET_REPORT_ID, &rid) < 0) {
		syslog(LOG_ERR, "%s[iface:%d][c%d:%s]=> "
		    "ioctl(USB_SET_REPORT_ID): %m", hp->dev, hp->ndx,
		    hc->ndx, type_name(hc->type));
		return (-1);
	}

	return (0);
}

void
hid_recv(struct hid_child *hc, char *buf, int len)
{
	struct hid_parent *hp;
	int i;

	hp = hc->parent;
	assert(hp != NULL);

	if (verbose) {
		PRINT2("%s received data:", hc->u.hd.name);
		for (i = 0; i < len; i++)
			printf(" %d", buf[i]);
		putchar('\n');
	}

	if (write(hc->u.hd.hidctl_fd, buf, len) < 0)
		syslog(LOG_ERR, "%s[iface:%d][c%d:%s]=> write failed: %m",
		    hp->dev, hp->ndx, hc->ndx, type_name(hc->type));
}
