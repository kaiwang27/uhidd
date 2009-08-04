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
__FBSDID("$FreeBSD: trunk/uvhid/uvhid.c 36 2009-07-29 02:59:57Z kaiw27 $");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <sys/queue.h>
#include <sys/selinfo.h>
#include <dev/usb/usb_ioctl.h>
#include "uvhid_var.h"

#define	UVHID_NAME	"uvhid"
#define	UVHIDCTL_NAME	"uvhidctl"
#define	UVHID_QUEUE_SIZE	10240
#define	UVHID_MAX_REPORT_SIZE	255
#define UVHID_MAX_REPORT_DESC_SIZE	10240

MALLOC_DECLARE(M_UVHID);
MALLOC_DEFINE(M_UVHID, UVHID_NAME, "Virtual USB HID device");

#define UVHID_LOCK_GLOBAL()	mtx_lock(&uvhidmtx);
#define UVHID_UNLOCK_GLOBAL()	mtx_unlock(&uvhidmtx);
#define UVHID_LOCK(s)		mtx_lock(&(s)->us_mtx)
#define UVHID_UNLOCK(s)		mtx_unlock(&(s)->us_mtx)
#define UVHID_LOCK_ASSERT(s, w)	mtx_assert(&(s)->us_mtx, w)
#define	UVHID_SLEEP(s, f, d, t) \
	msleep(&(s)->f, &(s)->us_mtx, PCATCH | (PZERO + 1), d, t)

struct rqueue {
	int		cc;
	unsigned char	q[UVHID_QUEUE_SIZE];
	unsigned char	*head;
	unsigned char	*tail;
};

struct uvhid_softc {
	struct rqueue	us_rq;
	struct rqueue	us_wq;
	struct selinfo	us_rsel;
	struct selinfo	us_wsel;
	struct mtx	us_mtx;
	struct cv	us_cv;
	
	int		us_hcflags;
	int		us_hflags;

#define	OPEN	(1 << 0)	/* device is open */
#define	READ	(1 << 1)	/* read pending */
#define	WRITE	(1 << 2)	/* write pending */

	unsigned char	us_rdesc[UVHID_MAX_REPORT_DESC_SIZE];
	int		us_rsz;
	int		us_rid;

	STAILQ_ENTRY(uvhid_softc) us_next;
};

/* Global mutex to protect the softc list. */
static struct mtx uvhidmtx;
static STAILQ_HEAD(, uvhid_softc) hidhead = STAILQ_HEAD_INITIALIZER(hidhead);
static struct clonedevs	*hidctl_clones = NULL;
static struct clonedevs	*hid_clones = NULL;

static void	hidctl_clone(void *arg, struct ucred *cred, char *name,
		    int namelen, struct cdev **dev);
static void	hidctl_init(struct cdev *hidctl_dev, struct cdev *hid_dev);
static int	gen_read(struct uvhid_softc *sc, int *scflag,
		    struct rqueue *rq, struct uio *uio, int flag);
static int	gen_write(struct uvhid_softc *sc, int *scflag,
		    struct rqueue *rq, struct selinfo *sel, struct uio *uio,
		    int flag);
static int	gen_poll(struct uvhid_softc *sc, struct rqueue *rq,
		    struct selinfo *sel, int events, struct thread *td);
static void	rq_reset(struct rqueue *rq);
static void	rq_dequeue(struct rqueue *rq, char *dst, int *size);
static void	rq_enqueue(struct rqueue *rq, char *src, int size);

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

	/*
	 * Create hidctl device node.
	 */
	if (clone_create(&hidctl_clones, &hidctl_cdevsw, &unit, dev, 0)) {
		*dev = make_dev(&hidctl_cdevsw, unit, UID_ROOT, GID_WHEEL,
		    0600, UVHIDCTL_NAME "%d", unit);
		if (*dev != NULL) {
			dev_ref(*dev);
			(*dev)->si_flags |= SI_CHEAPCLONE;
		}
	}

	/*
	 * Create hid device node.
	 */
	hid_dev = NULL;
	if (clone_create(&hid_clones, &hid_cdevsw, &unit, dev, 0)) {
		hid_dev = make_dev(&hid_cdevsw, unit, UID_ROOT, GID_WHEEL,
		    0666, UVHID_NAME "%d", unit);
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
	mtx_init(&sc->us_mtx, "uvhidmtx", NULL, MTX_DEF | MTX_RECURSE);
	cv_init(&sc->us_cv, "uvhidcv");
	hidctl_dev->si_drv1 = sc;
	hid_dev->si_drv1 = sc;
	UVHID_LOCK_GLOBAL();
	STAILQ_INSERT_TAIL(&hidhead, sc, us_next);
	UVHID_UNLOCK_GLOBAL();
}
static void
hidctl_destroy(struct uvhid_softc *sc)
{

	UVHID_LOCK(sc);
	if (((sc->us_hcflags & OPEN) != 0) || ((sc->us_hflags & OPEN) != 0))
		cv_wait_unlock(&sc->us_cv, &sc->us_mtx);
	else
		UVHID_UNLOCK(sc);
	mtx_destroy(&sc->us_mtx);
	cv_destroy(&sc->us_cv);
	free(sc, M_UVHID);
}

