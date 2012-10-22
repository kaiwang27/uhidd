/*-
 * Copyright (c) 2009, 2010, 2012 Kai Wang
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libusb20.h>
#include <libusb20_desc.h>
#include <libutil.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "uhidd.h"

int verbose = 0;

static int detach = 1;
static int hidump = 0;
static struct pidfh *pfh = NULL;
static STAILQ_HEAD(, hid_interface) hilist;

static void	usage(void);
static void	version(void);
static int	find_device(const char *dev);
static int	open_device(const char *dev, struct libusb20_device *pdev);
static void	open_iface(const char *dev, struct libusb20_device *pdev,
		    struct libusb20_interface *iface, int i);
static int	alloc_hid_interface_be(struct hid_interface *hi);
static void	*start_hid_interface(void *arg);
static int	hid_set_report(void *context, int report_id, char *buf,
		    int len);
static void	create_runtime_dir(void);
static void	remove_runtime_dir(void);
static void	sighandler(int sig __unused);
static void	terminate(int eval);

int
main(int argc, char **argv)
{
	struct hid_interface *hi;
	char *pid_file;
	pid_t otherpid;
	int e, eval, opt;

	eval = 0;

	while ((opt = getopt(argc, argv, "c:dDhH:kmosuUvV")) != -1) {
		switch(opt) {
		case 'c':
			config_file = optarg;
			break;
		case 'd':
			detach = 0;
			break;
		case 'D':
			hidump = 1;
			detach = 0;
			break;
		case 'h':
			clconfig.vhid_attach = 1;
			break;
		case 'H':
			clconfig.vhid_devname = optarg;
			break;
		case 'k':
			clconfig.kbd_attach = 1;
			break;
		case 'm':
			clconfig.mouse_attach = 1;
			break;
		case 'o':
			clconfig.cc_attach = 1;
			break;
		case 's':
			clconfig.vhid_strip_id = 1;
			break;
		case 'u':
			clconfig.detach_kernel_driver = 1;
			break;
		case 'U':
			clconfig.forced_attach = 1;
			break;
		case 'v':
			detach = 0;
			verbose++;
			break;
		case 'V':
			version();
		default:
			usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (*argv == NULL)
		usage();

	openlog("uhidd", LOG_PID|LOG_PERROR|LOG_NDELAY, LOG_USER);

	config_init();

	/* Check that another uhidd isn't already attached to the device. */
	if (asprintf(&pid_file, "/var/run/uhidd.%s.pid", basename(*argv)) < 0) {
		syslog(LOG_ERR, "asprintf failed: %m");
		exit(1);
	}
	pfh = pidfile_open(pid_file, 0600, &otherpid);
	if (pfh == NULL) {
		if (errno == EEXIST) {
			syslog(LOG_ERR, "uhidd already running on %s, pid: %d.",
			    *argv, otherpid);
			exit(1);
		}
		syslog(LOG_WARNING, "cannot open or create pidfile");
	}
	free(pid_file);

	if (config_read_file() < 0) {
		if (verbose)
			syslog(LOG_WARNING, "proceed without configuration"
			    " file");
	}

	if (detach) {
		if (daemon(0, 0) < 0) {
			syslog(LOG_ERR, "daemon failed: %m");
			exit(1);
		}
	}

	signal(SIGTERM, sighandler);
	signal(SIGINT, sighandler);

	/* Write pid file. */
	pidfile_write(pfh);

	STAILQ_INIT(&hilist);

	if (find_device(*argv) < 0) {
		eval = 1;
		goto uhidd_end;
	}

	if (STAILQ_EMPTY(&hilist))
		goto uhidd_end;

	create_runtime_dir();

	STAILQ_FOREACH(hi, &hilist, next) {
		if (alloc_hid_interface_be(hi) < 0)
			goto uhidd_end;
		hi->hp = hid_parser_alloc(hi->rdesc, hi->rsz, hi);
		if (hi->hp == NULL) {
			syslog(LOG_ERR, "%s: hid_parser alloc failed",
			    hi->dev);
			continue;
		}
		hid_parser_set_write_callback(hi->hp, hid_set_report);
		hid_parser_attach_drivers(hi->hp);
	}

	STAILQ_FOREACH(hi, &hilist, next) {
		if (hi->hp && hi->hp->hp_attached > 0) {
			e = pthread_create(&hi->thread, NULL,
			    start_hid_interface, (void *)hi);
			if (e) {
				syslog(LOG_ERR, "pthread_create failed: %m");
				goto uhidd_end;
			}
		}
	}
	STAILQ_FOREACH(hi, &hilist, next) {
		if (hi->hp && hi->hp->hp_attached > 0) {
			e = pthread_join(hi->thread, NULL);
			if (e) {
				syslog(LOG_ERR, "pthread_join failed: %m");
				goto uhidd_end;
			}
		}
	}

