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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "../uvhid/uvhid_var.h"

#include "uhidd.h"

/*
 * General Virtual HID device.
 */

struct vhid_dev {
	int vd_fd;
	int vd_rid;
	char *vd_name;
	pthread_t vd_task;
};

static void *vhid_task(void *arg);

int
vhid_match(struct hid_appcol *ha)
{
	struct hid_interface *hi;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if (config_vhid_attach(hi) <= ATTACH_NO)
		return (HID_MATCH_NONE);

	return (HID_MATCH_GHID);
}

int
vhid_attach(struct hid_appcol *ha)
{
	struct hid_interface *hi;
	struct hid_report *hr;
	struct vhid_dev *vd;
	struct stat sb;
	struct usb_gen_descriptor ugd;
	int e;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if ((vd = calloc(1, sizeof(*vd))) == NULL) {
		syslog(LOG_ERR, "calloc failed in vhid_attach: %m");
		return (-1);
	}

	hid_appcol_set_private(ha, vd);

	/*
	 * Open a new virtual hid device.
	 */
	if ((vd->vd_fd = open("/dev/uvhidctl", O_RDWR)) < 0) {
		syslog(LOG_ERR, "%s[%d] could not open /dev/uvhidctl: %m",
		    basename(hi->dev), hi->ndx);
		if (errno == ENOENT)
			PRINT1(1, "uvhid.ko kernel moduel not loaded?\n")
		return (-1);
	}

	if (fstat(vd->vd_fd, &sb) < 0) {
		syslog(LOG_ERR, "%s[%d] fstat: /dev/uvhidctl: %m",
		    basename(hi->dev), hi->ndx);
		close(vd->vd_fd);
		return (-1);
	}

	if ((vd->vd_name = strdup(devname(sb.st_rdev, S_IFCHR))) == NULL) {
		syslog(LOG_ERR, "%s[%d] strdup failed: %m", basename(hi->dev),
		    hi->ndx);
		close(vd->vd_fd);
		return (-1);
	}

	PRINT1(1, "vhid device created: %s\n", devname(sb.st_rdev, S_IFCHR));

	/*
	 * Set the report descriptor of this virtual hid device.
	 */

	ugd.ugd_data = ha->ha_rdesc;
	ugd.ugd_actlen = ha->ha_rsz;

	if (ioctl(vd->vd_fd, USB_SET_REPORT_DESC, &ugd) < 0) {
		syslog(LOG_ERR, "%s[%d] ioctl(USB_SET_REPORT_DESC): %m",
		    basename(hi->dev), hi->ndx);
		return (-1);
	}

	/*
	 * Report id is set to the first one if the child device has multiple,
	 * or 0 if none. XXX This is so because the USB hid device ioctl
	 * USB_GET_REPORT_ID is not able to handle multiple report IDs.
	 */

	if (STAILQ_EMPTY(&ha->ha_hrlist))
		vd->vd_rid = 0;
	else {
		hr = STAILQ_FIRST(&ha->ha_hrlist);
		vd->vd_rid = hr->hr_id;
	}

	if (ioctl(vd->vd_fd, USB_SET_REPORT_ID, &vd->vd_rid) < 0) {
		syslog(LOG_ERR, "%s[%d] ioctl(USB_SET_REPORT_ID): %m",
		    basename(hi->dev), hi->ndx);
		return (-1);
	}

	/* Create hidctl read task. */
	e = pthread_create(&vd->vd_task, NULL, vhid_task, (void *) ha);
	if (e) {
		syslog(LOG_ERR, "%s[%d] pthread_create failed: %m",
		    basename(hi->dev), hi->ndx);
		close(vd->vd_fd);
		return (-1);
	}

	return (0);
}

void
vhid_recv_raw(struct hid_appcol *ha, uint8_t *buf, int len)
{
	struct hid_interface *hi;
	struct vhid_dev *vd;
	int i;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);
	vd = hid_appcol_get_private(ha);
	assert(vd != NULL);

	if (verbose > 1) {
		PRINT1(2, "%s received data:", vd->vd_name);
		for (i = 0; i < len; i++)
			printf(" %u", buf[i]);
		putchar('\n');
	}

	if (config_vhid_strip_id(hi) > 0) {
		buf++;
		len--;
	}

	if (write(vd->vd_fd, buf, len) < 0)
		syslog(LOG_ERR, "%s[%d] write failed: %m", basename(hi->dev),
		    hi->ndx);
}

static void *
vhid_task(void *arg)
{
	struct hid_interface *hi;
	struct hid_appcol *ha;
	struct vhid_dev *vd;
	char *buf;
	int i, len;

	ha = arg;
	assert(ha != NULL);
	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);
	vd = hid_appcol_get_private(ha);
	assert(vd != NULL);

	if ((buf = malloc(_TR_BUFSIZE)) == NULL) {
		syslog(LOG_ERR, "%s[%d] malloc failed: %m", basename(hi->dev),
		    hi->ndx);
		return (NULL);
	}

	for (;;) {
		len = read(vd->vd_fd, buf, sizeof(buf));
		if (len < 0) {
			if (errno != EINTR)
				break;
			continue;
		}
		if (verbose) {
			PRINT1(1, "%s[%d] vhid_task recevied:",
			    basename(hi->dev), hi->ndx);
			for (i = 0; i < len; i++)
				printf("%d ", buf[i]);
			putchar('\n');
		}
		if (vd->vd_rid != 0 && vd->vd_rid == buf[0])
			hid_appcol_xfer_raw_data(ha, vd->vd_rid, buf + 1,
			    len - 1);
		else
			hid_appcol_xfer_raw_data(ha, vd->vd_rid, buf, len);
	}

	free(buf);

	return (NULL);
}