static int
hidctl_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;

	UVHID_LOCK(sc);
	if (sc->us_hcflags & OPEN) {
		UVHID_UNLOCK(sc);
		return (EBUSY);
	}
	sc->us_hcflags |= OPEN;
	rq_reset(&sc->us_rq);
	rq_reset(&sc->us_wq);
	UVHID_UNLOCK(sc);

	return (0);
}

static int
hidctl_close(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;

	UVHID_LOCK(sc);
	sc->us_hcflags &= ~OPEN;
	rq_reset(&sc->us_rq);
	rq_reset(&sc->us_wq);
	selwakeuppri(&sc->us_rsel, PZERO + 1);
	if ((sc->us_hflags & OPEN) == 0)
		cv_broadcast(&sc->us_cv);
	UVHID_UNLOCK(sc);

	return (0);
}

static int
hidctl_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc = dev->si_drv1;

	return (gen_read(sc, &sc->us_hcflags, &sc->us_wq, uio, flag));
}

static int
hidctl_write(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc = dev->si_drv1;

	return (gen_write(sc, &sc->us_hcflags, &sc->us_rq, &sc->us_rsel, uio,
	    flag));
}

static int
hidctl_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;
	struct usb_gen_descriptor *ugd;
	int err;

	err = 0;

	switch (cmd) {
	case USB_SET_REPORT_DESC:
		UVHID_LOCK(sc);
		ugd = (struct usb_gen_descriptor *)data;
		if (ugd->ugd_actlen == 0) {
			UVHID_UNLOCK(sc);
			break;
		}
		if (ugd->ugd_actlen > UVHID_MAX_REPORT_DESC_SIZE) {
			UVHID_UNLOCK(sc);
			err = ENXIO;
			break;
		}
		sc->us_rsz = ugd->ugd_actlen;
		err = copyin(ugd->ugd_data, sc->us_rdesc, ugd->ugd_actlen);
		UVHID_UNLOCK(sc);
		break;

	case USB_SET_REPORT_ID:
		UVHID_LOCK(sc);
		sc->us_rid = *(int *)data;
		UVHID_UNLOCK(sc);
		break;

	default:
		err = ENOIOCTL;
		break;
	}

	return (err);
}

static int
hidctl_poll(struct cdev *dev, int events, struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;

	return (gen_poll(sc, &sc->us_wq, &sc->us_wsel, events, td));
}

static int
hid_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;

	UVHID_LOCK(sc);
	if (sc->us_hflags & OPEN) {
		UVHID_UNLOCK(sc);
		return (EBUSY);
	}
	sc->us_hflags |= OPEN;
	UVHID_UNLOCK(sc);

	return (0);
}

static int
hid_close(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;

	UVHID_LOCK(sc);
	sc->us_hflags &= ~OPEN;
	selwakeuppri(&sc->us_wsel, PZERO + 1);
	if ((sc->us_hcflags & OPEN) == 0)
		cv_broadcast(&sc->us_cv);
	UVHID_UNLOCK(sc);

	return (0);
}

static int
hid_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc = dev->si_drv1;

	return (gen_read(sc, &sc->us_hflags, &sc->us_rq, uio, flag));
}

static int
hid_write(struct cdev *dev, struct uio *uio, int flag)
{
	struct uvhid_softc *sc = dev->si_drv1;

	return (gen_write(sc, &sc->us_hflags, &sc->us_wq, &sc->us_wsel, uio,
	    flag));
}

