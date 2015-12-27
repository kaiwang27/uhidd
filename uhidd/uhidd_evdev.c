/*-
 * Copyright (c) 2015 Kai Wang
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
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/usb/usbhid.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>
#if __FreeBSD_version >= 1100023
#include <cuse.h>
#else
#include <cuse4bsd.h>
#endif

#include "uhidd.h"

/*
 * The driver implements kernel input/eventX interface using the cuse(3)
 * library.
 *
 * Linux input framework is partly documented by:
 *
 * Using the Input Subsystem (Part I & II)
 * http://www.linuxjournal.com/article/6396
 * http://www.linuxjournal.com/article/6429
 *
 * evdev events are described by:
 * https://www.kernel.org/doc/Documentation/input/
 *
 * This driver is written based on analysis of the behavior of test programs
 * using libevdev and the xf86-input-evdev driver. Bugs and incompatibilities
 * with the Linux input event interface are expected.
 */

#if CUSE_VERSION < 0x000118
#error uhidd requires cuse4bsd version >= 0.1.24
#endif

#ifndef	CUSE_ID_UHIDD
#define	CUSE_ID_UHIDD(what) CUSE_MAKE_ID('U','D',what,0)
#endif

#define EVDEV_CUSE_INDEX 'E'
#define EVDEV_CUSE_DEFAULT_DEVNAME "input/event"

/*
 * List of ioctls supported by this driver. Note that the value
 * for FreeBSD's IOC_OUT/IOC_IN pair is inverted, comparing with Linux
 * IOC_READ/IOC_WRITE.
 *
 * This following definition will work with application
 * compiled with v4l_compat, since v4l_compat has replaced
 * IOC_READ/IOC_WRITE with IOC_OUT/IOC_IN.
 */
#define	IOCTL_GETVERSION	_IOR('E', 0x1, int)
#define IOCTL_GETDEVID		_IOR('E', 0x2, uint16_t[4])
#define	IOCTL_GETREPEAT		_IOR('E', 0x3, unsigned int[2])
#define	IOCTL_SETREPEAT		_IOW('E', 0x3, unsigned int[2])
#define	IOCTL_GETDEVNAME(n)	_IOC(IOC_OUT, 'E', 0x6, n)
#define	IOCTL_GETPHYS(n)	_IOC(IOC_OUT, 'E', 0x7, n)
#define	IOCTL_GETUNIQ(n)	_IOC(IOC_OUT, 'E', 0x8, n)
#define	IOCTL_GETDEVPROP(n)	_IOC(IOC_OUT, 'E', 0x9, n)
#define	IOCTL_GETMTSLOTS(n)	_IOC(IOC_OUT, 'E', 0xa, n)
#define	IOCTL_GETKEY(n)		_IOC(IOC_OUT, 'E', 0x18, n)
#define	IOCTL_GETLED(n)		_IOC(IOC_OUT, 'E', 0x19, n)
#define	IOCTL_GETSOUND(n)	_IOC(IOC_OUT, 'E', 0x1a, n)
#define	IOCTL_GETSWITCH(n)	_IOC(IOC_OUT, 'E', 0x1b, n)
#define	IOCTL_GETBIT(e,n)	_IOC(IOC_OUT, 'E', 0x20 + (e), n)
#define	IOCTL_GETABS(a)		_IOR('E', 0x40 + (a), int32_t[6])
#define	IOCTL_SETABS(a)		_IOW('E', 0xc0 + (a), int32_t[6])
#define	IOCTL_GRABDEV		_IOW('E', 0x90, int)

/* Input event types supported by this driver */
#define	EVTYPE_SYN		0x00
#define	EVTYPE_KEY		0x01
#define	EVTYPE_REL		0x02
#define	EVTYPE_ABS		0x03
#define	EVTYPE_MISC		0x04
#define	EVTYPE_SWITCH		0x05
#define	EVTYPE_LED		0x11
#define	EVTYPE_SOUND		0x12
#define	EVTYPE_REPEAT		0x14
#define	EVTYPE_FF		0x15

