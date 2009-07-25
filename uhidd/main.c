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
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <libusb20.h>
#include <libusb20_desc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dev/usb/usbhid.h>

#include "extern.h"

int debug = 0;
int detach = 0;
STAILQ_HEAD(, hid_parent) hplist;

static void	usage(void);
static void	find_and_attach(struct libusb20_backend *backend,
    const char *dev);
static void	attach_dev(const char *dev, struct libusb20_device *pdev);
static void	attach_iface(const char *dev, struct libusb20_device *pdev,
    struct libusb20_interface *iface, int i);
static void	attach_hid_parent(struct hid_parent *hp);
static void	attach_hid_child(struct hid_child *hc);
static void	child_recv(struct hid_child *hc, char *buf, int len);
static void	dispatch(struct hid_parent *hp, char *buf, int len);
static void	repair_report_desc(struct hid_child *hc);
static void	*start_hid_parent(void *arg);

int
main(int argc, char **argv)
{
	struct hid_parent *hp;
	struct libusb20_backend *backend;
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

	backend = libusb20_be_alloc_default();
	if (backend == NULL)
		errx(1, "can not alloc backend");

	STAILQ_INIT(&hplist);
	find_and_attach(backend, *argv);
	libusb20_be_free(backend);

	if (STAILQ_EMPTY(&hplist))
		exit(0);
	STAILQ_FOREACH(hp, &hplist, next)
		pthread_create(&hp->thread, NULL, start_hid_parent, (void *)hp);
	STAILQ_FOREACH(hp, &hplist, next)
		pthread_join(hp->thread, NULL);

	printf("main terminated\n");
	exit(0);
}

static void
find_and_attach(struct libusb20_backend *backend, const char *dev)
{
	struct libusb20_device *pdev;
	unsigned int bus, addr;

	if (sscanf(dev, "/dev/ugen%u.%u", &bus, &addr) < 2)
		errx(1, "%s not found", dev);

	pdev = NULL;
	while ((pdev = libusb20_be_device_foreach(backend, pdev)) != NULL) {
		if (bus == libusb20_dev_get_bus_number(pdev) &&
		    addr == libusb20_dev_get_address(pdev))
			attach_dev(dev, pdev);
	}
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
	struct libusb20_endpoint *ep;
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

	/* Find the IN interrupt endpoint. */
	for (j = 0; j < iface->num_endpoints; j++) {
		ep = &iface->endpoints[j];
		if ((ep->desc.bmAttributes & LIBUSB20_TRANSFER_TYPE_MASK) ==
		    LIBUSB20_TRANSFER_TYPE_INTERRUPT &&
		    ((ep->desc.bEndpointAddress & LIBUSB20_ENDPOINT_DIR_MASK) ==
		    LIBUSB20_ENDPOINT_IN)) {
			hp->ep = ep->desc.bEndpointAddress;
			hp->pkt_sz = ep->desc.wMaxPacketSize;
			if (debug) {
				printf("%s: iface(%d) find IN interrupt ep:"
				    " %#x", dev, ndx, hp->ep);
				printf(" packet_size=%#x\n", hp->pkt_sz);
			}
			break;
		}
	}
	if (hp->ep == 0) {
		printf("%s: iface(%d) does not have IN interrupt ep\n", dev,
		    ndx);
		free(hp);
		return;
	}

	STAILQ_INSERT_TAIL(&hplist, hp, next);

	attach_hid_parent(hp);
}

