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
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <err.h>
#include <fcntl.h>
#include <libusb20.h>
#include <libusb20_desc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dev/usb/usbhid.h>

#include "extern.h"

struct hid_child;

struct hid_parent {
	const char			*dev;
	struct libusb20_device		*pdev;
	struct libusb20_interface	*iface;
	int				 ndx;
	unsigned char			 rdesc[_MAX_RDESC_SIZE];
	int				 rsz;
	pthread_t			 thread;
	STAILQ_HEAD(, hid_child)	 hclist;
	STAILQ_ENTRY(hid_parent)	 next;
};

struct hid_child {
	struct hid_parent	*parent;
	enum uhidd_ctype	 type;
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
	STAILQ_ENTRY(hid_child)	 next;
};

int debug = 0;
int detach = 0;
STAILQ_HEAD(, hid_parent) hplist;

static void	usage(void);
static void	find_dev(const char *dev);
static void	attach_dev(const char *dev, struct libusb20_device *pdev);
static void	attach_iface(const char *dev, struct libusb20_device *pdev,
    struct libusb20_interface *iface, int i);
static void	attach_hid_parent(struct hid_parent *hp);
static void	attach_hid_child(struct hid_child *hc);
int
main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "dk")) != -1) {
		switch(opt) {
		case 'd':
			debug++;
			break;
		case 'k':
			detach++;
			break;
		default:
			usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (*argv == NULL)
		usage();

	STAILQ_INIT(&hplist);

	find_dev(*argv);

	exit(0);
}

static void
find_dev(const char *dev)
{
	struct libusb20_backend *backend;
	struct libusb20_device *pdev;
	unsigned int bus, addr;

	if (sscanf(dev, "/dev/ugen%u.%u", &bus, &addr) < 2)
		errx(1, "%s not found", dev);

	backend = libusb20_be_alloc_default();
	if (backend == NULL)
		errx(1, "can not alloc backend");

	pdev = NULL;
	while ((pdev = libusb20_be_device_foreach(backend, pdev)) != NULL) {
		if (bus == libusb20_dev_get_bus_number(pdev) &&
		    addr == libusb20_dev_get_address(pdev))
			attach_dev(dev, pdev);
	}

	libusb20_be_free(backend);
}

static void
attach_dev(const char *dev, struct libusb20_device *pdev)
{
	struct libusb20_config *config;
	struct libusb20_interface *iface;
	int cndx, e, i;

	e = libusb20_dev_open(pdev, 32);
	if (e != 0) {
		printf("%s: libusb20_dev_open failed\n", dev);
		return;
	}
		
	/* Get current configuration. */
	cndx = libusb20_dev_get_config_index(pdev);
	config = libusb20_dev_alloc_config(pdev, cndx);
	if (config == NULL) {
		printf("%s: can not alloc config", dev);
		return;
	}

	/* Iterate each interface. */
	for (i = 0; i < config->num_interface; i++) {
		iface = &config->interface[i];
		if (iface->desc.bInterfaceClass == LIBUSB20_CLASS_HID) {
			printf("%s: has HID interface %d\n", dev, i);
			attach_iface(dev, pdev, iface, i);
		}
	}

	free(config);
}

static void
attach_iface(const char *dev, struct libusb20_device *pdev,
    struct libusb20_interface *iface, int ndx)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	struct hid_parent *hp;
	unsigned char rdesc[16384];
	int desc, ds, e, j, pos, size;
	uint16_t actlen;

#if 0
	/* XXX ioctl currently unimplemented */
	if (libusb20_dev_kernel_driver_active(pdev, ndx)) {
		printf("%s: iface(%d) kernel driver is active\n", dev, ndx);
		/* TODO probably detach the kernel driver here. */
	} else
		printf("%s: iface(%d) kernel driver is not active\n", dev, ndx);
#endif

	/* Get report descriptor. */
	pos = 0;
	size = iface->extra.len;
	while (size > 2) {
		if (libusb20_me_get_1(&iface->extra, pos + 1) == LIBUSB20_DT_HID)
			break;
		size -= libusb20_me_get_1(&iface->extra, pos);
		pos += libusb20_me_get_1(&iface->extra, pos);
	}
	if (size <= 2)
		return;
	desc = pos + 6;
	for (j = 0; j < libusb20_me_get_1(&iface->extra, pos + 5);
	     j++, desc += j * 3) {
		if (libusb20_me_get_1(&iface->extra, desc) ==
		    LIBUSB20_DT_REPORT)
			break;
	}
	if (j >= libusb20_me_get_1(&iface->extra, pos + 5))
		return;
	ds = libusb20_me_get_2(&iface->extra, desc + 1);
	printf("%s: iface(%d) report size = %d\n", dev, ndx, ds);
	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);
	req.bmRequestType = LIBUSB20_ENDPOINT_IN |
		LIBUSB20_REQUEST_TYPE_STANDARD | LIBUSB20_RECIPIENT_INTERFACE;
	req.bRequest = LIBUSB20_REQUEST_GET_DESCRIPTOR;
	req.wValue = LIBUSB20_DT_REPORT << 8;
	req.wIndex = ndx;
	req.wLength = ds;
	e = libusb20_dev_request_sync(pdev, &req, rdesc, &actlen, 0, 0);
	if (e) {
		printf("%s: iface(%d) libusb20_dev_request_sync failed\n",
		    dev, ndx);
		return;
	}

	hp = calloc(1, sizeof(*hp));
	if (hp == NULL)
		err(1, "calloc");
	hp->dev = dev;
	hp->pdev = pdev;
	hp->iface = iface;
	hp->ndx = ndx;
	memcpy(hp->rdesc, rdesc, actlen);
	hp->rsz = actlen;
	STAILQ_INIT(&hp->hclist);
	STAILQ_INSERT_TAIL(&hplist, hp, next);

	attach_hid_parent(hp);
}