uhidd_end:

	terminate(eval);
}

static void
create_runtime_dir(void)
{
	struct hid_interface *hi;
	char dpath[PATH_MAX];

	hi = STAILQ_FIRST(&hilist);
	if (hi != NULL && hi->dev != NULL) {
		snprintf(dpath, sizeof(dpath), "/var/run/uhidd.%s",
		    basename(hi->dev));
		mkdir(dpath, 0755);
	}
}

static void
remove_runtime_dir(void)
{
	struct hid_interface *hi;
	struct dirent *d;
	DIR *dir;
	char dpath[PATH_MAX], fpath[PATH_MAX];

	hi = STAILQ_FIRST(&hilist);
	if (hi != NULL && hi->dev != NULL) {
		snprintf(dpath, sizeof(dpath), "/var/run/uhidd.%s",
		    basename(hi->dev));
		if ((dir = opendir(dpath)) != NULL) {
			while ((d = readdir(dir)) != NULL) {
				snprintf(fpath, sizeof(fpath), "%s/%s", dpath,
				    d->d_name);
				remove(fpath);
			}
			closedir(dir);
			remove(dpath);
		}
	}
}

static void
terminate(int eval)
{

	pidfile_remove(pfh);
	remove_runtime_dir();

	exit(eval);
}

/* ARGSUSED */
static void
sighandler(int sig __unused)
{

	terminate(1);
}

static int
find_device(const char *dev)
{
	struct libusb20_backend *backend;
	struct libusb20_device *pdev;
	unsigned int bus, addr;
	int ret;

	if (sscanf(dev, "/dev/ugen%u.%u", &bus, &addr) < 2) {
		syslog(LOG_ERR, "%s not found", dev);
		return (-1);
	}

	backend = libusb20_be_alloc_default();
	if (backend == NULL) {
		syslog(LOG_ERR, "can not alloc backend");
		return (-1);
	}

	ret = 0;
	pdev = NULL;
	while ((pdev = libusb20_be_device_foreach(backend, pdev)) != NULL) {
		if (bus == libusb20_dev_get_bus_number(pdev) &&
		    addr == libusb20_dev_get_address(pdev)) {
			ret = open_device(dev, pdev);
			break;
		}
	}

	if (pdev == NULL) {
		syslog(LOG_ERR, "%s not found", dev);
		ret = -1;
	}

	libusb20_be_free(backend);

	return (ret);
}

static int
open_device(const char *dev, struct libusb20_device *pdev)
{
	struct libusb20_config *config;
	struct libusb20_interface *iface;
	int cndx, e, i;

	e = libusb20_dev_open(pdev, 32);
	if (e != 0) {
		syslog(LOG_ERR, "libusb20_dev_open %s failed", dev);
		return (-1);
	}

	/*
	 * Use current configuration.
	 */
	cndx = libusb20_dev_get_config_index(pdev);
	config = libusb20_dev_alloc_config(pdev, cndx);
	if (config == NULL) {
		syslog(LOG_ERR, "Can not alloc config for %s", dev);
		return (-1);
	}

	/*
	 * Iterate each interface.
	 */
	for (i = 0; i < config->num_interface; i++) {
		iface = &config->interface[i];
		if (iface->desc.bInterfaceClass == LIBUSB20_CLASS_HID) {
			if (verbose)
				PRINT0(dev, i, "HID interface\n");
			open_iface(dev, pdev, iface, i);
		}
	}

	free(config);

	return (0);
}

