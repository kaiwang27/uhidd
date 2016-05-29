/*-
 * Copyright (c) 2009, 2012 Kai Wang
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
#include <dev/usb/usb_ioctl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#if __FreeBSD_version >= 1100023
#include <cuse.h>
#else
#include <cuse4bsd.h>
#endif
#include "uhidd.h"

/*
 * General Virtual HID device driver. (cuse4bsd version)
 */

#if CUSE_VERSION < 0x000118
#error uhidd requires cuse4bsd version >= 0.1.24
#endif

#ifndef	CUSE_ID_UHIDD
#define	CUSE_ID_UHIDD(what) CUSE_MAKE_ID('U','D',what,0)
#endif

#define VHID_CUSE_INDEX 'A'
#define VHID_CUSE_DEFAULT_DEVNAME "uvhid"
#define VHID_QUEUE_SIZE	10240
#define VHID_MAX_REPORT_SIZE 255
#define VHID_MAX_REPORT_DESC_SIZE 10240

#define VHID_LOCK(s)		pthread_mutex_lock(&(s)->vd_mtx)
#define VHID_UNLOCK(s)		pthread_mutex_unlock(&(s)->vd_mtx)

#define VHID_OPEN (1 << 0)	/* device is open */
#define VHID_READ (1 << 1)	/* read pending */
#define VHID_WRITE (1 << 2)	/* write pending */

struct rqueue {
	int		cc;
	unsigned char	q[VHID_QUEUE_SIZE];
	unsigned char	*head;
	unsigned char	*tail;
};

struct vhid_dev {
	char		vd_name[80];
	int		vd_flags;
	struct rqueue	vd_rq;
	unsigned char	vd_rdesc[VHID_MAX_REPORT_DESC_SIZE];
	uint16_t	vd_rsz;
	int		vd_rid;
	pthread_mutex_t vd_mtx;
	pthread_cond_t	vd_cv;
};

static void rq_reset(struct rqueue *rq);
static void rq_dequeue(struct rqueue *rq, char *dst, int *size);
static void rq_enqueue(struct rqueue *rq, char *src, int size);
static cuse_open_t vhid_open;
static cuse_close_t vhid_close;
static cuse_read_t vhid_read;
static cuse_write_t vhid_write;
static cuse_ioctl_t vhid_ioctl;
static cuse_poll_t vhid_poll;

static struct cuse_methods vhid_cuse_methods = {
	.cm_open = vhid_open,
	.cm_close = vhid_close,
	.cm_read = vhid_read,
	.cm_write = vhid_write,
	.cm_ioctl = vhid_ioctl,
	.cm_poll = vhid_poll,
};

static inline int
min(int a, int b)
{

	return (a < b ? a : b);
}

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
	const char *dname;
	int classid, devid, i;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if ((vd = calloc(1, sizeof(*vd))) == NULL) {
		syslog(LOG_ERR, "calloc failed in vhid_attach: %m");
		return (-1);
	}

	if (pthread_mutex_init(&vd->vd_mtx, NULL) != 0) {
		syslog(LOG_ERR, "pthread_mutex_init failed in vhid_attach");
		return (-1);
	}

	if (pthread_cond_init(&vd->vd_cv, NULL) != 0) {
		syslog(LOG_ERR, "pthread_cond_init failed in vhid_attach");
		return (-1);
	}

	rq_reset(&vd->vd_rq);

	hid_appcol_set_private(ha, vd);

	/*
	 * Create a new virtual hid device.
	 */

	if (ucuse_init() < 0)
		return (-1);

	classid = VHID_CUSE_INDEX - 'A';
	if (cuse_alloc_unit_number_by_id(&devid, CUSE_ID_UHIDD(classid)) !=
	    0) {
		syslog(LOG_ERR, "cuse_alloc_unit_number_by_id failed");
		return (-1);
	}

	dname = config_vhid_devname(hi);
	if (dname == NULL)
		dname = VHID_CUSE_DEFAULT_DEVNAME;

	cuse_dev_create(&vhid_cuse_methods, vd, ha, 0, 0, 0660, "%s%d",
	    dname, devid);

	snprintf(vd->vd_name, sizeof(vd->vd_name), "%s%d", dname, devid);

	/* Create worker threads. */
	for (i = 0; i < 2; i++)
		ucuse_create_worker();

	PRINT1(1, "vhid device created: %s\n", vd->vd_name);

	/*
	 * Set the report descriptor of this virtual hid device.
	 */

	if (ha->ha_rsz <= VHID_MAX_REPORT_DESC_SIZE) {
		memcpy(vd->vd_rdesc, ha->ha_rdesc, ha->ha_rsz);
		vd->vd_rsz = ha->ha_rsz;
	} else {
		syslog(LOG_ERR, "%s[%d] report descriptor too big!",
		    hi->dev, hi->ndx);
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

	VHID_LOCK(vd);
	rq_enqueue(&vd->vd_rq, buf, len);
	VHID_UNLOCK(vd);

	if (pthread_cond_signal(&vd->vd_cv) != 0)
		syslog(LOG_ERR, "pthread_cond_signal failed in vhid_recv_raw");

	cuse_poll_wakeup();
}