static int
hid_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;
	struct usb_gen_descriptor *ugd;
	int err;

	err = 0;

	switch (cmd) {
	case USB_GET_REPORT_DESC:
		UVHID_LOCK(sc);
		ugd = (struct usb_gen_descriptor *)data;
		ugd->ugd_actlen = min(sc->us_rsz, ugd->ugd_maxlen);
		if (ugd->ugd_data == NULL || ugd->ugd_actlen == 0) {
			UVHID_UNLOCK(sc);
			break;
		}
		err = copyout(sc->us_rdesc, ugd->ugd_data, ugd->ugd_actlen);
		UVHID_UNLOCK(sc);
		break;

	case USB_SET_IMMED:
	case USB_GET_REPORT:
	case USB_SET_REPORT:
		err = ENODEV;	/* not supported. */
			
	case USB_GET_REPORT_ID:
		UVHID_LOCK(sc);
		*(int *)data = sc->us_rid;
		UVHID_UNLOCK(sc);
		break;

	default:
		err = ENOIOCTL;
		break;
	}

	return (err);
}

static int
hid_poll(struct cdev *dev, int events, struct thread *td)
{
	struct uvhid_softc *sc = dev->si_drv1;

	return (gen_poll(sc, &sc->us_rq, &sc->us_rsel, events, td));
}

static int
gen_read(struct uvhid_softc *sc, int *scflag, struct rqueue *rq,
    struct uio *uio, int flag)
{
	unsigned char buf[UVHID_MAX_REPORT_SIZE];
	int amnt, err, len;

	UVHID_LOCK(sc);
	if (*scflag & READ) {
		UVHID_UNLOCK(sc);
		return (EALREADY);
	}
	*scflag |= READ;

	len = 0;

read_again:
	if (rq->cc > 0) {
		rq_dequeue(rq, buf, &len);
		UVHID_UNLOCK(sc);
		amnt = min(uio->uio_resid, len);
		err = uiomove(buf, amnt, uio);
		UVHID_LOCK(sc);
	} else {
		if (flag & O_NONBLOCK) {
			err = EWOULDBLOCK;
			goto read_done;
		}
		err = msleep(&rq->cc, &sc->us_mtx, PCATCH | (PZERO + 1),
		    "uvhidr", 0);
		if (err != 0)
			goto read_done;
		goto read_again;
	}

read_done:
	*scflag &= ~READ;
	UVHID_UNLOCK(sc);

	return (err);
}

static int
gen_write(struct uvhid_softc *sc, int *scflag, struct rqueue *rq,
    struct selinfo *sel, struct uio *uio, int flag)
{
	unsigned char buf[UVHID_MAX_REPORT_SIZE];
	int err, len;

	UVHID_LOCK(sc);

	if (*scflag & WRITE) {
		UVHID_UNLOCK(sc);
		return (EALREADY);
	}
	*scflag |= WRITE;

	UVHID_UNLOCK(sc);
	len = uio->uio_resid;
	err = uiomove(buf, len, uio);
	UVHID_LOCK(sc);
	if (err != 0)
		goto write_done;
	rq_enqueue(rq, buf, len);
	selwakeuppri(sel, PZERO + 1);
	wakeup(&rq->cc);

write_done:
	*scflag &= ~WRITE;
	UVHID_UNLOCK(sc);

	return (err);
}

static int
gen_poll(struct uvhid_softc *sc, struct rqueue *rq, struct selinfo *sel,
    int events, struct thread *td)
{
	int revents;

	revents = 0;

	UVHID_LOCK(sc);

	if (events & (POLLIN | POLLRDNORM)) {
		if (rq->cc > 0)
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, sel);
	}

	/*
	 * Write is always non-blocking.
	 */
	if (events & (POLLOUT | POLLWRNORM))
		revents |= events & (POLLOUT | POLLWRNORM);

	UVHID_UNLOCK(sc);

	return (revents);
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
rq_enqueue(struct rqueue *rq, char *src, int size)
{
	int len, part1, part2;

	if (size > UVHID_MAX_REPORT_SIZE || size <= 0)
		return;

	/*
	 * Discard the oldest reports if not enough room left.
	 */
	len = size + 1;
	while (len > UVHID_QUEUE_SIZE - rq->cc)
		rq_dequeue(rq, NULL, NULL);

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
	struct uvhid_softc *sc;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&uvhidmtx, "uvhidgmtx", NULL, MTX_DEF);
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
		UVHID_LOCK_GLOBAL();
		while ((sc = STAILQ_FIRST(&hidhead)) != NULL) {
			STAILQ_REMOVE(&hidhead, sc, uvhid_softc, us_next);
			UVHID_UNLOCK_GLOBAL();
			hidctl_destroy(sc);
			UVHID_LOCK_GLOBAL();
		}
		UVHID_UNLOCK_GLOBAL();
		clone_cleanup(&hidctl_clones);
		clone_cleanup(&hid_clones);
		mtx_destroy(&uvhidmtx);
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

DEV_MODULE(uvhid, uvhid_modevent, NULL);