static void
open_iface(const char *dev, struct libusb20_device *pdev,
    struct libusb20_interface *iface, int ndx)
{
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	struct hid_interface *hi;
	struct libusb20_endpoint *ep;
	unsigned char rdesc[16384];
	int desc, ds, e, j, pos, size;
	uint16_t actlen;

	/*
	 * Get report descriptor.
	 */

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
	if (verbose)
		PRINT0(dev, ndx, "Report descriptor size = %d\n", ds);
	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);
	req.bmRequestType = LIBUSB20_ENDPOINT_IN |
	    LIBUSB20_REQUEST_TYPE_STANDARD | LIBUSB20_RECIPIENT_INTERFACE;
	req.bRequest = LIBUSB20_REQUEST_GET_DESCRIPTOR;
	req.wValue = LIBUSB20_DT_REPORT << 8;
	req.wIndex = ndx;
	req.wLength = ds;
	e = libusb20_dev_request_sync(pdev, &req, rdesc, &actlen, 0, 0);
	if (e) {
		syslog(LOG_ERR, "%s[%d]=> libusb20_dev_request_sync"
		    " failed", basename(dev), ndx);
		return;
	}

	/*
	 * Dump HID report descriptor in human readable form, if requested.
	 */
	if (hidump) {
		PRINT0(dev, ndx, "Report descriptor dump:\n");
		dump_report_desc(rdesc, actlen);
	}

	/*
	 * Allocate a hid parent device.
	 */

	hi = calloc(1, sizeof(*hi));
	if (hi == NULL) {
		syslog(LOG_ERR, "calloc failed: %m");
		exit(1);
	}
	hi->dev = dev;
	hi->pdev = pdev;
	hi->iface = iface;
	hi->ndx = ndx;
	memcpy(hi->rdesc, rdesc, actlen);
	hi->rsz = actlen;
	ddesc = libusb20_dev_get_device_desc(pdev);
	hi->vendor_id = ddesc->idVendor;
	hi->product_id = ddesc->idProduct;

	/*
	 * Find the input interrupt endpoint.
	 */

	for (j = 0; j < iface->num_endpoints; j++) {
		ep = &iface->endpoints[j];
		if ((ep->desc.bmAttributes & LIBUSB20_TRANSFER_TYPE_MASK) ==
		    LIBUSB20_TRANSFER_TYPE_INTERRUPT &&
		    ((ep->desc.bEndpointAddress & LIBUSB20_ENDPOINT_DIR_MASK) ==
		    LIBUSB20_ENDPOINT_IN)) {
			hi->ep = ep->desc.bEndpointAddress;
			hi->pkt_sz = ep->desc.wMaxPacketSize;
			if (verbose) {
				PRINT1("Find IN interrupt ep: %#x", hi->ep);
				printf(" packet_size=%#x\n", hi->pkt_sz);
			}
			break;
		}
	}
	if (hi->ep == 0) {
		PRINT1("does not have IN interrupt ep\n");
		free(hi);
		return;
	}

	STAILQ_INSERT_TAIL(&hilist, hi, next);
}

static int
alloc_hid_interface_be(struct hid_interface *hi)
{
	struct libusb20_backend *backend;
	struct libusb20_device *pdev;
	unsigned int bus, addr, e;

	assert(hi != NULL);

	if (sscanf(hi->dev, "/dev/ugen%u.%u", &bus, &addr) < 2) {
		syslog(LOG_ERR, "%s not found", hi->dev);
		return (-1);
	}

	backend = libusb20_be_alloc_default();
	pdev = NULL;
	while ((pdev = libusb20_be_device_foreach(backend, pdev)) != NULL) {
		if (bus == libusb20_dev_get_bus_number(pdev) &&
		    addr == libusb20_dev_get_address(pdev)) {
			e = libusb20_dev_open(pdev, 32);
			if (e != 0) {
				syslog(LOG_ERR, "%s: libusb20_dev_open failed",
				    hi->dev);
				return (-1);
			}
			break;
		}
	}
	if (pdev == NULL) {
		syslog(LOG_ERR, "%s not found", hi->dev);
		return (-1);
	}

	hi->pdev = pdev;

	return (0);
}

