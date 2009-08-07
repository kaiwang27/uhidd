%{
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
#include <errno.h>
#include <stdio.h>
#include <stdio.h>
#include <syslog.h>

#include "uhidd.h"

extern int yylex(void);
extern int yyparse(void);
extern int lineno;
extern FILE *yyin;

const char *config_file = "/usr/local/etc/uhidd.conf";
struct uhidd_config uconfig;
struct device_config clconfig;

static struct device_config dconfig, *dconfig_p;

%}

%token T_YES
%token T_NO
%token T_ATTACHMOUSE
%token T_ATTACHKEYBOARD
%token T_ATTACHHID
%token T_DETACHKERNELDRIVER
%token T_ATTACHMOUSEASHID
%token T_ATTACHKEYBOARDASHID
%token T_STRIPREPORTID
%token T_GLOBAL
%token <val> T_NUM
%token <str> T_USAGE
%token <str> T_STRING
%type <hac> hidaction_conf

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
	: global_conf
	| device_conf
	;

global_conf
	: T_GLOBAL "{" conf_entry_list "}" {
		if (uconfig.gconfig.interface == -1)
			errx(1, "multiple global section in config file");
		dconfig.vendor_id = -1;
		dconfig.product_id = -1;
		dconfig.interface = -1;
		memcpy(&uconfig.gconfig, &dconfig,
		    sizeof(struct device_config));
		memset(&dconfig, 0 ,sizeof(struct device_config));
		STAILQ_INIT(&dconfig.halist);
	}
	;

device_conf
	: device_name "{" conf_entry_list "}" {
		dconfig_p = calloc(1, sizeof(struct device_config));
		if (dconfig_p == NULL)
			err(1, "calloc");
		memcpy(dconfig_p, &dconfig, sizeof(struct device_config));
		STAILQ_INSERT_TAIL(&uconfig.dclist, dconfig_p, next);
		memset(&dconfig, 0 ,sizeof(struct device_config));
		STAILQ_INIT(&dconfig.halist);
	}
	;

device_name
	: T_NUM ":" T_NUM {
		dconfig.vendor_id = $1;
		dconfig.product_id = $3;
		dconfig.interface = -1;
	}
	| T_NUM ":" T_NUM ":" T_NUM {
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
	: attachmouse
	| attachkeyboard
	| attachhid
	| detachkerneldriver
	| attachmouseashid
	| attachkeyboardashid
	| stripreportid
	| hidaction_conf {
		STAILQ_INSERT_TAIL(&dconfig.halist, $1, next);
	}
	;

attachmouse
	: T_ATTACHMOUSE T_YES {
		dconfig.attach_mouse = 1;
	}
	| T_ATTACHMOUSE T_NO {
		dconfig.attach_mouse = 0;
	}
	;

attachkeyboard
	: T_ATTACHKEYBOARD T_YES {
		dconfig.attach_kbd = 1;
	}
	| T_ATTACHKEYBOARD T_NO {
		dconfig.attach_kbd = 0;
	}
	;

attachhid
	: T_ATTACHHID T_YES {
		dconfig.attach_hid = 1;
	}
	| T_ATTACHHID T_NO {
		dconfig.attach_hid = 0;
	}
	;

detachkerneldriver
	: T_DETACHKERNELDRIVER T_YES {
		dconfig.detach_kernel_driver = 1;
	}
	| T_DETACHKERNELDRIVER T_NO {
		dconfig.detach_kernel_driver = 0;
	}
	;

attachmouseashid
	: T_ATTACHMOUSEASHID T_YES {
		dconfig.attach_mouse_as_hid = 1;
	}
	| T_ATTACHMOUSEASHID T_NO {
		dconfig.attach_mouse_as_hid = 0;
	}
	;

attachkeyboardashid
	: T_ATTACHKEYBOARDASHID T_YES {
		dconfig.attach_kbd_as_hid = 1;
	}
	| T_ATTACHKEYBOARDASHID T_NO {
		dconfig.attach_kbd_as_hid = 0;
	}
	;

stripreportid
	: T_STRIPREPORTID T_YES {
		dconfig.strip_report_id = 1;
	}
	| T_STRIPREPORTID T_NO {
		dconfig.strip_report_id = 0;
	}

hidaction_conf
	: T_STRING T_NUM T_NUM T_STRING {
		$$ = calloc(1, sizeof(struct hidaction_config));
		if ($$ == NULL)
			err(1, "calloc");
		$$->usage = $1;
		$$->anyvalue = 0;
		$$->value = $2;
		$$->debounce = $3;
		$$->action = $4;
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

void
config_init(void)
{

	STAILQ_INIT(&uconfig.dclist);
	STAILQ_INIT(&uconfig.gconfig.halist);
	STAILQ_INIT(&dconfig.halist);

	/*
	 * Set default values for command line config.
	 */
	clconfig.attach_mouse = -1;
	clconfig.attach_kbd = -1;
	clconfig.attach_hid = -1;
	clconfig.attach_mouse_as_hid = -1;
	clconfig.attach_kbd_as_hid = -1;
	clconfig.strip_report_id = -1;
}

int
config_read_file(void)
{
	int r;

	if ((yyin = fopen(config_file, "r")) == NULL) {
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
config_attach_mouse(struct hid_parent *hp)
{
	struct device_config *dc;

	dc = config_find_device(hp->vendor_id, hp->product_id, hp->ndx);
	if (dc != NULL)
		return (dc->attach_mouse);
	if (clconfig.attach_mouse != -1)
		return (clconfig.attach_mouse);

	return (uconfig.gconfig.attach_mouse);
}

int
config_attach_kbd(struct hid_parent *hp)
{
	struct device_config *dc;

	dc = config_find_device(hp->vendor_id, hp->product_id, hp->ndx);
	if (dc != NULL)
		return (dc->attach_kbd);
	if (clconfig.attach_kbd != -1)
		return (clconfig.attach_kbd);

	return (uconfig.gconfig.attach_kbd);
}

int
config_attach_hid(struct hid_parent *hp)
{
	struct device_config *dc;

	dc = config_find_device(hp->vendor_id, hp->product_id, hp->ndx);
	if (dc != NULL)
		return (dc->attach_hid);
	if (clconfig.attach_hid != -1)
		return (clconfig.attach_hid);

	return (uconfig.gconfig.attach_hid);
}

int
config_attach_mouse_as_hid(struct hid_parent *hp)
{
	struct device_config *dc;

	dc = config_find_device(hp->vendor_id, hp->product_id, hp->ndx);
	if (dc != NULL)
		return (dc->attach_mouse_as_hid);
	if (clconfig.attach_mouse_as_hid != -1)
		return (clconfig.attach_mouse_as_hid);

	return (uconfig.gconfig.attach_mouse_as_hid);
}

int
config_attach_kbd_as_hid(struct hid_parent *hp)
{
	struct device_config *dc;

	dc = config_find_device(hp->vendor_id, hp->product_id, hp->ndx);
	if (dc != NULL)
		return (dc->attach_kbd_as_hid);
	if (clconfig.attach_kbd_as_hid != -1)
		return (clconfig.attach_kbd_as_hid);

	return (uconfig.gconfig.attach_kbd_as_hid);
}

int
config_strip_report_id(struct hid_parent *hp)
{
	struct device_config *dc;

	dc = config_find_device(hp->vendor_id, hp->product_id, hp->ndx);
	if (dc != NULL)
		return (dc->strip_report_id);
	if (clconfig.strip_report_id != -1)
		return (clconfig.strip_report_id);

	return (uconfig.gconfig.strip_report_id);
}
