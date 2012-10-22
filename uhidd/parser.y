%{
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
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "uhidd.h"

extern int yylex(void);
extern int yyparse(void);
extern int lineno;
extern FILE *yyin;

static void config_add_keymap_entry(char *usage, char *hex);
static void config_add_keymap_entry_val(int i, char *hex);

const char *config_file = "/usr/local/etc/uhidd.conf";
struct uhidd_config uconfig;
struct device_config clconfig;

static struct device_config dconfig, *dconfig_p;

%}

%token T_YES
%token T_NO
%token T_DEFAULT
%token T_MOUSE_ATTACH
%token T_KBD_ATTACH
%token T_VHID_ATTACH
%token T_VHID_STRIP_REPORT_ID
%token T_VHID_DEVNAME
%token T_CC_ATTACH
%token T_CC_KEYMAP
%token T_HIDACTION
%token T_DETACHKERNELDRIVER
%token T_FORCED_ATTACH
%token <val> T_NUM
%token <val> T_HEX
%token <str> T_USAGE
%token <str> T_STRING
%type <hac> hidaction_entry

%union {
	char *str;
	int val;
	struct hidaction_config *hac;
}

%%

conf_file
	: conf_list
	|
	;

conf_list
	: conf
	| conf_list conf
	;

conf
	: default_conf
	| device_conf
	;

default_conf
	: T_DEFAULT "=" "{" conf_entry_list "}" {
		if (uconfig.gconfig.interface == -1)
			errx(1, "multiple default section in config file");
		dconfig.vendor_id = -1;
		dconfig.product_id = -1;
		dconfig.interface = -1;
		memcpy(&uconfig.gconfig, &dconfig,
		    sizeof(struct device_config));
		memset(&dconfig, 0 ,sizeof(struct device_config));
		STAILQ_INIT(&dconfig.haclist);
	}
	;

device_conf
	: device_name "=" "{" conf_entry_list "}" {
		dconfig_p = calloc(1, sizeof(struct device_config));
		if (dconfig_p == NULL)
			err(1, "calloc");
		memcpy(dconfig_p, &dconfig, sizeof(struct device_config));
		STAILQ_INSERT_TAIL(&uconfig.dclist, dconfig_p, next);
		memset(&dconfig, 0 ,sizeof(struct device_config));
		STAILQ_INIT(&dconfig.haclist);
	}
	;

device_name
	: T_HEX ":" T_HEX {
		dconfig.vendor_id = $1;
		dconfig.product_id = $3;
		dconfig.interface = -1;
	}
	| T_HEX ":" T_HEX ":" T_NUM {
		dconfig.vendor_id = $1;
		dconfig.product_id = $3;
		dconfig.interface = $5;
	}
	;

conf_entry_list
	: conf_entry
	| conf_entry_list conf_entry
	;

conf_entry
	: mouse_attach
	| kbd_attach
	| cc_attach
	| cc_keymap
	| vhid_attach
	| vhid_strip_id
	| vhid_devname
	| hidaction
	| detach_kernel_driver
	| forced_attach
	;

mouse_attach
	: T_MOUSE_ATTACH "=" T_YES {
		dconfig.mouse_attach = 1;
	}
	| T_MOUSE_ATTACH "=" T_NO {
		dconfig.mouse_attach = -1;
	}
	;

kbd_attach
	: T_KBD_ATTACH "=" T_YES {
		dconfig.kbd_attach = 1;
	}
	| T_KBD_ATTACH "=" T_NO {
		dconfig.kbd_attach = -1;
	}
	;

cc_attach
	: T_CC_ATTACH "=" T_YES {
		dconfig.cc_attach = 1;
	}
	| T_CC_ATTACH "=" T_NO {
		dconfig.cc_attach = -1;
	}
	;

cc_keymap
	: T_CC_KEYMAP "=" "{" cc_keymap_entry_list "}" {
		dconfig.cc_keymap_set = 1;
	}
	;

cc_keymap_entry_list
	: cc_keymap_entry
	| cc_keymap_entry_list cc_keymap_entry
	;

cc_keymap_entry
	: T_USAGE "=" T_STRING {
		config_add_keymap_entry($1, $3);
	}
	| T_HEX "=" T_STRING {
		config_add_keymap_entry_val($1, $3);
	}
	;

vhid_attach
	: T_VHID_ATTACH "=" T_YES {
		dconfig.vhid_attach = 1;
	}
	| T_VHID_ATTACH "=" T_NO {
		dconfig.vhid_attach = -1;
	}
	;

vhid_strip_id
	: T_VHID_STRIP_REPORT_ID "=" T_YES {
		dconfig.vhid_strip_id = 1;
	}
	| T_VHID_STRIP_REPORT_ID "=" T_NO {
		dconfig.vhid_strip_id = -1;
	}

vhid_devname
	: T_VHID_DEVNAME "=" T_STRING {
		dconfig.vhid_devname = $3;
	}
	;

detach_kernel_driver
	: T_DETACHKERNELDRIVER "=" T_YES {
		dconfig.detach_kernel_driver = 1;
	}
	| T_DETACHKERNELDRIVER "=" T_NO {
		dconfig.detach_kernel_driver = -1;
	}