/* Total count for each event type */
#define	PROP_CNT		0x20
#define	EVTYPE_CNT		0x20
#define	KEY_CNT			0x300
#define	REL_CNT			0x10
#define	ABS_CNT			0x40
#define	SWITCH_CNT		0x10
#define	MISC_CNT		0x08
#define	LED_CNT			0x10
#define	SOUND_CNT		0x08
#define	REPEAT_CNT		0x02
#define	FF_CNT			0x80

/* evdev message format */
struct evmsg {
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

#define	EVMSG_SZ	sizeof(struct evmsg)
#define	EVBUF_SZ	(32 * EVMSG_SZ)
#define	LONG_NBITS	(sizeof(unsigned long) * 8)
#define NLONGS(x)	(howmany(x, LONG_NBITS))
#define	NBYTES(x)	(howmany(x, LONG_NBITS) * sizeof(unsigned long))
#define	BIT_SET(v, n)	(v[(n)/LONG_NBITS] |= 1UL << ((n) % LONG_NBITS))
#define	BIT_CLR(v, n)	(v[(n)/LONG_NBITS] &= ~(1UL << ((n) % LONG_NBITS)))
#define	BIT_ISSET(v, n)	(v[(n)/LONG_NBITS] & 1UL << ((n) % LONG_NBITS))
#define	EVMSG(e,t,c,v)					\
	do {						\
		memcpy(&(e).time, &tv, sizeof(tv));	\
		(e).type = (uint16_t)(t);		\
		(e).code = (uint16_t)(c);		\
		(e).value = (v);			\
	} while (0)

/* Debug printf macros */
#define	PRINTE(v, ...)							\
	do {								\
		if (verbose >= (v)) {					\
			char pb[64], pb2[1024];				\
			snprintf(pb, sizeof(pb), "%s[%d][%s]",		\
			    basename(hi->dev), hi->ndx, ed->devname);	\
			snprintf(pb2, sizeof(pb2), __VA_ARGS__);	\
			printf("%s-> %s", pb, pb2);			\
		}							\
	} while (0)

#define	PRINTEC(v, ...)							\
	do {								\
		if (verbose >= (v)) {					\
			char pb[64], pb2[1024];				\
			snprintf(pb, sizeof(pb), "%s[%d][%s][c:%d]",	\
			    basename(hi->dev), hi->ndx, ed->devname,	\
				ec->ndx);				\
			snprintf(pb2, sizeof(pb2), __VA_ARGS__);	\
			printf("%s-> %s", pb, pb2);			\
		}							\
	} while (0)

struct evdev_dev;

/* evdev client struct */
struct evclient {
	struct evdev_dev *evdev;
	char buf[EVBUF_SZ];
	char *head;
	char *tail;
	size_t cc;
	unsigned ndx;
	int flags;
	unsigned char enabled;
	pthread_mutex_t mtx;
	pthread_cond_t cv;
	LIST_ENTRY(evclient) next;

#define	EVCLIENT_READ		0x01
#define	EVCLIENT_WRITE		0x02
#define	EVCLIENT_LOCK(s)	pthread_mutex_lock(&(s)->mtx)
#define	EVCLIENT_UNLOCK(s)	pthread_mutex_unlock(&(s)->mtx)
};

/* evdev device struct */
struct evdev_dev {
	char devname[80];
	char name[80];
	char phys[80];
	char uniq[80];
	unsigned long prop_bits[NLONGS(PROP_CNT)];
	unsigned long evtype_bits[NLONGS(EVTYPE_CNT)];
	unsigned long key_bits[NLONGS(KEY_CNT)];
	unsigned long rel_bits[NLONGS(REL_CNT)];
	unsigned long abs_bits[NLONGS(ABS_CNT)];
	unsigned long switch_bits[NLONGS(SWITCH_CNT)];
	unsigned long led_bits[NLONGS(LED_CNT)];
	unsigned long misc_bits[NLONGS(MISC_CNT)];
	unsigned long sound_bits[NLONGS(SOUND_CNT)];
	unsigned long repeat_bits[NLONGS(REPEAT_CNT)];
	unsigned long ff_bits[NLONGS(FF_CNT)];
	unsigned long key_states[NLONGS(KEY_CNT)];
	unsigned long led_states[NLONGS(LED_CNT)];
	unsigned long switch_states[NLONGS(SWITCH_CNT)];
	unsigned long sound_states[NLONGS(SOUND_CNT)];
	int refcnt;
	unsigned totalcnt;
	pthread_mutex_t ed_mtx;
	struct evdev_cb *cb;
	void *priv;
	LIST_HEAD(, evclient) clients;

#define	EVDEV_LOCK(s)		pthread_mutex_lock(&(s)->ed_mtx)
#define	EVDEV_UNLOCK(s)		pthread_mutex_unlock(&(s)->ed_mtx)
};

static void evclient_enqueue(struct evclient *ec, char *buf, size_t len);
static int evclient_dequeue(struct evclient *ec, char *buf, size_t len);
static struct evdev_dev *evdev_alloc(void *priv, struct evdev_cb *cb);
static int evdev_alloc_cuse_dev(struct evdev_dev *ed);
static void evdev_init_bits(struct evdev_dev *ed);
static void evdev_init_input_bits(struct evdev_dev *ed, struct hid_field *hf);
static void evdev_init_output_bits(struct evdev_dev *ed, struct hid_field *hf);
static void evdev_grab(struct evdev_dev *ed, struct evclient *ec,
    uint32_t grab);
static void evdev_enqueue(struct evdev_dev *ed, char *buf, size_t len);
static int evdev_cuse_open(struct cuse_dev *cdev, int fflags);
static int evdev_cuse_close(struct cuse_dev *cdev, int fflags);
static int evdev_cuse_read(struct cuse_dev *cdev, int fflags, void *peer_ptr,
    int len);
static int evdev_cuse_write(struct cuse_dev *cdev, int fflags,
    const void *peer_ptr, int len);
static int evdev_cuse_ioctl(struct cuse_dev *cdev, int fflags,
    unsigned long cmd, void *peer_data);
static int evdev_cuse_poll(struct cuse_dev *cdev, int fflags, int events);

static struct cuse_methods evdev_cuse_methods = {
	.cm_open = evdev_cuse_open,
	.cm_close = evdev_cuse_close,
	.cm_read = evdev_cuse_read,
	.cm_write = evdev_cuse_write,
	.cm_ioctl = evdev_cuse_ioctl,
	.cm_poll = evdev_cuse_poll,
};

struct evdev_dev *
evdev_register_device(void *priv, struct evdev_cb *cb)
{
	struct evdev_dev *ed;

	assert(cb->get_hid_appcol != NULL);

	if ((ed = evdev_alloc(priv, cb)) == NULL)
		return (NULL);

	evdev_init_bits(ed);

	if (evdev_alloc_cuse_dev(ed) < 0) {
		free(ed);
		return (NULL);
	}

	return (ed);
}

void
evdev_report_key_event(struct evdev_dev *ed, int scancode, int key,
    int value)
{
	struct evmsg em[2];
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0)
		return;

