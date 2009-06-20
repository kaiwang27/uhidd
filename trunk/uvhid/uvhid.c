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
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>

#define	UVHID_NAME	"uvhid"
#define	UVHIDCTL_NAME	"uvhidctl"
#define	UVHID_QUEUE_SIZE	10240
#define	UVHID_MAX_REPORT_SIZE	255

MALLOC_DECLARE(M_UVHID);
MALLOC_DEFINE(M_UVHID, UVHID_NAME, "Virtual USB HID device");

#define UVHID_LOCK(s)		mtx_lock(&(s)->us_mtx)
#define UVHID_UNLOCK(s)		mtx_unlock(&(s)->us_mtx)
#define	UVHID_SLEEP(s, f, d, t) \
	msleep(&(s)->f, &(s)->us_mtx, PCATCH | (PZERO + 1), d, t)

struct rqueue {
	int		 cc;
	unsigned char	 q[UVHID_QUEUE_SIZE];
	unsigned char	*head;
	unsigned char	*tail;
};

struct uvhid_softc {
	struct rqueue	us_rq;
	struct rqueue	us_wq;
	struct mtx	us_mtx;

#define	HIDCTL_OPEN	(1 << 0)	/* hidctl device is open */
#define	HID_OPEN	(1 << 1)	/* hid device is open */
#define	HIDCTL_READ	(1 << 2)	/* hidctl read pending */
#define	HID_READ	(1 << 3)	/* hid read pending */
#define	HIDCTL_WRITE	(1 << 2)	/* hidctl write pending */
#define	HID_WRITE	(1 << 3)	/* hid write pending */

	int		us_flags;
};

static void	hidctl_clone(void *arg, struct ucred *cred, char *name,
    int namelen, struct cdev **dev);
static void	hidctl_init(struct cdev *hidctl_dev, struct cdev *hid_dev);
static void	report_dequeue(struct rqueue *rq, char *dst, int *size);
static void	report_enqueue(struct rqueue *rq, char *src, int size);
static d_open_t		hidctl_open;
static d_close_t	hidctl_close;
static d_read_t		hidctl_read;
static d_write_t	hidctl_write;
static d_ioctl_t	hidctl_ioctl;
static d_poll_t		hidctl_poll;
static d_open_t		hid_open;
static d_close_t	hid_close;
static d_read_t		hid_read;
static d_write_t	hid_write;
static d_ioctl_t	hid_ioctl;
static d_poll_t		hid_poll;

static struct cdevsw hidctl_cdevsw = {
	.d_version = D_VERSION,
	.d_flags   = D_NEEDMINOR,
	.d_open	   = hidctl_open,
	.d_close   = hidctl_close,
	.d_read	   = hidctl_read,
	.d_write   = hidctl_write,
	.d_ioctl   = hidctl_ioctl,
	.d_poll	   = hidctl_poll,
	.d_name	   = UVHIDCTL_NAME,
};

static struct cdevsw hid_cdevsw = {
	.d_version = D_VERSION,
	.d_flags   = D_NEEDMINOR,
	.d_open	   = hid_open,
	.d_close   = hid_close,
	.d_read	   = hid_read,
	.d_write   = hid_write,
	.d_ioctl   = hid_ioctl,
	.d_poll	   = hid_poll,
	.d_name	   = UVHID_NAME,
};

static struct clonedevs	*hidctl_clones = NULL;
static struct clonedevs	*hid_clones = NULL;

static void
hidctl_clone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
	struct cdev *hid_dev;
	int unit;

	if (*dev != NULL)
		return;
	if (strcmp(name, UVHIDCTL_NAME) == 0)
		unit = -1;
	else if (dev_stdclone(name, NULL, UVHIDCTL_NAME, &unit) != 1)
		return;

	/* Create hidctl device node. */
	if (clone_create(&hidctl_clones, &hidctl_cdevsw, &unit, dev, 0)) {
		*dev = make_dev(&hidctl_cdevsw, unit, UID_ROOT, GID_WHEEL,
		    0600, UVHIDCTL_NAME "%d", unit);
		if (*dev != NULL) {
			dev_ref(*dev);
			(*dev)->si_flags |= SI_CHEAPCLONE;
		}
	}

	/* Create hid device node. */
	hid_dev = NULL;
	if (clone_create(&hid_clones, &hid_cdevsw, &unit, dev, 0)) {
		hid_dev = make_dev(&hid_cdevsw, unit, UID_ROOT, GID_WHEEL,
		    0600, UVHID_NAME "%d", unit);
		if (hid_dev != NULL) {
			dev_ref(hid_dev);
			hid_dev->si_flags |= SI_CHEAPCLONE;
		}
	}

	if (hid_dev == NULL)
		return;

	hidctl_init(*dev, hid_dev);
}

static void
hidctl_init(struct cdev *hidctl_dev, struct cdev *hid_dev)
{
	struct uvhid_softc *sc;

	sc = malloc(sizeof(*sc), M_UVHID, M_WAITOK|M_ZERO);
	mtx_init(&sc->us_mtx, "uvhid", NULL, MTX_DEF);
	hidctl_dev->si_drv1 = sc;
	hid_dev->si_drv1 = sc;
}

static int
hidctl_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	if (sc->us_flags & HIDCTL_OPEN) {
		UVHID_UNLOCK(sc);
		return (EBUSY);
	}

	sc->us_flags |= HIDCTL_OPEN;

	UVHID_UNLOCK(sc);

	return (0);
}

