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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "y.tab.h"

int lineno = 1;

int	yylex(void);

%}

%option noyywrap
%option nounput
%option noinput

%%

default			{ return (T_DEFAULT); }
mouse_attach		{ return (T_MOUSE_ATTACH); }
kbd_attach		{ return (T_KBD_ATTACH); }
vhid_attach		{ return (T_VHID_ATTACH); }
vhid_strip_id		{ return (T_VHID_STRIP_REPORT_ID); }
vhid_devname		{ return (T_VHID_DEVNAME); }
cc_attach		{ return (T_CC_ATTACH); }
cc_keymap		{ return (T_CC_KEYMAP); }
hidaction		{ return (T_HIDACTION); }
detach_kernel_driver	{ return (T_DETACHKERNELDRIVER); }
forced_attach		{ return (T_FORCED_ATTACH); }

0x[0-9a-fA-F]+		{
				yylval.val = strtoul(yytext, NULL, 16);
				return (T_HEX);
			}

[0-9]+			{
				yylval.val = strtol(yytext, NULL, 10);
				return (T_NUM);
			}

\"[Yy][Ee][Ss]\"	{ return (T_YES); }

\"[Nn][Oo]\"		{ return (T_NO); }

\"evdev\"		{ return (T_EVDEV); }

\"evdev\+\"		{ return (T_EVDEVP); }

\"[^"]+\"		{
				yytext[yyleng - 1] = '\0';
				yylval.str = strdup(yytext + 1);
				return (T_STRING);
			}

[a-zA-Z_/]+		{
				yytext[yyleng] = '\0';
				yylval.str = strdup(yytext);
				return (T_USAGE);
}

\:			{ return (':'); }
\{			{ return ('{'); }
\}			{ return ('}'); }
\*			{ return ('*'); }
=			{ return ('='); }
[ \t]			/* whitespace */
\#.*			/* comment */
"\n"			{ lineno++; }