static void
rq_reset(struct rqueue *rq)
{

	rq->cc = 0;
	rq->head = rq->tail = rq->q;
}

static void
rq_dequeue(struct rqueue *rq, char *dst, int *size)
{
	int len, part1, part2;

	if (rq->cc == 0) {
		if (size)
			*size = 0;
		return;
	}

	len = (unsigned char) *rq->head++;
	rq->cc--;
	if (len > rq->cc)
		len = rq->cc;
	if (len == 0) {
		if (size)
			*size = 0;
		return;
	}

	part1 = VHID_QUEUE_SIZE - (rq->head - rq->q);
	if (part1 > len)
		part1 = len;
	part2 = len - part1;

	if (part1 > 0) {
		if (dst) {
			memcpy(dst, rq->head, part1);
			dst += part1;
		}
		rq->head += part1;
		rq->cc -= part1;
	}

	if (rq->head == rq->q + VHID_QUEUE_SIZE)
		rq->head = rq->q;

	if (part2 > 0) {
		if (dst)
			memcpy(dst, rq->head, part2);
		rq->head += part2;
		rq->cc -= part2;
	}

	if (size)
		*size = len;
}

static void
rq_enqueue(struct rqueue *rq, char *src, int size)
{
	int len, part1, part2;

	if (size > VHID_MAX_REPORT_SIZE || size <= 0)
		return;

	/*
	 * Discard the oldest reports if not enough room left.
	 */
	len = size + 1;
	while (len > VHID_QUEUE_SIZE - rq->cc)
		rq_dequeue(rq, NULL, NULL);

	part1 = VHID_QUEUE_SIZE - (rq->tail - rq->q);
	if (part1 > len)
		part1 = len;
	part2 = len - part1;

	if (part1 > 0) {
		*rq->tail++ = (unsigned char) size;
		rq->cc++;
		part1--;

		memcpy(rq->tail, src, part1);
		src += part1;
		rq->tail += part1;
		rq->cc += part1;
	}

	if (rq->tail == rq->q + VHID_QUEUE_SIZE)
		rq->tail = rq->q;

	if (part2 > 0) {
		if (part1 == 0) {
			*rq->tail++ = (unsigned char) size;
			rq->cc++;
			part2--;
		}

		memcpy(rq->tail, src, part2);
		rq->tail += part2;
		rq->cc += part2;
	}
}

static int
vhid_open(struct cuse_dev *cdev, int fflags)
{
	struct vhid_dev *vd = cuse_dev_get_priv0(cdev);

	(void) fflags;

	VHID_LOCK(vd);
	if (vd->vd_flags & VHID_OPEN) {
		VHID_UNLOCK(vd);
		return (CUSE_ERR_BUSY);
	}
	vd->vd_flags |= VHID_OPEN;
	VHID_UNLOCK(vd);

	return (CUSE_ERR_NONE);
}

static int
vhid_close(struct cuse_dev *cdev, int fflags)
{
	struct vhid_dev *vd = cuse_dev_get_priv0(cdev);

	(void) fflags;

	VHID_LOCK(vd);
	vd->vd_flags &= ~VHID_OPEN;
	VHID_UNLOCK(vd);

	return (CUSE_ERR_NONE);
}