	EVMSG(em[0], EVTYPE_MISC, 4, scancode);
	EVMSG(em[1], EVTYPE_KEY, key, value);

	evdev_enqueue(ed, (char *) em, sizeof(em));
}

void
evdev_report_key_repeat_event(struct evdev_dev *ed, int key)
{
	struct evmsg em;
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0)
		return;

	EVMSG(em, EVTYPE_KEY, key, 2);

	evdev_enqueue(ed, (char *) &em, sizeof(em));
}

void
evdev_sync_report(struct evdev_dev *ed)
{
	struct evmsg em;
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0)
		return;
	
	EVMSG(em, EVTYPE_SYN, 0, 1);

	evdev_enqueue(ed, (char *) &em, sizeof(em));
}

const char *
evdev_devname(struct evdev_dev *ed)
{

	return (ed->devname);
}

static struct evdev_dev *
evdev_alloc(void *priv, struct evdev_cb *cb)
{
	struct evdev_dev *ed;

	assert(priv != NULL && cb != NULL && cb->get_hid_interface != NULL);

	if ((ed = calloc(1, sizeof(*ed))) == NULL)
		return (NULL);

	ed->cb = cb;
	ed->priv = priv;
	LIST_INIT(&ed->clients);

	if (pthread_mutex_init(&ed->ed_mtx, NULL)) {
		syslog(LOG_ERR, "pthread_mutex_init failed: %m");
		free(ed);
		return (NULL);
	}

	return (ed);
}

