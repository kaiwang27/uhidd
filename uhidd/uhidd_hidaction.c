/*-
 * Copyright (c) 2009, 2012 Kai Wang
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
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "uhidd.h"

static void match_hidaction(struct hid_appcol *ha, struct hidaction_config *hc);
static void run_cmd(struct hid_interface *hi, struct hidaction *hac, int val);

void
find_hidaction(struct hid_appcol *ha)
{
	struct hidaction_config *hc;
	struct device_config *dc;
	struct hid_interface *hi;

	/*
	 * Search global hidaction rules.
	 */

	STAILQ_FOREACH(hc, &uconfig.gconfig.haclist, next)
		match_hidaction(ha, hc);

	/*
	 * Search per device hidaction rules.
	 */

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && !STAILQ_EMPTY(&dc->haclist)) {
		STAILQ_FOREACH(hc, &dc->haclist, next)
			match_hidaction(ha, hc);
	}
}

static void
match_hidaction(struct hid_appcol *ha, struct hidaction_config *hc)
{
	struct hid_interface *hi;
	struct hid_report *hr;
	struct hid_field *hf;
	struct hidaction *hac;
	char ub[256];
	unsigned u;
	int i;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	STAILQ_FOREACH(hr, &ha->ha_hrlist, hr_next) {
		STAILQ_FOREACH(hf, &hr->hr_hflist[HID_INPUT], hf_next) {
			for (i = 0; i < hf->hf_nusage_count; i++) {
				u = hf->hf_nusage[i];
				snprintf(ub, sizeof(ub), "%s:%s",
				    usage_page(HID_PAGE(u)),
				    usage_in_page(HID_PAGE(u), HID_USAGE(u)));
				if (!strcasecmp(ub, hc->usage)) {
					hac = malloc(sizeof(*hac));
					if (hac == NULL)
						err(1, "malloc");
					hac->conf = hc;
					hac->hr = hr;
					hac->hf = hf;
					hac->usage = u;
					hac->lastseen = -1;
					hac->lastused = -1;
					STAILQ_INSERT_TAIL(&ha->ha_haclist, hac,
					    next);
					PRINT1(1, "Found hidaction match for "
					    "usage %s at (rid:%d pos:%d)\n",
					    hc->usage, hr->hr_id, hf->hf_pos);
				}
			}
		}
	}
}

void
run_hidaction(struct hid_appcol *ha, struct hid_report *hr)
{
	struct hid_interface *hi;
	struct hid_field *hf;
	struct hidaction *hac;
	int i;

	hi = hid_appcol_get_parser_private(ha);
	assert(hi != NULL);

	STAILQ_FOREACH(hac, &ha->ha_haclist, next) {
		if (hr != hac->hr)
			continue;
		STAILQ_FOREACH(hf, &hr->hr_hflist[HID_INPUT], hf_next) {
			if (hf != hac->hf)
				continue;
			for (i = 0; i < hf->hf_count; i++) {
				if (hf->hf_usage[i] != hac->usage)
					continue;
				run_cmd(hi, hac, hf->hf_value[i]);
			}
		}
	}
}

#define CMDSZ	1024

static void
run_cmd(struct hid_interface *hi, struct hidaction *hac, int val)
{
        char cmdbuf[CMDSZ], *p, *q;
        size_t len;
	int r;

	if (hac->conf->value != val && hac->conf->anyvalue == 0)
		goto next;

	if (hac->conf->debounce == 0)
		goto docmd;

	if (hac->conf->debounce == 1 &&
	    (hac->conf->lastseen == -1 || hac->conf->lastseen != val))
		goto docmd;

	if ((hac->conf->debounce > 1) && ((hac->conf->lastused == -1) ||
	    (abs(hac->conf->lastused - val) >= hac->conf->debounce))) {
		hac->conf->lastused = val;
		goto docmd;
	}

	goto next;

docmd:
	for (p = hac->conf->action, q = cmdbuf; *p && q < &cmdbuf[CMDSZ-1]; ) {
                if (*p == '$') {
                        p++;
                        len = &cmdbuf[CMDSZ-1] - q;
                        if (*p == 'V') {
                                p++;
                                snprintf(q, len, "%d", val);
                                q += strlen(q);
			} else if (*p == 'N') {
				p++;
				strncpy(q, hac->conf->usage, len);
				q += strlen(q);
			} else if (*p == 'H' ) {
				p++;
				strncpy(q, hi->dev, len);
				q += strlen(q);
                        } else if (*p)
                                *q++ = *p++;
                } else
                        *q++ = *p++;
        }
        *q = 0;

	PRINT1(1, "run_hidaction: system '%s'\n", cmdbuf);
        r = system(cmdbuf);
	PRINT1(2, "run_hidaction: return code = 0x%x\n", r);

next:
	hac->conf->lastseen = val;
}
