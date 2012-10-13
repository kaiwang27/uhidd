/*-
 * Copyright (c) 2012 Kai Wang
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
__FBSDID("$FreeBSD $");

#include <sys/param.h>
#include <cuse4bsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <syslog.h>

#include "uhidd.h"

#if 0
const char *uhidd_cusedevs[] = {
	"Auhid%d",
	"Auvhid%d",
	"Avhid%d",
	"Bjs%d",
	"Binput/js%d",
	"Cinput/event%d",
};
#endif

int
ucuse_init(void)
{

	if (cuse_init() != CUSE_ERR_NONE) {
		syslog(LOG_ERR, "cuse_init failed");
		return (-1);
	}

	return (0);
}

static void
ucuse_worder_hup_catcher(int dummy)
{

	/* Catch the SIGHUP signaled by cuse4bsd. */
	(void) dummy;
}

static void *
ucuse_worker(void * arg)
{

	(void) arg;

	signal(SIGHUP, ucuse_worder_hup_catcher);

	for (;;) {
		if (cuse_wait_and_process() != 0)
			break;
	}

	return (NULL);
}

int
ucuse_create_worker(void)
{
	pthread_t id;

	if (pthread_create(&id, NULL, ucuse_worker, NULL)) {
		syslog(LOG_ERR, "pthread_create failed: %m");
		return (-1);
	}

	return (0);
}
