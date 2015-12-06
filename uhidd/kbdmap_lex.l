/*-
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

%{

#include "kbdmap_lex.h"

%}

%option nounput
%option noinput
%option noyywrap

D	[0-9]
X	[0-9a-fA-F]
A	.
L	[OCNB]

%%

nop		{ return TNOP; }
lshift 		{ return TLSH; }
rshift 		{ return TRSH; }
clock 		{ return TCLK; }
nlock		{ return TNLK; }
slock 		{ return TSLK; }
lalt|alt 	{ return TLALT; }
btab		{ return TBTAB; }
lctrl|ctrl 	{ return TLCTR; }
nscr 		{ return TNEXT; }
pscr 		{ return TPREV; }
rctrl 		{ return TRCTR; }
ralt 		{ return TRALT; }
alock 		{ return TALK; }
ashift 		{ return TASH; }
meta 		{ return TMETA; }
boot 		{ return TRBT; }
debug 		{ return TDBG; }
susp		{ return TSUSP; }
saver		{ return TSPSC; }
panic		{ return TPANIC; }
lshifta|shifta	{ return TLSHA; }
rshifta		{ return TRSHA; }
lctrla|ctrla	{ return TLCTRA; }
rctrla		{ return TRCTRA; }
lalta|alta	{ return TLALTA; }
ralta		{ return TRALTA; }
halt		{ return THALT; }
pdwn		{ return TPDWN; }
paste		{ return TPASTE; }

NUL|nul		{ kbdmap_number = 0; return TNUM; }
SOH|soh		{ kbdmap_number = 1; return TNUM; }
STX|stx		{ kbdmap_number = 2; return TNUM; }
ETX|etx		{ kbdmap_number = 3; return TNUM; }
EOT|eot		{ kbdmap_number = 4; return TNUM; }
ENQ|enq		{ kbdmap_number = 5; return TNUM; }
ACK|ack		{ kbdmap_number = 6; return TNUM; }
BEL|bel		{ kbdmap_number = 7; return TNUM; }
BS|bs		{ kbdmap_number = 8; return TNUM; }
HT|ht		{ kbdmap_number = 9; return TNUM; }
LF|lf|NL|nl	{ kbdmap_number = 10; return TNUM; }
VT|vt		{ kbdmap_number = 11; return TNUM; }
FF|ff|NP|np	{ kbdmap_number = 12; return TNUM; }
CR|cr		{ kbdmap_number = 13; return TNUM; }
SO|so		{ kbdmap_number = 14; return TNUM; }
SI|si		{ kbdmap_number = 15; return TNUM; }
DLE|dle		{ kbdmap_number = 16; return TNUM; }
DC1|dc1		{ kbdmap_number = 17; return TNUM; }
DC2|dc2		{ kbdmap_number = 18; return TNUM; }
DC3|dc3		{ kbdmap_number = 19; return TNUM; }
DC4|dc4		{ kbdmap_number = 20; return TNUM; }
NAK|nak		{ kbdmap_number = 21; return TNUM; }
SYN|syn		{ kbdmap_number = 22; return TNUM; }
ETB|etb		{ kbdmap_number = 23; return TNUM; }
CAN|can		{ kbdmap_number = 24; return TNUM; }
EM|em		{ kbdmap_number = 25; return TNUM; }
SUB|sub		{ kbdmap_number = 26; return TNUM; }
ESC|esc		{ kbdmap_number = 27; return TNUM; }
FS|fs		{ kbdmap_number = 28; return TNUM; }
GS|gs		{ kbdmap_number = 29; return TNUM; }
RS|rs		{ kbdmap_number = 30; return TNUM; }
NS|ns		{ kbdmap_number = 31; return TNUM; }
US|us		{ kbdmap_number = 31; return TNUM; }
SP|sp		{ kbdmap_number = 32; return TNUM; }
DEL|del		{ kbdmap_number = 127; return TNUM; }

dgra|DGRA	{ kbdmap_number =  0; return TACC; }
dacu|DACU	{ kbdmap_number =  1; return TACC; }
dcir|DCIR	{ kbdmap_number =  2; return TACC; }
dtil|DTIL	{ kbdmap_number =  3; return TACC; }
dmac|DMAC	{ kbdmap_number =  4; return TACC; }
dbre|DBRE	{ kbdmap_number =  5; return TACC; }
ddot|DDOT	{ kbdmap_number =  6; return TACC; }
duml|DUML	{ kbdmap_number =  7; return TACC; }
ddia|DDIA	{ kbdmap_number =  7; return TACC; }
dsla|DSLA	{ kbdmap_number =  8; return TACC; }
drin|DRIN	{ kbdmap_number =  9; return TACC; }
dced|DCED	{ kbdmap_number =  10; return TACC; }
dapo|DAPO	{ kbdmap_number =  11; return TACC; }
ddac|DDAC	{ kbdmap_number =  12; return TACC; }
dogo|DOGO	{ kbdmap_number =  13; return TACC; }
dcar|DCAR	{ kbdmap_number =  14; return TACC; }

fkey{D}({D}*)		{ 
				sscanf(kbdmaptext+4, "%d", &kbdmap_number); 
				return TFUNC; 
			}
scr{D}({D}*)		{ 
				sscanf(kbdmaptext+3, "%d", &kbdmap_number); 
				return TSCRN; 
			}
'{A}'			{ kbdmap_letter = *(kbdmaptext+1); return TLET; }
#({A}*)			{ /* ignore */ }
0x{X}({X}*)  		{
				sscanf(kbdmaptext, "%x", &kbdmap_number);
				return TNUM;
			}
{D}({D}*)  		{
				sscanf(kbdmaptext, "%d", &kbdmap_number);
				return TNUM;
			}
{L}			{ 
				if (*kbdmaptext == 'O') kbdmap_number = 0;
				if (*kbdmaptext == 'C') kbdmap_number = 1;
				if (*kbdmaptext == 'N') kbdmap_number = 2;
				if (*kbdmaptext == 'B') kbdmap_number = 3;
				return TFLAG;
			}
\n		{ return TNEWLINE; }				
[ \t]		{ /* ignore */ }
.		{ return *kbdmaptext; }
