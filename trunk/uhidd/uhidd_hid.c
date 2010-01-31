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

/*
 * General HID device.
 */

struct hid_dev {
	int hidctl_fd;
	char *name;
};

static int
hid_match(struct hid_appcol *ha)
{
	struct hid_parent *hp;

	hp = hid_appcol_get_interface_private(ha);
	assert(hp != NULL);

	if (!config_attach_hid(hp))
		return (HID_MATCH_NONE);

	return (HID_MATCH_GHID);
}

static int
hid_attach(struct hid_appcol *ha)
{
	struct hid_parent *hp;
	struct hid_report *hr;
	struct hid_dev *hd;
	struct stat sb;
	struct usb_gen_descriptor ugd;
	int rid;

	hp = hid_appcol_get_interface_private(ha);
	assert(hp != NULL);

	if ((hd = calloc(1, sizeof(*hd))) == NULL) {
		syslog(LOG_ERR, "calloc failed in hid_attach: %m");
		return (-1);
	}

	hid_appcol_set_private(ha, hd);

	/*
	 * Open a new virtual hid device.
	 */
	if ((hd->hidctl_fd = open("/dev/uvhidctl", O_RDWR)) < 0) {
		syslog(LOG_ERR, "%s[iface:%d]=> could not open "
		    "/dev/uvhidctl: %m", hp->dev, hp->ndx);
		if (verbose && errno == ENOENT)
			PRINT1("uvhid.ko kernel moduel not loaded?\n")
		return (-1);
	}

	if (fstat(hd->hidctl_fd, &sb) < 0) {
		syslog(LOG_ERR, "%s[iface:%d]=> fstat: "
		    "/dev/uvhidctl: %m", hp->dev, hp->ndx);
		return (-1);
	}

	if ((hd->name = strdup(devname(sb.st_rdev, S_IFCHR))) == NULL) {
		syslog(LOG_ERR, "%s[iface:%d]=> strdup failed: %m", hp->dev,
		    hp->ndx);
		return (-1);
	}

	if (verbose)
		PRINT1("hid device name: %s\n", devname(sb.st_rdev, S_IFCHR));

	/*
	 * Set the report descriptor of this virtual hid device.
	 */

	ugd.ugd_data = ha->ha_rdesc;
	ugd.ugd_actlen = ha->ha_rsz;

	if (ioctl(hd->hidctl_fd, USB_SET_REPORT_DESC, &ugd) < 0) {
		syslog(LOG_ERR, "%s[iface:%d]=> "
		    "ioctl(USB_SET_REPORT_DESC): %m", hp->dev, hp->ndx);
		return (-1);
	}

	/*
	 * Report id is set to the first one if the child device has multiple,
	 * or 0 if none. XXX This is so because the USB hid device ioctl
	 * USB_GET_REPORT_ID is not able to handle multiple report IDs.
	 */

	if (STAILQ_EMPTY(&ha->ha_hrlist))
		rid = 0;
	else {
		hr = STAILQ_FIRST(&ha->ha_hrlist);
		rid = hr->hr_id;
	}

	if (ioctl(hd->hidctl_fd, USB_SET_REPORT_ID, &rid) < 0) {
		syslog(LOG_ERR, "%s[iface:%d]=> ioctl(USB_SET_REPORT_ID): %m",
		    hp->dev, hp->ndx);
		return (-1);
	}

	return (0);
}

static void
hid_recv_raw(struct hid_appcol *ha, uint8_t *buf, int len)
{
	struct hid_parent *hp;
	struct hid_dev *hd;
	int i;

	hp = hid_appcol_get_interface_private(ha);
	assert(hp != NULL);
	hd = hid_appcol_get_private(ha);
	assert(hd != NULL);

	if (verbose) {
		PRINT1("%s received data:", hd->name);
		for (i = 0; i < len; i++)
			printf(" %u", buf[i]);
		putchar('\n');
	}

	if (config_strip_report_id(hp)) {
		buf++;
		len--;
	}

	if (write(hd->hidctl_fd, buf, len) < 0)
		syslog(LOG_ERR, "%s[iface:%d]=> write failed: %m", hp->dev,
		    hp->ndx);
}

void
hid_driver_init(void)
{
	struct hid_driver hd;

	hd.hd_match = hid_match;
	hd.hd_attach = hid_attach;
	hd.hd_recv = NULL;
	hd.hd_recv_raw = hid_recv_raw;

	hid_driver_register(&hd);
}