static int
vhid_read(struct cuse_dev *cdev, int fflags, void *peer_ptr, int len)
{
	struct vhid_dev *vd = cuse_dev_get_priv0(cdev);
	struct rqueue *rq = &vd->vd_rq;
	unsigned char buf[VHID_MAX_REPORT_SIZE];
	int amnt, err, n;

	(void) fflags;

	/*
	 * Read length should be the size prescribed by the report
	 * descriptor.
	 */

	VHID_LOCK(vd);
	if (vd->vd_flags & VHID_READ) {
		VHID_UNLOCK(vd);
		return (CUSE_ERR_BUSY); /* actually EALREADY */
	}
	vd->vd_flags |= VHID_READ;

	amnt = n = 0;

read_again:
	if (rq->cc > 0) {
		rq_dequeue(rq, buf, &n);
		VHID_UNLOCK(vd);
		amnt = min(len, n);
		err = cuse_copy_out(buf, peer_ptr, amnt);
		VHID_LOCK(vd);
	} else {
		if (fflags & CUSE_FFLAG_NONBLOCK) {
			err = CUSE_ERR_WOULDBLOCK;
			goto read_done;
		}
		err = pthread_cond_wait(&vd->vd_cv, &vd->vd_mtx);
		if (err != 0) {
			err = CUSE_ERR_OTHER;
			goto read_done;
		}
		goto read_again;
	}

read_done:
	vd->vd_flags &= ~VHID_READ;
	VHID_UNLOCK(vd);

	if (err)
		return (err);

	return (amnt);
}

static int
vhid_write(struct cuse_dev *cdev, int fflags, const void *peer_ptr, int len)
{
	struct vhid_dev *vd = cuse_dev_get_priv0(cdev);
	struct hid_appcol *ha = cuse_dev_get_priv1(cdev);
	struct hid_interface *hi;
	unsigned char buf[VHID_MAX_REPORT_SIZE];
	int err, i;

	(void) fflags;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	if (len > VHID_MAX_REPORT_SIZE)
		return (CUSE_ERR_INVALID);

	VHID_LOCK(vd);

	if (vd->vd_flags & VHID_WRITE) {
		VHID_UNLOCK(vd);
		return (CUSE_ERR_BUSY); /* actually EALREADY */
	}
	vd->vd_flags |= VHID_WRITE;

	VHID_UNLOCK(vd);
	err = cuse_copy_in(peer_ptr, buf, len);
	VHID_LOCK(vd);
	if (err != CUSE_ERR_NONE)
		goto write_done;

	if (verbose) {
		PRINT1(1, "%s[%d] vhid_task recevied:", hi->dev,
		    hi->ndx);
		for (i = 0; i < len; i++)
			printf("%d ", buf[i]);
		putchar('\n');
	}

	if (vd->vd_rid != 0 && vd->vd_rid == buf[0])
		hid_appcol_xfer_raw_data(ha, vd->vd_rid, buf + 1,
		    len - 1);
	else
		hid_appcol_xfer_raw_data(ha, vd->vd_rid, buf, len);

write_done:
	vd->vd_flags &= ~VHID_WRITE;
	VHID_UNLOCK(vd);

	if (err)
		return (err);

	return (len);
}

static int
vhid_ioctl(struct cuse_dev *cdev, int fflags, unsigned long cmd,
    void *peer_data)
{
	struct vhid_dev *vd = cuse_dev_get_priv0(cdev);
	struct usb_gen_descriptor ugd;
	uint16_t size;
	int err;

	(void) fflags;

	switch (cmd) {
	case USB_GET_REPORT_DESC:
		err = cuse_copy_in(peer_data, &ugd, sizeof(ugd));
		if (err != CUSE_ERR_NONE)
			break;
		if (vd->vd_rsz > ugd.ugd_maxlen)
			size = ugd.ugd_maxlen;
		else
			size = vd->vd_rsz;
		err = cuse_copy_out(&size,
		    &((struct usb_gen_descriptor *)peer_data)->ugd_actlen,
		    sizeof(size));
		if (err != CUSE_ERR_NONE)
			break;
		if (ugd.ugd_data == NULL)
			break;
		err = cuse_copy_out(vd->vd_rdesc, ugd.ugd_data, (int)size);
		break;

	case USB_SET_IMMED:
	case USB_GET_REPORT:
	case USB_SET_REPORT:
		err = CUSE_ERR_INVALID;	/* not supported. */
		break;

	case USB_GET_REPORT_ID:
		err = cuse_copy_out(&vd->vd_rid, peer_data,
		    sizeof(vd->vd_rid));
		break;

	default:
		err = CUSE_ERR_INVALID;
		break;
	}

	return (err);
}

static int
vhid_poll(struct cuse_dev *cdev, int fflags, int events)
{
	struct vhid_dev *vd = cuse_dev_get_priv0(cdev);
	int revents;

	(void) fflags;

	revents = 0;

	VHID_LOCK(vd);

	if (events & CUSE_POLL_READ) {
		if (vd->vd_rq.cc > 0)
			revents |= CUSE_POLL_READ;
	}

	/* Write is always non-blocking. */
	if (events & CUSE_POLL_WRITE)
		revents |= CUSE_POLL_WRITE;

	VHID_UNLOCK(vd);

	return (revents);
}