static int
hidctl_close(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	sc->us_flags &= ~HIDCTL_OPEN;
	sc->us_rq.cc = 0;
	sc->us_rq.head = sc->us_rq.tail = sc->us_rq.q;
	sc->us_wq.cc = 0;
	sc->us_wq.head = sc->us_wq.tail = sc->us_wq.q;

	UVHID_UNLOCK(sc);

	return (0);
}

static int
hidctl_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc;
	unsigned char buf[UVHID_MAX_REPORT_SIZE];
	int len, err;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	if (sc->us_flags & HIDCTL_READ) {
		UVHID_UNLOCK(sc);
		return (EALREADY);
	}
	sc->us_flags |= HIDCTL_READ;

hidctl_read_again:
	len = 0;
	if (sc->us_wq.cc > 0) {
		report_dequeue(&sc->us_wq, buf, &len);
		err = uiomove(buf, len, uio);
	} else {
		if (flag & O_NONBLOCK) {
			err = EWOULDBLOCK;
			goto hidctl_read_done;
		}
		err = UVHID_SLEEP(sc, us_wq, "hidctlr", 0);
		if (err != 0)
			goto hidctl_read_done;
		goto hidctl_read_again;
	}

hidctl_read_done:

	sc->us_flags &= ~HIDCTL_READ;

	UVHID_UNLOCK(sc);

	return (err);
}

static int
hidctl_write(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc;
	unsigned char buf[UVHID_MAX_REPORT_SIZE];
	int err;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	if (sc->us_flags & HIDCTL_WRITE) {
		UVHID_UNLOCK(sc);
		return (EALREADY);
	}

	sc->us_flags |= HIDCTL_WRITE;
	err = uiomove(buf, uio->uio_resid, uio);
	if (err != 0)
		goto hidctl_write_done;
	report_enqueue(&sc->us_rq, buf, uio->uio_resid);

hidctl_write_done:

	sc->us_flags &= ~HIDCTL_WRITE;

	UVHID_UNLOCK(sc);

	return (err);
}

static int
hidctl_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{

	return (0);
}

static int
hidctl_poll(struct cdev *dev, int events, struct thread *td)
{

	return (0);
}

static int
hid_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	if (sc->us_flags & HID_OPEN) {
		UVHID_UNLOCK(sc);
		return (EBUSY);
	}

	sc->us_flags |= HID_OPEN;

	UVHID_UNLOCK(sc);

	return (0);
}

static int
hid_close(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	sc->us_flags &= ~HID_OPEN;

	UVHID_UNLOCK(sc);

	return (0);
}

static int
hid_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc;
	unsigned char buf[UVHID_MAX_REPORT_SIZE];
	int len, err;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	if (sc->us_flags & HID_READ) {
		UVHID_UNLOCK(sc);
		return (EALREADY);
	}
	sc->us_flags |= HID_READ;

hid_read_again:
	len = 0;
	if (sc->us_rq.cc > 0) {
		report_dequeue(&sc->us_rq, buf, &len);
		err = uiomove(buf, len, uio);
	} else {
		if (flag & O_NONBLOCK) {
			err = EWOULDBLOCK;
			goto hid_read_done;
		}
		err = UVHID_SLEEP(sc, us_rq, "hidr", 0);
		if (err != 0)
			goto hid_read_done;
		goto hid_read_again;
	}

hid_read_done:

	sc->us_flags &= ~HID_READ;

	UVHID_UNLOCK(sc);

	return (err);
}

static int
hid_write(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc;
	unsigned char buf[UVHID_MAX_REPORT_SIZE];
	int err;

	sc = dev->si_drv1;

	UVHID_LOCK(sc);

	if (sc->us_flags & HID_WRITE) {
		UVHID_UNLOCK(sc);
		return (EALREADY);
	}

	sc->us_flags |= HID_WRITE;
	err = uiomove(buf, uio->uio_resid, uio);
	if (err != 0)
		goto hid_write_done;
	report_enqueue(&sc->us_wq, buf, uio->uio_resid);

hid_write_done:

	sc->us_flags &= ~HID_WRITE;

	UVHID_UNLOCK(sc);

	return (err);
}

static int
hid_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{

	return (0);
}

static int
hid_poll(struct cdev *dev, int events, struct thread *td)
{

	return (0);
}

static void
report_dequeue(struct rqueue *rq, char *dst, int *size)
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

	part1 = UVHID_QUEUE_SIZE - (rq->head - rq->q);
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

	if (rq->head == rq->q + UVHID_QUEUE_SIZE)
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
report_enqueue(struct rqueue *rq, char *src, int size)
{
	int len, part1, part2;

	if (size > UVHID_MAX_REPORT_SIZE || size <= 0)
		return;

	/* Discard the oldest reports if not enough room left. */
	len = size + 1;
	while (len > UVHID_QUEUE_SIZE - rq->cc)
		report_dequeue(rq, NULL, NULL);

	part1 = UVHID_QUEUE_SIZE - (rq->tail - rq->q);
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

	if (rq->tail == rq->q + UVHID_QUEUE_SIZE)
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
uvhid_modevent(module_t mod, int type, void *data)
{
	static eventhandler_tag	tag;

	switch (type) {
	case MOD_LOAD:
		clone_setup(&hidctl_clones);
		clone_setup(&hid_clones);
		tag = EVENTHANDLER_REGISTER(dev_clone, hidctl_clone, 0, 1000);
		if (tag == NULL) {
			clone_cleanup(&hidctl_clones);
			return (ENOMEM);
		}
		break;

	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(dev_clone, tag);
		clone_cleanup(&hidctl_clones);
		clone_cleanup(&hid_clones);
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

DEV_MODULE(uvhid, uvhid_modevent, NULL);
