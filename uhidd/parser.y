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

#include "extern.h"

extern int yylex(void);
extern int yyparse(void);
extern int lineno;
extern FILE *yyin;

const char *config_file = "uhidd.conf";
struct glob_config gconfig;

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
%token T_GENERIC
%token T_ATTACH
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
	: attachmouse
	| attachkeyboard
	| attachhid
	| detachkerneldriver
	| attachmouseashid
	| attachkeyboardashid
	| generic_conf
	;

generic_conf
	: T_GENERIC "{" generic_entry_list "}"
	;

generic_entry_list
	: hidaction_conf {
		STAILQ_INSERT_TAIL(&gconfig.halist, $1, next);
	}
	| generic_entry_list hidaction_conf {
		STAILQ_INSERT_TAIL(&gconfig.halist, $2, next);
	}
	;

device_conf
	: device_name "{" device_conf_entry_list "}" {
		dconfig_p = calloc(1, sizeof(struct device_config));
		if (dconfig_p == NULL)
			err(1, "calloc");
		memcpy(dconfig_p, &dconfig, sizeof(struct device_config));
		STAILQ_INSERT_TAIL(&gconfig.dclist, dconfig_p, next);
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

device_conf_entry_list
	: device_conf_entry
	| device_conf_entry_list device_conf_entry
	;

device_conf_entry
	: attach
	| hidaction_conf {
		STAILQ_INSERT_TAIL(&dconfig.halist, $1, next);
	}
	;

attach
	: T_ATTACH T_YES {
		dconfig.attach = 1;
	}
	| T_ATTACH T_NO {
		dconfig.attach = 0;
	}
	;

attachmouse
	: T_ATTACHMOUSE T_YES {
		gconfig.attach_mouse = 1;
	}
	| T_ATTACHMOUSE T_NO {
		gconfig.attach_mouse = 0;
	}
	;

attachkeyboard
	: T_ATTACHKEYBOARD T_YES {
		gconfig.attach_kbd = 1;
	}
	| T_ATTACHKEYBOARD T_NO {
		gconfig.attach_kbd = 0;
	}
	;

attachhid
	: T_ATTACHHID T_YES {
		gconfig.attach_hid = 1;
	}
	| T_ATTACHHID T_NO {
		gconfig.attach_hid = 0;
	}
	;

detachkerneldriver
	: T_DETACHKERNELDRIVER T_YES {
		gconfig.detach_kernel_driver = 1;
	}
	| T_DETACHKERNELDRIVER T_NO {
		gconfig.detach_kernel_driver = 0;
	}
	;

attachmouseashid
	: T_ATTACHMOUSEASHID T_YES {
		gconfig.attach_mouse_as_hid = 1;
	}
	| T_ATTACHMOUSEASHID T_NO {
		gconfig.attach_mouse_as_hid = 0;
	}
	;

attachkeyboardashid
	: T_ATTACHKEYBOARDASHID T_YES {
		gconfig.attach_kbd_as_hid = 1;
	}
	| T_ATTACHKEYBOARDASHID T_NO {
		gconfig.attach_kbd_as_hid = 0;
	}
	;

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
	fprintf(stderr, "Syntax error in config file %s, line %d\n",
	    config_file, lineno);
	exit(1);
}

int
read_config_file(void)
{
	int r;

	STAILQ_INIT(&gconfig.dclist);
	STAILQ_INIT(&gconfig.halist);
	STAILQ_INIT(&dconfig.halist);

	if ((yyin = fopen(config_file, "r")) == NULL) {
		fprintf(stderr, "open %s failed: %s", config_file,
		    strerror(errno));
		return (-1);
	}

	r = 0;
	if (yyparse() < 0)
		r = -1;

	fclose(yyin);

	return (r);
}
