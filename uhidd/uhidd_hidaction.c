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

/*
 * Copyright (c) 2000, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson <lennart@augustsson.net>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD $");

#include <sys/param.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "uhidd.h"

void
find_global_hidaction(struct hid_child *hc)
{
	struct hidaction_config *hac;

	STAILQ_FOREACH(hac, &gconfig.halist, next)
		match_hidaction(hc, hac);
}

void
find_device_hidaction(struct hid_child *hc)
{
	struct hid_parent *hp;
	struct device_config *dc;
	struct hidaction_config *hac;

	hp = hc->parent;
	assert(hp != NULL);

	STAILQ_FOREACH(dc, &gconfig.dclist, next) {
		if (dc->vendor_id != hp->vendor_id ||
		    dc->product_id != hp->product_id)
			continue;
		STAILQ_FOREACH(hac, &dc->halist, next)
			match_hidaction(hc, hac);
	}
}

void
match_hidaction(struct hid_child *hc, struct hidaction_config *hac)
{
	struct hid_parent *hp;
	struct hidaction *ha;
	struct hid_data *d;
	hid_item_t h;
	char ub[256], coll[256];
	int u, lo, hi, range;

	assert(hc->p != NULL);

	hp = hc->parent;
	assert(hp != NULL);

	coll[0] = '\0';
	for (d = hid_start_parse(hc->p, 1<<hid_input);
	     hid_get_item(d, &h, -1); ) {
		switch (h.kind) {
		case hid_input:
			if (h.usage_minimum != 0 || h.usage_maximum != 0) {
				lo = h.usage_minimum;
				hi = h.usage_maximum;
				range = 1;
			} else {
				lo = h.usage;
				hi = h.usage;
				range = 0;
			}
			for (u = lo; u <= hi; u++) {
				snprintf(ub, sizeof(ub), "%s:%s",
				    usage_page(HID_PAGE(u)),
				    usage_in_page(HID_PAGE(u), HID_USAGE(u)));
				if (verbose > 3)
					printf("usage %s\n", ub);
				if (!strcasecmp(ub, hac->usage))
					goto foundhid;
				if (coll[0]) {
					snprintf(ub, sizeof(ub), "%s.%s:%s",
					    coll + 1, usage_page(HID_PAGE(u)),
					    usage_in_page(HID_PAGE(u),
					    HID_USAGE(u)));
					if (verbose > 3)
						printf("coll.usage %s\n", ub);
					if (!strcasecmp(ub, hac->usage))
						goto foundhid;
				}
			}
			break;
		case hid_collection:
			snprintf(coll + strlen(coll),
			    sizeof(coll) - strlen(coll), ".%s:%s",
			    usage_page(HID_PAGE(h.usage)),
			    usage_in_page(HID_PAGE(h.usage),
			    HID_USAGE(h.usage)));
			break;
		case hid_endcollection:
			if (coll[0])
				*strrchr(coll, '.') = 0;
			break;
		default:
			break;
		}
	}

	hid_end_parse(d);
	return; 		/* not found. */

foundhid:
	hid_end_parse(d);
	
	if ((ha = malloc(sizeof(*ha))) == NULL) {
		syslog(LOG_ERR, "malloc: %m");
		exit(1);
	}
	ha->conf = hac;
	ha->item = h;
	ha->lastseen = -1;
	ha->lastused = -1;
	STAILQ_INSERT_TAIL(&hc->halist, ha, next);

	if (verbose)
		PRINT2("Found match for usage %s at (rid:%d pos:%d)\n",
		    hac->usage, h.report_ID, h.pos);
}

#define CMDSZ	1024

void
run_hidaction(struct hid_child *hc, struct hidaction *ha, char *buf, int len)
{
	struct hid_parent *hp;
        char cmdbuf[CMDSZ], *p, *q;
        size_t l;
        int r, val;

	(void) len;

	hp = hc->parent;
	assert(hp != NULL);

	if (ha->item.report_ID > 0 && ha->item.report_ID != *buf)
		return;

	val = hid_get_data(buf, &ha->item);

	if (ha->conf->value != val && ha->conf->anyvalue == 0)
		goto next;
	if (ha->conf->debounce == 0)
		goto docmd;
	if (ha->conf->debounce == 1 &&
	    (ha->conf->lastseen == -1 || ha->conf->lastseen != val))
		goto docmd;
	if ((ha->conf->debounce > 1) && ((ha->conf->lastused == -1) ||
	    (abs(ha->conf->lastused - val) >= ha->conf->debounce))) {
		ha->conf->lastused = val;
		goto docmd;
	}

	goto next;

docmd:
	for (p = ha->conf->action, q = cmdbuf; *p && q < &cmdbuf[CMDSZ-1]; ) {
                if (*p == '$') {
                        p++;
                        l = &cmdbuf[CMDSZ-1] - q;
                        if (*p == 'V') {
                                p++;
                                snprintf(q, l, "%d", val);
                                q += strlen(q);
                        } else if (*p)
                                *q++ = *p++;
                } else
                        *q++ = *p++;
        }
        *q = 0;

        if (verbose)
                PRINT2("run_hidaction: system '%s'\n", cmdbuf);
        r = system(cmdbuf);
        if (verbose > 1)
                PRINT2("run_hidaction: return code = 0x%x\n", r);

next:
	ha->conf->lastseen = val;

}