int
hid_handle_kernel_driver(struct hid_parser *hp)
{
	struct hid_interface *hi;
	struct libusb20_device *pdev;
	int ndx;

	/*
	 * Check if any kernel driver is attached to this interface.
	 */

	hi = hid_parser_get_private(hp);
	assert(hi != NULL);
	pdev = hi->pdev;
	ndx = hi->ndx;
	if (libusb20_dev_kernel_driver_active(pdev, ndx) == 0) {
		PRINT1("Kernel driver is active\n");
		if (config_detach_kernel_driver(hi) > 0) {
			if (libusb20_dev_detach_kernel_driver(pdev, ndx) != 0) {
				PRINT1("Unable to detach kernel driver: "
				    "libusb20_dev_detach_kernel_driver "
				    "failed\n");
				if (config_forced_attach(hi) > 0) {
					PRINT1("Continue anyway\n");
					return (0);
				}
				return (-1);
			} else
				PRINT1("kernel driver detached!\n");
		} else {
			if (config_forced_attach(hi) > 0) {
				PRINT1("Continue anyway\n");
				return (0);
			}
			PRINT1("Abort attach since kernel driver is active\n");
			PRINT1("Please try running uhidd with option '-u' to "
			    "detach the kernel drivers\n");
			PRINT1("or specify option '-U' to force attaching the"
			    " interface\n")
			return (-1);
		}
	}

	return (0);
}

static void *
start_hid_interface(void *arg)
{
	struct hid_interface *hi;
	struct libusb20_transfer *xfer;
	char buf[4096];
	uint32_t actlen;
	uint8_t x;
	int e, i;

	hi = arg;
	assert(hi != NULL);

	/*
	 * Start receiving data from the endpoint.
	 */

	if (verbose)
		PRINT1("HID interface task started\n");

	x = (hi->ep & LIBUSB20_ENDPOINT_ADDRESS_MASK) * 2;
	x |= 1;			/* IN transfer. */
	xfer = libusb20_tr_get_pointer(hi->pdev, x);
	if (xfer == NULL) {
		syslog(LOG_ERR, "%s[%d] libusb20_tr_get_pointer failed\n",
		    basename(hi->dev), hi->ndx);
		goto parent_end;
	}

	e = libusb20_tr_open(xfer, 4096, 1, hi->ep);
	if (e == LIBUSB20_ERROR_BUSY) {
		PRINT1("xfer already opened\n");
	} else if (e) {
		syslog(LOG_ERR, "%s[%d] libusb20_tr_open failed\n",
		    basename(hi->dev), hi->ndx);
		goto parent_end;
	}

	for (;;) {

		if (libusb20_tr_pending(xfer)) {
			PRINT1("tr pending\n");
			continue;
		}

		libusb20_tr_setup_intr(xfer, buf, hi->pkt_sz, 0);

		libusb20_tr_start(xfer);

		for (;;) {
			if (libusb20_dev_process(hi->pdev) != 0) {
				PRINT1(" device detached?\n");
				goto parent_end;
			}
			if (libusb20_tr_pending(xfer) == 0)
				break;
			libusb20_dev_wait_process(hi->pdev, -1);
		}

		switch (libusb20_tr_get_status(xfer)) {
		case 0:
			actlen = libusb20_tr_get_actual_length(xfer);
			if (verbose > 2) {
				PRINT1("received data(%u): ", actlen);
				for (i = 0; (uint32_t) i < actlen; i++)
					printf("%02d ", buf[i]);
				putchar('\n');
			}
			hid_parser_input_data(hi->hp, buf, actlen);
			break;
		case LIBUSB20_TRANSFER_TIMED_OUT:
			if (verbose)
				PRINT1("TIMED OUT\n");
			break;
		default:
			if (verbose)
				PRINT1("transfer error\n");
			break;
		}
	}

parent_end:

	if (verbose)
		PRINT1("HID parent exit\n");

	return (NULL);
}