static int
evdev_alloc_cuse_dev(struct evdev_dev *ed)
{
	int classid, devid, i;

	if (ucuse_init() < 0)
		return (-1);

	classid = EVDEV_CUSE_INDEX - 'A';
	if (cuse_alloc_unit_number_by_id(&devid, CUSE_ID_UHIDD(classid)) !=
	    0) {
		syslog(LOG_ERR, "cuse_alloc_unit_number_by_id failed");
		return (-1);
	}

	cuse_dev_create(&evdev_cuse_methods, ed, NULL, 0, 0, 0660, "%s%d",
	    EVDEV_CUSE_DEFAULT_DEVNAME, devid);

	snprintf(ed->devname, sizeof(ed->devname), "%s%d",
	    EVDEV_CUSE_DEFAULT_DEVNAME, devid);

	/* Create worker threads. */
	for (i = 0; i < 2; i++)
		ucuse_create_worker();

	return (0);
}

static void
evdev_init_bits(struct evdev_dev *ed)
{
	struct hid_interface *hi = ed->cb->get_hid_interface(ed->priv);
	struct hid_appcol *ha = ed->cb->get_hid_appcol(ed->priv);
	struct hid_report *hr;
	struct hid_field *hf;
	int flags, i;

	BIT_SET(ed->evtype_bits, EVTYPE_SYN);
	PRINT1(2, "set EVTYPE_SYN\n");

	if (ed->cb->set_repeat_delay != NULL) {
		BIT_SET(ed->evtype_bits, EVTYPE_REPEAT);
		PRINT1(2, "set EVTYPE_REPEAT\n");
		ed->repeat_bits[0] = 0x3;
	}

	STAILQ_FOREACH(hr, &ha->ha_hrlist, hr_next) {
		for (i = 0; i < 3; i++) {
			if (STAILQ_EMPTY(&hr->hr_hflist[i]))
				continue;
			STAILQ_FOREACH(hf, &hr->hr_hflist[i], hf_next) {
				flags = hid_field_get_flags(hf);
				if (flags & HIO_CONST)
					continue;
				if ((enum hid_kind) i == HID_INPUT)
					evdev_init_input_bits(ed, hf);
				else if ((enum hid_kind) i == HID_OUTPUT)
					evdev_init_output_bits(ed, hf);
			}
		}
	}
}

static void
evdev_init_input_bits(struct evdev_dev *ed, struct hid_field *hf)
{
	struct hid_interface *hi = ed->cb->get_hid_interface(ed->priv);
	struct hid_key hk;
	unsigned up;
	int i, k, found_key;
	uint16_t u;

	up = hid_field_get_usage_page(hf);

	found_key = 0;
	if (up == HUP_KEYBOARD || up == HUP_CONSUMER ||
	    up == HUP_GENERIC_DESKTOP) {
		hk.up = up;
		for (i = 0; i < hf->hf_nusage_count; i++) {
			u = hf->hf_nusage[i] & 0xFFFF;
			if (up == HUP_CONSUMER && u == HUG_VOLUME) {
				/*
				 * HUG_VOLUME is converted to HUG_VOLUME_UP
				 * or HUG_VOLUME_DOWN to fit in the key
				 * press/release model.
				 */
				hk.code = HUG_VOLUME_UP;
				BIT_SET(ed->key_bits, evdev_hid2key(&hk));
				PRINT1(3, "set key_bits: 0x%03x HID(%#x)\n",
				    (unsigned) evdev_hid2key(&hk),
				    HID_USAGE2(hk.up, hk.code));
				hk.code = HUG_VOLUME_DOWN;
				BIT_SET(ed->key_bits, evdev_hid2key(&hk));
				PRINT1(3, "set key_bits: 0x%03x HID(%#x)\n",
				    (unsigned) evdev_hid2key(&hk),
				    HID_USAGE2(hk.up, hk.code));
				continue;
			}
			hk.code = u;
			if ((k = evdev_hid2key(&hk)) > 0) {
				BIT_SET(ed->key_bits, k);
				PRINT1(3, "set key_bits: 0x%03x HID(%#x)\n",
				    (unsigned) evdev_hid2key(&hk),
				    HID_USAGE2(hk.up, hk.code));
				found_key = 1;
			} else
				PRINT1(4, "No keymap for HID(%#x)\n",
				    HID_USAGE2(hk.up, hk.code));
		}
	}

	if (found_key) {
		BIT_SET(ed->evtype_bits, EVTYPE_KEY);
		PRINT1(2, "set EVTYPE_KEY\n");
	}
}