static void
repair_report_desc(struct hid_child *hc)
{
	struct hid_parent *hp;
	struct hid_child *phc;
	unsigned int bTag, bType, bSize;
	unsigned char *b, *pos;
	hid_item_t env;
	int bytes, i;

	hp = hc->parent;
	assert(hp != NULL);

	/* First child does not need repairing. */
	phc = STAILQ_FIRST(&hp->hclist);
	if (phc == hc)
		return;

	while (STAILQ_NEXT(phc, next) != hc)
		phc = STAILQ_NEXT(phc, next);
	env = phc->env;

	/* First step: insert USAGE PAGE before USAGE if need. */
	b = hc->rdesc;
	while (b < hc->rdesc + hc->rsz) {
		pos = b;
		bSize = *b++;
		if (bSize == 0xfe) {
			/* long item */
			bSize = *b++;
			bTag = *b++;
			b += bSize;
			continue;
		}

		/* short item */
		bTag = bSize >> 4;
		bType = (bSize >> 2) & 3;
		bSize &= 3;
		if (bSize == 3)
			bSize = 4;
		b += bSize;

		/* If we found a USAGE PAGE, no need to continue. */
		if (bType == 1 && bTag == 0)
			break;

		/* Check if it is USAGE item. */
		if (bType == 2 && bTag == 0) {
			/*
			 * We need to insert USAGE PAGE before this
			 * USAGE. USAGE PAGE needs 3-byte space.
			 */
			if (env._usage_page < 256)
				bytes = 2;
			else
				bytes = 3;
			memmove(pos + bytes, pos, hc->rsz - (pos - hc->rdesc));
			pos[0] = (1 << 2) | (bytes - 1);
			pos[1] = env._usage_page & 0xff;
			if (bytes == 3)
				pos[2] = (env._usage_page & 0xff00) >> 8;
			hc->rsz += bytes;
			if (debug > 1) {
				printf("\tnr=%d repair: insert USAGE PAGE",
				    hc->nr);
				for (i = 0; i < bytes; i++)
					printf(" 0x%02x", pos[i]);
				putchar('\n');
			}
			break;
		}

	}

	/*
	 * Second step: insert missing REPORT COUNT before the first main
	 * item.
	 */
	b = hc->rdesc;
	while (b < hc->rdesc + hc->rsz) {
		pos = b;
		bSize = *b++;
		if (bSize == 0xfe) {
			/* long item */
			bSize = *b++;
			bTag = *b++;
			b += bSize;
			continue;
		}

		/* short item */
		bTag = bSize >> 4;
		bType = (bSize >> 2) & 3;
		bSize &= 3;
		if (bSize == 3)
			bSize = 4;
		b += bSize;

		/* Check if we already got REPORT COUNT. */
		if (bType == 1 && bTag == 9)
			break;

		/* Check if it is INPUT, OUTPUT or FEATURE. */
		if (bType == 0 && (bTag == 8 || bTag == 9 || bTag == 11)) {
			if (env.report_count < 256)
				bytes = 2;
			else if (env.report_count < 65536)
				bytes = 3;
			else
				bytes = 5;
			memmove(pos + bytes, pos, hc->rsz - (pos - hc->rdesc));
			if (bytes < 5)
				pos[0] = (9 << 4) | (1 << 2) | (bytes - 1);
			else
				pos[0] = (9 << 4) | (1 << 2) | 3;
			pos[1] = env.report_count & 0xff;
			if (bytes > 2)
				pos[2] = (env.report_count & 0xff00) >> 8;
			if (bytes > 3) {
				pos[3] = (env.report_count & 0xff0000) >> 16;
				pos[4] = env.report_count >> 24;
			}
			hc->rsz += bytes;
			if (debug > 1) {
				printf("\tnr=%d repair: insert REPORT COUNT",
				    hc->nr);
				for (i = 0; i < bytes; i++)
					printf(" 0x%02x", pos[i]);
				putchar('\n');
			}
			break;
		}
	}
}