static void
attach_hid_parent(struct hid_parent *hp)
{
	struct hid_child *hc;
	hid_parser_t p;
	hid_data_t d;
	hid_item_t h, ch;
	int rid[_MAX_REPORT_IDS];
	int i, nr, start, end;

	/* Check how many children we have. */
	p = hid_parser_alloc(hp->rdesc, hp->rsz);
	nr = hid_get_report_id_num(p);
	hid_get_report_ids(p, rid, nr);
	printf("%s: iface(%d) nr=%d ", hp->dev, hp->ndx, nr);
	printf("rid=(");
	for (i = 0; i < nr; i++) {
		printf("%d", rid[i]);
		if (i < nr - 1)
			putchar(',');
	}
	printf(")\n");
	if (nr == 0)
		printf("\tid(%2d): input(%d) output(%d) feature(%d)\n", 0,
		    hid_report_size(p, hid_input, 0),
		    hid_report_size(p, hid_output, 0),
		    hid_report_size(p, hid_feature, 0));
	for (i = 0; i < nr; i++)
		printf("\tid(%2d): input(%d) output(%d) feature(%d)\n", rid[i],
		    hid_report_size(p, hid_input, rid[i]),
		    hid_report_size(p, hid_output, rid[i]),
		    hid_report_size(p, hid_feature, rid[i]));
	memset(&h, 0, sizeof(h));
	memset(&ch, 0, sizeof(ch));
	start = end = 0;
	for (d = hid_start_parse(p, 1<<hid_collection | 1<<hid_endcollection);
	     hid_get_item(d, &h, -1); ) {
		if (h.kind == hid_collection && h.collection == 0x01) {
			start = d->p - d->start;
			ch = h;
			printf("%s: iface(%d) application start offset %d\n",
			    hp->dev, hp->ndx, start);
		}
		if (h.kind == hid_endcollection &&
		    ch.collevel - 1 == h.collevel) {
			end = d->p - d->start - 1;
			hc = calloc(1, sizeof(*hc));
			if (hc == NULL)
				err(1, "calloc");
			hc->parent = hp;
			hc->rsz = end - start;
			if (hc->rsz <= 0 || hc->rsz > _MAX_RDESC_SIZE)
				errx(1, "%s: iface(%d) invalid hid child"
				    " report desc range [%d-%d]", hp->dev,
				    hp->ndx, start, end);
			memcpy(hc->rdesc, &hp->rdesc[start], hp->rsz);
			STAILQ_INSERT_TAIL(&hp->hclist, hc, next);
			printf("%s: iface(%d) application end offset %d\n",
			    hp->dev, hp->ndx, end);
			printf("%s: iface(%d) [%d-%d] ", hp->dev, hp->ndx,
			    start, end);
			if (ch.usage ==
			    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE)) {
				printf("mouse found\n");
				hc->type = UHIDD_MOUSE;
			} else if (ch.usage ==
			    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD)) {
				printf("keyboard found\n");
				hc->type = UHIDD_KEYBOARD;
			} else {
				printf("general hid device found\n");
				hc->type = UHIDD_HID;
			}
			attach_hid_child(hc);
		}
	}
	hid_end_parse(d);
	hid_parser_free(p);
}

static void
attach_hid_child(struct hid_child *hc)
{
	hid_parser_t p;
	int i;

	p = hid_parser_alloc(hc->rdesc, hc->rsz);
	hc->nr = p->nr;
	memcpy(hc->rid, p->rid, p->nr * sizeof(int));
	printf("\tnr=%d ", hc->nr);
	printf("rid=(");
	for (i = 0; i < hc->nr; i++) {
		printf("%d", hc->rid[i]);
		if (i < hc->nr - 1)
			putchar(',');
	}
	/*
	 * TODO well here we need to "repair" some child report desc before
	 * we can give it to the parser, since there are possible global
	 * items(i.e. environment) missing.
	 */
	printf(")\n");
	if (hc->nr == 0)
		printf("\tid(%2d): input(%d) output(%d) feature(%d)\n", 0,
		    hid_report_size(p, hid_input, 0),
		    hid_report_size(p, hid_output, 0),
		    hid_report_size(p, hid_feature, 0));
	for (i = 0; i < hc->nr; i++)
		printf("\tid(%2d): input(%d) output(%d) feature(%d)\n",
		    hc->rid[i],
		    hid_report_size(p, hid_input, hc->rid[i]),
		    hid_report_size(p, hid_output, hc->rid[i]),
		    hid_report_size(p, hid_feature, hc->rid[i]));

	/* TODO open hidctl device here. */
}

static void
usage(void)
{

	fprintf(stderr, "usage: uhidd [-dk] /dev/ugen%%u.%%u\n");
	exit(1);
}