static void
evdev_init_output_bits(struct evdev_dev *ed, struct hid_field *hf)
{
	struct hid_interface *hi = ed->cb->get_hid_interface(ed->priv);
	unsigned up;
	int i;
	uint16_t u;

	up = hid_field_get_usage_page(hf);

	if (up == HUP_LEDS) {
		BIT_SET(ed->evtype_bits, EVTYPE_LED);
		PRINT1(2, "set EVTYPE_LED\n");
		for (i = 0; i < hf->hf_nusage_count; i++) {
			u = hf->hf_nusage[i] & 0xFFFF;
			switch (u) {
			case 0x01: /* Num Lock */
				BIT_SET(ed->led_bits, 0);
				PRINT1(3, "set led_bits 0\n");
				break;
			case 0x02: /* Caps Lock */
				BIT_SET(ed->led_bits, 1);
				PRINT1(3, "set led_bits 1\n");
				break;
			case 0x03: /* Scroll Lock */
				BIT_SET(ed->led_bits, 2);
				PRINT1(3, "set led_bits 2\n");
				break;
			case 0x04: /* Compose */
				BIT_SET(ed->led_bits, 3);
				PRINT1(3, "set led_bits 3\n");
				break;
			case 0x05: /* Kana */
				BIT_SET(ed->led_bits, 4);
				PRINT1(3, "set led_bits 4\n");
				break;
			default:
				PRINT1(0, "ignored led bits %#x\n", u);
				break;
			}
		}
	}
}

static void
evdev_grab(struct evdev_dev *ed, struct evclient *ec, uint32_t grab)
{
	struct evclient *_ec;

	EVDEV_LOCK(ed);
	LIST_FOREACH(_ec, &ed->clients, next) {
		EVCLIENT_LOCK(_ec);
		if (grab) {
			if (_ec != ec)
				_ec->enabled = 0;
		} else if (!_ec->enabled) {
			_ec->enabled = 1;
			pthread_cond_signal(&_ec->cv);
		}
		EVCLIENT_UNLOCK(_ec);
	}
	EVDEV_UNLOCK(ed);

	if (!grab)
		cuse_poll_wakeup();
}

static void
evdev_enqueue(struct evdev_dev *ed, char *buf, size_t len)
{
	struct evclient *ec;

	assert(ed != NULL && buf != NULL);
	assert(len > 0 && len % EVMSG_SZ == 0 && len <= EVBUF_SZ);

	/* Insert the event message and wakeup all clients. */
	EVDEV_LOCK(ed);
	LIST_FOREACH(ec, &ed->clients, next) {
		EVCLIENT_LOCK(ec);
		evclient_enqueue(ec, buf, len);
		pthread_cond_signal(&ec->cv);
		EVCLIENT_UNLOCK(ec);
	}
	EVDEV_UNLOCK(ed);

	cuse_poll_wakeup();
}

static void
evclient_enqueue(struct evclient *ec, char *buf, size_t len)
{
	struct evmsg *em;
	size_t part1, part2;

	assert(ec != NULL && buf != NULL);
	assert(len > 0 && len % EVMSG_SZ == 0 && len <= EVBUF_SZ);

	/*
	 * Discard the oldest event messages if not enough room left.
	 * According to the Linux evdev documentation, we should signal
	 * the buffer overrun to the user application by emitting a
	 * SYN_DROPPED event.
	 */
	if (len > EVBUF_SZ - ec->cc) {
		evclient_dequeue(ec, NULL, len - (EVBUF_SZ - ec->cc));
		/*
		 * Signal buffer overrun. (but don't do anything if the
		 * ring buffer is already empty)
		 */
		if (ec->cc > 0) {
			em = (struct evmsg *)(uintptr_t)ec->head;
			em->type = EVTYPE_SYN;
			em->code = 3;
			em->value = 1;
		}
	}

	part1 = EVBUF_SZ - (ec->tail - ec->buf);
	if (part1 > len)
		part1 = len;
	part2 = len - part1;

	if (part1 > 0) {
		memcpy(ec->tail, buf, part1);
		buf += part1;
		ec->tail += part1;
		ec->cc += part1;
	}

	if (ec->tail == ec->buf + EVBUF_SZ)
		ec->tail = ec->buf;

	if (part2 > 0) {
		memcpy(ec->tail, buf, part2);
		ec->tail += part2;
		ec->cc += part2;
	}
}

