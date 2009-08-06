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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "y.tab.h"

#define YY_NO_UNPUT
int lineno = 1;

int	yylex(void);

%}

%option noyywrap

%%

yes			{ return (T_YES); }
no			{ return (T_NO); }
AttachMouse		{ return (T_ATTACHMOUSE); }
AttachKeyboard		{ return (T_ATTACHKEYBOARD); }
AttachHID		{ return (T_ATTACHHID); }
DetachKernelDriver	{ return (T_DETACHKERNELDRIVER); }
AttachMouseAsHID	{ return (T_ATTACHMOUSEASHID); }
AttachKeyboardAsHID	{ return (T_ATTACHKEYBOARDASHID); }
StripReportID		{ return (T_STRIPREPORTID); }
global			{ return (T_GLOBAL); }

0x[0-9a-fA-F]+		{
				yylval.val = strtoul(yytext, NULL, 16);
				return (T_NUM);
			}

[0-9]+			{
				yylval.val = strtol(yytext, NULL, 10);
				return (T_NUM);
			}

\"[^"]+\"		{
				yytext[yyleng - 1] = '\0';
				yylval.str = strdup(yytext + 1);
				return (T_STRING);
			}

\:			{ return (':'); }
\{			{ return ('{'); }
\}			{ return ('}'); }
\*			{ return ('*'); }
[ \t]			/* whitespace */
\#.*			/* comment */
"\n"			{ lineno++; }