static void
attach_hid_parent(struct hid_parent *hp)
{
	struct hid_child *hc;
	hid_parser_t p;
	hid_data_t d;
	hid_item_t h, ch;
	int rid[_MAX_REPORT_IDS];
	int i, nr, start, end, lend;

	/* Check how many children we have. */
	p = hid_parser_alloc(hp->rdesc, hp->rsz);
	nr = hid_get_report_id_num(p);
	hid_get_report_ids(p, rid, nr);
	if (debug) {
		printf("%s: iface(%d) nr=%d ", hp->dev, hp->ndx, nr);
		printf("rid=(");
		for (i = 0; i < nr; i++) {
			printf("%d", rid[i]);
			if (i < nr - 1)
				putchar(',');
		}
		printf(")\n");
	}
	if (debug > 1) {
		if (nr == 0)
			printf("\tid(%2d): input(%d) output(%d) feature(%d)\n",
			    0,
			    hid_report_size(p, hid_input, 0),
			    hid_report_size(p, hid_output, 0),
			    hid_report_size(p, hid_feature, 0));
		for (i = 0; i < nr; i++)
			printf("\tid(%2d): input(%d) output(%d) feature(%d)\n",
			    rid[i],
			    hid_report_size(p, hid_input, rid[i]),
			    hid_report_size(p, hid_output, rid[i]),
			    hid_report_size(p, hid_feature, rid[i]));
	}
	memset(&h, 0, sizeof(h));
	memset(&ch, 0, sizeof(ch));
	start = end = lend = 0;
	for (d = hid_start_parse(p, 1<<hid_collection | 1<<hid_endcollection);
	     hid_get_item(d, &h, -1); ) {
		if (h.kind == hid_collection && h.collection == 0x01)
			ch = h;
		if (h.kind == hid_endcollection &&
		    ch.collevel - 1 == h.collevel) {
			end = d->p - d->start;
			hc = calloc(1, sizeof(*hc));
			if (hc == NULL)
				err(1, "calloc");
			hc->cons_fd = -1;
			hc->parent = hp;
			hc->env = h;
			start = lend;
			lend = end;
			hc->rsz = end - start;
			if (hc->rsz <= 0 || hc->rsz > _MAX_RDESC_SIZE)
				errx(1, "%s: iface(%d) invalid hid child"
				    " report desc range [%d-%d]", hp->dev,
				    hp->ndx, start, end);
			memcpy(hc->rdesc, &hp->rdesc[start], hp->rsz);
			STAILQ_INSERT_TAIL(&hp->hclist, hc, next);
			printf("%s: iface(%d) [%d-%d] ", hp->dev, hp->ndx,
			    start, end);
			if (ch.usage == HID_USAGE2(HUP_GENERIC_DESKTOP,
			    HUG_MOUSE)) {
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

			/*
			 * Here we need to "repair" some child report desc
			 * before we can give it to the parser, since there
			 * are possible global items(i.e. environment) missing.
			 */
			repair_report_desc(hc);

			/*
			 * Attach child.
			 */
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
	if (debug > 1) {
		printf("\tnr=%d ", hc->nr);
		printf("rid=(");
		for (i = 0; i < hc->nr; i++) {
			printf("%d", hc->rid[i]);
			if (i < hc->nr - 1)
				putchar(',');
		}

		printf(")\n");
		if (hc->nr == 0)
			printf("\tid(%2d): input(%d) output(%d) feature(%d)\n",
			    0,
			    hid_report_size(p, hid_input, 0),
			    hid_report_size(p, hid_output, 0),
			    hid_report_size(p, hid_feature, 0));
		for (i = 0; i < hc->nr; i++)
			printf("\tid(%2d): input(%d) output(%d) feature(%d)\n",
			    hc->rid[i],
			    hid_report_size(p, hid_input, hc->rid[i]),
			    hid_report_size(p, hid_output, hc->rid[i]),
			    hid_report_size(p, hid_feature, hc->rid[i]));
		if (debug > 2)
			dump_report_desc(hc->rdesc, hc->rsz);
	}
	hc->p = p;

	switch (hc->type) {
	case UHIDD_MOUSE:
		mouse_attach(hc);
		break;
	case UHIDD_KEYBOARD:
/* 		kbd_attach(hc); */
		break;
	case UHIDD_HID:
/* 		hid_attach(hc); */
		break;
	default:
		/* Internal Error! */
		assert(0);
	}
}

static void *
start_hid_parent(void *arg)
{
	struct hid_parent *hp;
	struct libusb20_backend *backend;
	struct libusb20_transfer *xfer;
	struct libusb20_device *pdev;
	unsigned int bus, addr;
	char buf[4096];
	uint32_t actlen;
	uint8_t x;
	int e, i;

	hp = arg;
	assert(hp != NULL);

	if (debug)
		printf("%s: iface(%d) hid parent started\n", hp->dev, hp->ndx);

	if (sscanf(hp->dev, "/dev/ugen%u.%u", &bus, &addr) < 2)
		errx(1, "%s not found", hp->dev);

	backend = libusb20_be_alloc_default();
	pdev = NULL;
	while ((pdev = libusb20_be_device_foreach(backend, pdev)) != NULL) {
		if (bus == libusb20_dev_get_bus_number(pdev) &&
		    addr == libusb20_dev_get_address(pdev)) {
			e = libusb20_dev_open(pdev, 32);
			if (e != 0) {
				printf("%s: libusb20_dev_open failed\n",
				    hp->dev);
				return (NULL);
			}
			break;
		}
	}
	if (pdev == NULL)
		errx(1, "%s not found", hp->dev);
	hp->pdev = pdev;

	x = (hp->ep & LIBUSB20_ENDPOINT_ADDRESS_MASK) * 2;
	x |= 1;
	xfer = libusb20_tr_get_pointer(hp->pdev, x);
	if (xfer == NULL) {
		printf("%s: iface(%d) libusb20_tr_get_pointer failed\n",
		    hp->dev, hp->ndx);
		goto parent_end;
	}
		
	e = libusb20_tr_open(xfer, 4096, 1, hp->ep);
	if (e == LIBUSB20_ERROR_BUSY) {
		printf("%s: iface(%d) xfer already opened\n", hp->dev,
		    hp->ndx);
	} else if (e) {
		printf("%s: iface(%d) libusb20_tr_open failed\n",
		    hp->dev, hp->ndx);
		goto parent_end;
	}

	for (;;) {

		if (libusb20_tr_pending(xfer)) {
			printf("%s: iface(%d) tr pending\n", hp->dev, hp->ndx);
			continue;
		}
		
		libusb20_tr_setup_intr(xfer, buf, hp->pkt_sz, 0);

		libusb20_tr_start(xfer);

		for (;;) {
			if (libusb20_dev_process(hp->pdev) != 0) {
				printf("%s: iface(%d) device detached?\n",
				    hp->dev, hp->ndx);
				goto parent_end;
			}
			if (libusb20_tr_pending(xfer) == 0)
				break;
			libusb20_dev_wait_process(hp->pdev, -1);
		}

		switch (libusb20_tr_get_status(xfer)) {
		case 0:
			actlen = libusb20_tr_get_actual_length(xfer);
			if (debug > 1) {
				printf("%s: iface(%d) received data(%u): ",
				    hp->dev, hp->ndx, actlen);
				for (i = 0; (uint32_t)i < actlen; i++)
					printf("%02d ", buf[i]);
				putchar('\n');
			}
			dispatch(hp, buf, actlen);
			break;
		case LIBUSB20_TRANSFER_TIMED_OUT:
			if (debug)
				printf("%s: iface(%d) TIMED OUT\n", hp->dev,
				    hp->ndx);
			break;
		default:
			if (debug)
				printf("%s: iface(%d) transfer error\n",
				    hp->dev, hp->ndx);
			break;
		}
	}

parent_end:

	if (debug)
		printf("%s: iface(%d) hid parent exit\n", hp->dev, hp->ndx);

	return (NULL);
}

static void
dispatch(struct hid_parent *hp, char *buf, int len)
{
	struct hid_child *hc;
	int i;

	if (STAILQ_EMPTY(&hp->hclist)) {
		printf("%s: iface(%d) no hid child device exist, "
		    "packet discarded.", hp->dev, hp->ndx);
		return;
	}

	STAILQ_FOREACH(hc, &hp->hclist, next) {
		if (hc->nr == 0) {
			/*
			 * If the child has no report IDs at all, just dispatch
			 * this packet to it.
			 */
			child_recv(hc, buf, len);
			return;
		}
		for (i = 0; i < hc->nr; i++) {
			if (hc->rid[i] == buf[0]) {
				child_recv(hc, buf, len);
				return;
			}
		}
	}

	/*
	 * If there are no matching child device, the report desc is probably
	 * broken. Just send the packet to the first child.
	 */
	if (hc == NULL) {
		if (debug)
			printf("%s: iface(%d) packet doesn't belong to any"
			    " hid child, packet sent to the first child.",
			    hp->dev, hp->ndx);
		hc = STAILQ_FIRST(&hp->hclist);
		child_recv(hc, buf, len);
	}
}

static void
child_recv(struct hid_child *hc, char *buf, int len)
{

	switch (hc->type) {
	case UHIDD_MOUSE:
		mouse_recv(hc, buf, len);
		break;
	case UHIDD_KEYBOARD:
/* 		kbd_recv(hc, buf, len); */
		break;
	case UHIDD_HID:
/* 		hid_recv(hc, buf, len); */
		break;
	default:
		/* Internal Error! */
		assert(0);
	}
}

static void
usage(void)
{

	fprintf(stderr, "usage: uhidd [-dk] /dev/ugen%%u.%%u\n");
	exit(1);
}