static int
evclient_dequeue(struct evclient *ec, char *buf, size_t len)
{
	size_t part1, part2;

	assert(ec != NULL && ec->cc > 0);
	assert(len > 0 && len % EVMSG_SZ == 0);

	if (len > ec->cc)
		len = ec->cc;

	part1 = EVBUF_SZ - (ec->head - ec->buf);
	if (part1 > len)
		part1 = len;

	part2 = len - part1;

	if (part1 > 0) {
		if (buf) {
			memcpy(buf, ec->head, part1);
			buf += part1;
		}
		ec->head += part1;
		ec->cc -= part1;
	}

	if (ec->head == ec->buf + EVBUF_SZ)
		ec->head = ec->buf;

	if (part2 > 0) {
		if (buf)
			memcpy(buf, ec->head, part2);
		ec->head += part2;
		ec->cc -= part2;
	}

	return (len);
}

static int
evdev_cuse_open(struct cuse_dev *cdev, int fflags)
{
	struct evdev_dev *ed = cuse_dev_get_priv0(cdev);
	struct evclient *ec;
	struct hid_interface *hi = ed->cb->get_hid_interface(ed->priv);

	PRINTE(1, "evdev_cuse_open: cdev(%p) fflags(%#x)\n", cdev,
	    (unsigned) fflags);

	/* Initialize a new evdev client. */
	if ((ec = calloc(1, sizeof(struct evclient))) == NULL)
		return (CUSE_ERR_NO_MEMORY);
	ec->evdev = ed;
	if (pthread_mutex_init(&ec->mtx, NULL))
		return (CUSE_ERR_NO_MEMORY);
	if (pthread_cond_init(&ec->cv, NULL))
		return (CUSE_ERR_NO_MEMORY);
	ec->head = ec->tail = ec->buf;

	cuse_dev_set_per_file_handle(cdev, ec);
	
	EVDEV_LOCK(ed);
	ed->refcnt++;
	ec->ndx = ed->totalcnt++;
	ec->enabled = 1;
	LIST_INSERT_HEAD(&ed->clients, ec, next);
	EVDEV_UNLOCK(ed);
	
	return (CUSE_ERR_NONE);
}


static int
evdev_cuse_close(struct cuse_dev *cdev, int fflags)
{
	struct evdev_dev *ed = cuse_dev_get_priv0(cdev);
	struct evclient *ec = cuse_dev_get_per_file_handle(cdev);
	struct hid_interface *hi = ed->cb->get_hid_interface(ed->priv);

	PRINTE(1, "evdev_cuse_close: cdev(%p) fflags(%#x)\n", cdev,
	    (unsigned) fflags);

	EVDEV_LOCK(ed);
	LIST_REMOVE(ec, next);
	ed->refcnt--;
	EVDEV_UNLOCK(ed);

	free(ec);
	
	return (CUSE_ERR_NONE);
}

static int
evdev_cuse_read(struct cuse_dev *cdev, int fflags, void *peer_ptr, int len)
{
	struct evclient *ec = cuse_dev_get_per_file_handle(cdev);
	char buf[EVMSG_SZ * EVBUF_SZ];
	int err;

	if (len == 0 || len % EVMSG_SZ != 0) {
		return (CUSE_ERR_INVALID);
	}

	EVCLIENT_LOCK(ec);
	if (ec->flags & EVCLIENT_READ) {
		EVCLIENT_UNLOCK(ec);
		return (CUSE_ERR_BUSY); /* actually EALREADY */
	}
	ec->flags |= EVCLIENT_READ;

read_again:
	if (ec->cc > 0) {
		len = evclient_dequeue(ec, buf, len);
		assert(len > 0 && len % EVMSG_SZ == 0);
		EVCLIENT_UNLOCK(ec);
		err = cuse_copy_out(buf, peer_ptr, len);
		EVCLIENT_LOCK(ec);
	} else {
		if (fflags & CUSE_FFLAG_NONBLOCK) {
			err = CUSE_ERR_WOULDBLOCK;
			goto read_done;
		}
		if (pthread_cond_wait(&ec->cv, &ec->mtx)) {
			err = CUSE_ERR_OTHER;
			goto read_done;
		}
		goto read_again;
	}

read_done:
	ec->flags &= ~EVCLIENT_READ;
	EVCLIENT_UNLOCK(ec);

	if (err)
		return (err);

	return (len);
}