forced_attach
	: T_FORCED_ATTACH "=" T_YES {
		dconfig.forced_attach = 1;
	}
	| T_FORCED_ATTACH "=" T_NO {
		dconfig.forced_attach = -1;
	}


hidaction
	: T_HIDACTION "=" "{" hidaction_entry_list "}"
	;

hidaction_entry_list
	: hidaction_entry
	| hidaction_entry_list hidaction_entry
	;

hidaction_entry
	: T_STRING T_NUM T_NUM T_STRING {
		$$ = calloc(1, sizeof(struct hidaction_config));
		if ($$ == NULL)
			err(1, "calloc");
		$$->usage = $1;
		$$->anyvalue = 0;
		$$->value = $2;
		$$->debounce = $3;
		$$->action = $4;
		STAILQ_INSERT_TAIL(&dconfig.haclist, $$, next);
	}
	| T_STRING '*' T_NUM T_STRING {
		$$ = calloc(1, sizeof(struct hidaction_config));
		if ($$ == NULL)
			err(1, "calloc");
		$$->usage = $1;
		$$->anyvalue = 1;
		$$->value = 0;
		$$->debounce = $3;
		$$->action = $4;
		STAILQ_INSERT_TAIL(&dconfig.haclist, $$, next);
	}
	;

%%

/* ARGSUSED */
static void
yyerror(const char *s)
{

	(void) s;
	syslog(LOG_ERR, "Syntax error in config file %s, line %d\n",
	    config_file, lineno);
	syslog(LOG_ERR, "Aborting...");
	exit(1);
}

static void
config_add_keymap_entry(char *usage, char *hex)
{
	int i, value;

	if (!strcasecmp(usage, "Reserved"))
		return;
	value = strtoul(hex, NULL, 16);
	if (value > 127)
		return;
	for (i = 0; i < usage_consumer_num; i++) {
		if (!strcasecmp(usage, usage_consumer[i])) {
			dconfig.cc_keymap[i] = value;
			break;
		}
	}
}

static void
config_add_keymap_entry_val(int i, char *hex)
{
	int value;

	value = strtoul(hex, NULL, 16);
	if (value > 127)
		return;
	if (i < _MAX_MM_KEY)
		dconfig.cc_keymap[i] = value;
}

void
config_init(void)
{

	STAILQ_INIT(&uconfig.dclist);
	STAILQ_INIT(&dconfig.haclist);
}

int
config_read_file(void)
{
	int r;

	if ((yyin = fopen(config_file, "r")) == NULL) {
		if (verbose)
			syslog(LOG_WARNING, "open %s failed: %s", config_file,
			    strerror(errno));
		return (-1);
	}

	r = 0;
	if (yyparse() < 0)
		r = -1;

	fclose(yyin);

	return (r);
}

struct device_config *
config_find_device(int vendor, int product, int iface)
{
	struct device_config *dc;

	STAILQ_FOREACH(dc, &uconfig.dclist, next) {
		if (dc->vendor_id == vendor && dc->product_id == product &&
		    (dc->interface == -1 || dc->interface == iface))
			break;
	}

	return (dc);
}

int
config_mouse_attach(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->mouse_attach)
		return (dc->mouse_attach);
	if (clconfig.mouse_attach)
		return (clconfig.mouse_attach);

	return (uconfig.gconfig.mouse_attach);
}

int
config_kbd_attach(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->kbd_attach)
		return (dc->kbd_attach);
	if (clconfig.kbd_attach)
		return (clconfig.kbd_attach);

	return (uconfig.gconfig.kbd_attach);
}

int
config_vhid_attach(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->vhid_attach)
		return (dc->vhid_attach);
	if (clconfig.vhid_attach)
		return (clconfig.vhid_attach);

	return (uconfig.gconfig.vhid_attach);
}

int
config_cc_attach(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->cc_attach)
		return (dc->cc_attach);
	if (clconfig.cc_attach)
		return (clconfig.cc_attach);

	return (uconfig.gconfig.cc_attach);
}

int
config_vhid_strip_id(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->vhid_strip_id)
		return (dc->vhid_strip_id);
	if (clconfig.vhid_strip_id)
		return (clconfig.vhid_strip_id);

	return (uconfig.gconfig.vhid_strip_id);
}

char *
config_vhid_devname(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->vhid_devname != NULL)
		return (dc->vhid_devname);
	if (clconfig.vhid_devname != NULL)
		return (clconfig.vhid_devname);

	return (uconfig.gconfig.vhid_devname);
}

int
config_detach_kernel_driver(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->detach_kernel_driver)
		return (dc->detach_kernel_driver);
	if (clconfig.detach_kernel_driver)
		return (clconfig.detach_kernel_driver);

	return (uconfig.gconfig.detach_kernel_driver);
}

int
config_forced_attach(struct hid_interface *hi)
{
	struct device_config *dc;

	dc = config_find_device(hi->vendor_id, hi->product_id, hi->ndx);
	if (dc != NULL && dc->forced_attach)
		return (dc->forced_attach);
	if (clconfig.forced_attach)
		return (clconfig.forced_attach);

	return (uconfig.gconfig.forced_attach);
}
