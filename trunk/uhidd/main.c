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
#include <err.h>
#include <fcntl.h>
#include <libusb20.h>
#include <libusb20_desc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int debug = 0;
int detach = 0;

static void	usage(void);
static void	find_dev(const char *dev);
static void	attach_dev(const char *dev, struct libusb20_device *pdev);

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
		if (iface->desc.bInterfaceClass == 3) {
			printf("%s: has HID interface %d\n", dev, i);
		}
	}
}

static void
usage(void)
{

	fprintf(stderr, "usage: uhidd [-dk] /dev/ugen%%u.%%u\n");
	exit(1);
}