static int
evdev_cuse_write(struct cuse_dev *cdev, int fflags, const void *peer_ptr,
    int len)
{
	struct evdev_dev *ed = cuse_dev_get_priv0(cdev);
	struct evclient *ec = cuse_dev_get_per_file_handle(cdev);
	char buf[EVMSG_SZ * EVBUF_SZ];
	int err;

	(void) fflags;

	if (len == 0 || len % EVMSG_SZ != 0) {
		return (CUSE_ERR_INVALID);
	}

	EVCLIENT_LOCK(ec);
	if (ec->flags & EVCLIENT_WRITE) {
		EVCLIENT_UNLOCK(ec);
		return (CUSE_ERR_BUSY); /* actually EALREADY */
	}
	ec->flags |= EVCLIENT_WRITE;

	if ((size_t) len > EVBUF_SZ)
		len = EVBUF_SZ;

	EVCLIENT_UNLOCK(ec);
	err = cuse_copy_in(peer_ptr, buf, len);
	EVCLIENT_LOCK(ec);
	if (err != CUSE_ERR_NONE)
		goto write_done;

	evclient_enqueue(ec, buf, len);

write_done:
	ec->flags &= ~EVCLIENT_WRITE;
	EVCLIENT_UNLOCK(ec);

	if (err)
		return (err);

	evdev_enqueue(ed, buf, len);

	return (len);
}