#if 0
int
hid_interrupt_out(void *context, int report_id, char *buf, int len)
{
	struct hid_interface *hi;
	struct libusb20_transfer *xfer;
	uint32_t actlen;
	uint8_t x;
	int e, i, size;

	hi = context;
	assert(hi != NULL && hi->pdev != NULL);

	x = (hi->ep & LIBUSB20_ENDPOINT_ADDRESS_MASK) * 2;
	xfer = libusb20_tr_get_pointer(hi->pdev, x);
	if (xfer == NULL) {
		syslog(LOG_ERR, "%s[%d] libusb20_tr_get_pointer failed\n",
		    basename(hi->dev), hi->ndx);
		return (-1);
	}

	e = libusb20_tr_open(xfer, 4096, 1, XXX); /* FIXME */
	if (e && e != LIBUSB20_ERROR_BUSY) {
		syslog(LOG_ERR, "%s[%d] libusb20_tr_open failed\n",
		    basename(hi->dev), hi->ndx);
		return (-1);
	}
	
	if (libusb20_tr_pending(xfer)) {
		PRINT1("tr pending\n");
		return (-1);
	}

	size = len;
	while (size > 0) {

		libusb20_tr_setup_intr(xfer, buf, len, 0);

		libusb20_tr_start(xfer);

		for (;;) {
			if (libusb20_dev_process(hi->pdev) != 0) {
				PRINT1(" device detached?\n");
				return (-1);
			}
			if (libusb20_tr_pending(xfer) == 0)
				break;
			libusb20_dev_wait_process(hi->pdev, -1);
		}

		switch (libusb20_tr_get_status(xfer)) {
		case 0:
			actlen = libusb20_tr_get_actual_length(xfer);
			if (verbose > 2) {
				PRINT1("transfered data(%u): ", actlen);
				for (i = 0; (uint32_t) i < actlen; i++)
					printf("%02d ", buf[i]);
				putchar('\n');
			}
			break;

		case LIBUSB20_TRANSFER_TIMED_OUT:
			if (verbose)
				PRINT1("TIMED OUT\n");
			return (-1);
		default:
			if (verbose)
				PRINT1("transfer error\n");
			return (-1);
		}

		buf += actlen;
		size -= actlen;
	}

	return (0);
}
#endif

#define	_SET_REPORT_RETRY	3

static int
hid_set_report(void *context, int report_id, char *buf, int len)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	struct hid_interface *hi;
	uint16_t actlen;
	int e, i, try;

	hi = context;
	assert(hi != NULL && hi->pdev != NULL);

	printf("hid_set_report (%d)", len);
	for (i = 0; i < len; i++)
		printf(" 0x%02x", buf[i]);
	putchar('\n');

	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);
	req.bmRequestType = LIBUSB20_ENDPOINT_OUT |
	    LIBUSB20_REQUEST_TYPE_CLASS | LIBUSB20_RECIPIENT_INTERFACE;
	req.bRequest = 0x09;	/* SET_REPORT */
	req.wValue = (0x02 << 8) | (report_id & 0xff); /* FIXME report type */
	req.wIndex = hi->ndx;
	req.wLength = len;
	try = 0;
	do {
		e = libusb20_dev_request_sync(hi->pdev, &req, buf, &actlen, 0, 0);
		if (e && verbose)
			syslog(LOG_ERR, "%s[%d] libusb20_dev_request_sync failed",
			    basename(hi->dev), hi->ndx);
		try++;
	} while (e && try < _SET_REPORT_RETRY);
	if (e) {
		syslog(LOG_ERR, "%s[%d] libusb20_dev_request_sync failed",
		    basename(hi->dev), hi->ndx);
		return (-1);
	}
	if (verbose) {
		PRINT1("set_report: id(%d)", report_id);
		for (i = 0; i < len; i++)
			printf(" %d", buf[i]);
		putchar('\n');
	}

	return (0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: uhidd [-c config_file] [-H devname] "
	    "[-dDhkmosuUvV] /dev/ugen%%u.%%u\n");
	exit(1);
}

static void
version(void)
{

	fprintf(stderr, "uhidd 0.2.1\n");
	exit(0);
}