static int
evdev_cuse_ioctl(struct cuse_dev *cdev, int fflags, unsigned long cmd,
    void *peer_data)
{
	struct evdev_dev *ed = cuse_dev_get_priv0(cdev);
	struct evclient *ec = cuse_dev_get_per_file_handle(cdev);
	struct hid_interface *hi = ed->cb->get_hid_interface(ed->priv);
	uint32_t a32[6];
	uint16_t a16[4];
	unsigned v[2], len;
	int err;

	len = IOCPARM_LEN(cmd);
	cmd = IOCBASECMD(cmd);

	PRINTEC(1, "evdev_cuse_ioctl: cdev(%p) cmd(%#lx) fflags(%#x)\n", cdev,
	    cmd, (unsigned) fflags);
	
	switch (cmd) {
	case IOCBASECMD(IOCTL_GETVERSION):
		if (len != sizeof(int)) {
			err = CUSE_ERR_INVALID;
			break;
		}
		v[0] = 0x10001;
		err = cuse_copy_out(&v[0], peer_data, sizeof(v[0]));
		break;

	case IOCBASECMD(IOCTL_GETDEVID):
		if (len != 4 * sizeof(uint16_t)) {
			err = CUSE_ERR_INVALID;
			break;
		}
		a16[0] = 3;
		a16[1] = (uint16_t) hi->vendor_id;
		a16[2] = (uint16_t) hi->product_id;
		a16[3] = 0;
		err = cuse_copy_out(a16, peer_data, sizeof(a16));
		break;

	case IOCBASECMD(IOCTL_GETREPEAT):
		if (len != 2 * sizeof(unsigned int)) {
			err = CUSE_ERR_INVALID;
			break;
		}
		if (ed->cb->get_repeat_delay == NULL)
			return (CUSE_ERR_INVALID);
		ed->cb->get_repeat_delay(ed->priv, &a32[0], &a32[1]);
		err = cuse_copy_out(a32, peer_data, 2 * sizeof(uint32_t));
		break;

	case IOCBASECMD(IOCTL_SETREPEAT):
		if (len != 2 * sizeof(unsigned int)) {
			err = CUSE_ERR_INVALID;
			break;
		}
		if (ed->cb->set_repeat_delay == NULL)
			return (CUSE_ERR_INVALID);
		err = cuse_copy_in(peer_data, a32, 2 * sizeof(uint32_t));
		if (err != CUSE_ERR_NONE)
			break;
		ed->cb->set_repeat_delay(ed->priv, a32[0], a32[1]);
		break;

	case IOCBASECMD(IOCTL_GRABDEV):
		if (len != sizeof(int)) {
			err = CUSE_ERR_INVALID;
			break;
		}
		err = cuse_copy_in(peer_data, &v[0], sizeof(v[0]));
		PRINTEC(2, "IOCTL_GRABDEV: %u\n", v[0]);
		if (err != CUSE_ERR_NONE)
			break;
		evdev_grab(ed, ec, v[0]);
		break;

	case IOCTL_GETDEVNAME(0):
		err = ucuse_copy_out_string(ed->name, peer_data, len);
		break;

	case IOCTL_GETPHYS(0):
		err = ucuse_copy_out_string(ed->phys, peer_data, len);
		break;

	case IOCTL_GETUNIQ(0):
		err = ucuse_copy_out_string(ed->uniq, peer_data, len);
		break;

	case IOCTL_GETDEVPROP(0):
		len = MIN(len, sizeof(ed->prop_bits));
		err = cuse_copy_out(ed->prop_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(0, 0):
		PRINTEC(2, "IOCTL_GETBIT(0, 0): len=%lu max=%ju "
		    "evtype_bits=%#lx\n", NBYTES(len),
		    (uintmax_t) sizeof(ed->evtype_bits), ed->evtype_bits[0]);
		len = MIN(NBYTES(len), sizeof(ed->evtype_bits));
		err = cuse_copy_out(ed->evtype_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_KEY, 0):
		len = MIN(NBYTES(len), sizeof(ed->key_bits));
		err = cuse_copy_out(ed->key_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_REL, 0):
		len = MIN(NBYTES(len), sizeof(ed->rel_bits));
		err = cuse_copy_out(ed->rel_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_ABS, 0):
		len = MIN(NBYTES(len), sizeof(ed->abs_bits));
		err = cuse_copy_out(ed->abs_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_MISC, 0):
		len = MIN(NBYTES(len), sizeof(ed->misc_bits));
		err = cuse_copy_out(ed->misc_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_SWITCH, 0):
		len = MIN(NBYTES(len), sizeof(ed->switch_bits));
		err = cuse_copy_out(ed->switch_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_LED, 0):
		len = MIN(NBYTES(len), sizeof(ed->led_bits));
		err = cuse_copy_out(ed->led_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_SOUND, 0):
		len = MIN(NBYTES(len), sizeof(ed->sound_bits));
		err = cuse_copy_out(ed->sound_bits, peer_data, len);
		break;

	case IOCTL_GETBIT(EVTYPE_FF, 0):
		len = MIN(NBYTES(len), sizeof(ed->ff_bits));
		err = cuse_copy_out(ed->ff_bits, peer_data, len);
		break;

	case IOCTL_GETKEY(0):
		len = MIN(NBYTES(len), sizeof(ed->key_states));
		err = cuse_copy_out(ed->key_states, peer_data, len);
		break;

	case IOCTL_GETLED(0):
		len = MIN(NBYTES(len), sizeof(ed->led_states));
		err = cuse_copy_out(ed->led_states, peer_data, len);
		break;

	case IOCTL_GETSOUND(0):
		len = MIN(NBYTES(len), sizeof(ed->sound_states));
		err = cuse_copy_out(ed->sound_states, peer_data, len);
		break;

	default:
		/* Not supported. */
		return (CUSE_ERR_INVALID);
	}

	return (err);
}

static int
evdev_cuse_poll(struct cuse_dev *cdev, int fflags, int events)
{
	struct evclient *ec = cuse_dev_get_per_file_handle(cdev);
	int revents;

	(void) fflags;

	revents = 0;

	EVCLIENT_LOCK(ec);

	if (events & CUSE_POLL_READ) {
		if (ec->cc > 0 && (ec->flags & EVCLIENT_READ) == 0)
			revents |= CUSE_POLL_READ;
	}

	if (events & CUSE_POLL_WRITE) {
		if ((ec->flags & EVCLIENT_WRITE) == 0)
			revents |= CUSE_POLL_WRITE;
	}

	EVCLIENT_UNLOCK(ec);

	return (revents);
}
