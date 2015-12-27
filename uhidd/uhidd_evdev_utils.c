/*-
 * Copyright (c) 2015 Kai Wang
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
#include <dev/usb/usbhid.h>

#include "uhidd.h"

/*
 * Translation table for HUP_KEYBOARD usages to evdev key codes.
 */
static const int k[] =
{
/*==========================================*/
/* Name                   HID code    evdev */
/*==========================================*/
/* No Event                     00 */ -1,
/* Overrun Error                01 */ -1,
/* POST Fail                    02 */ -1,
/* ErrorUndefined               03 */ -1,
/* a A                          04 */ 0x1E,
/* b B                          05 */ 0x30,
/* c C                          06 */ 0x2E,
/* d D                          07 */ 0x20,
/* e E                          08 */ 0x12,
/* f F                          09 */ 0x21,
/* g G                          0A */ 0x22,
/* h H                          0B */ 0x23,
/* i I                          0C */ 0x17,
/* j J                          0D */ 0x24,
/* k K                          0E */ 0x25,
/* l L                          0F */ 0x26,
/* m M                          10 */ 0x32,
/* n N                          11 */ 0x31,
/* o O                          12 */ 0x18,
/* p P                          13 */ 0x19,
/* q Q                          14 */ 0x10,
/* r R                          15 */ 0x13,
/* s S                          16 */ 0x1F,
/* t T                          17 */ 0x14,
/* u U                          18 */ 0x16,
/* v V                          19 */ 0x2F,
/* w W                          1A */ 0x11,
/* x X                          1B */ 0x2D,
/* y Y                          1C */ 0x15,
/* z Z                          1D */ 0x2C,
/* 1 !                          1E */ 0x02,
/* 2 @                          1F */ 0x03,
/* 3 #                          20 */ 0x04,
/* 4 $                          21 */ 0x05,
/* 5 %                          22 */ 0x06,
/* 6 ^                          23 */ 0x07,
/* 7 &                          24 */ 0x08,
/* 8 *                          25 */ 0x09,
/* 9 (                          26 */ 0x0A,
/* 0 )                          27 */ 0x0B,
/* Return                       28 */ 0x1C,
/* Escape                       29 */ 0x01,
/* Backspace                    2A */ 0x0E,
/* Tab                          2B */ 0x0F,
/* Space                        2C */ 0x39,
/* - _                          2D */ 0x0C,
/* = +                          2E */ 0x0D,
/* [ {                          2F */ 0x1A,
/* ] }                          30 */ 0x1B,
/* \ |                          31 */ 0x2B,
/* Europe 1                     32 */ 0x2B,
/* ; :                          33 */ 0x27,
/* " '                          34 */ 0x28,
/* ` ~                          35 */ 0x29,
/* comma <                      36 */ 0x33,
/* . >                          37 */ 0x34,
/* / ?                          38 */ 0x35,
/* Caps Lock                    39 */ 0x3A,
/* F1                           3A */ 0x3B,
/* F2                           3B */ 0x3C,
/* F3                           3C */ 0x3D,
/* F4                           3D */ 0x3E,
/* F5                           3E */ 0x3F,
/* F6                           3F */ 0x40,
/* F7                           40 */ 0x41,
/* F8                           41 */ 0x42,
/* F9                           42 */ 0x43,
/* F10                          43 */ 0x44,
/* F11                          44 */ 0x57,
/* F12                          45 */ 0x58,
/* Print Screen                 46 */ 0x63,
/* Scroll Lock                  47 */ 0x46,
/* Break (Ctrl-Pause)/Pause     48 */ 0x77,
/* Insert                       49 */ 0x6E,
/* Home                         4A */ 0x66,
/* Page Up                      4B */ 0x68,
/* Delete                       4C */ 0x6F,
/* End                          4D */ 0x6B,
/* Page Down                    4E */ 0x6D,
/* Right Arrow                  4F */ 0x6A,
/* Left Arrow                   50 */ 0x69,
/* Down Arrow                   51 */ 0x6C,
/* Up Arrow                     52 */ 0x67,
/* Num Lock                     53 */ 0x45,
/* Keypad /                     54 */ 0x62,
/* Keypad *                     55 */ 0x37,
/* Keypad -                     56 */ 0x4A,
/* Keypad +                     57 */ 0x4E,
/* Keypad Enter                 58 */ 0x60,
/* Keypad 1 End                 59 */ 0x4F,
/* Keypad 2 Down                5A */ 0x50,
/* Keypad 3 PageDn              5B */ 0x51,
/* Keypad 4 Left                5C */ 0x4B,
/* Keypad 5                     5D */ 0x4C,
/* Keypad 6 Right               5E */ 0x4D,
/* Keypad 7 Home                5F */ 0x47,
/* Keypad 8 Up                  60 */ 0x48,
/* Keypad 9 PageUp              61 */ 0x49,
/* Keypad 0 Insert              62 */ 0x52,
/* Keypad . Delete              63 */ 0x53,
/* Europe 2                     64 */ 0x56,
/* App                          65 */ 0x8B,
/* Keyboard Power               66 */ 0x74,
/* Keypad =                     67 */ 0x75,
/* F13                          68 */ 0xB7,
/* F14                          69 */ 0xB8,
/* F15                          6A */ 0xB9,
/* F16                          6B */ 0xBA,
/* F17                          6C */ 0xBB,
/* F18                          6D */ 0xBC,
/* F19                          6E */ 0xBD,
/* F20                          6F */ 0xBE,
/* F21                          70 */ 0xBF,
/* F22                          71 */ 0xC0,
/* F23                          72 */ 0xC1,
/* F24                          73 */ 0xC2,
/* Keyboard Execute             74 */ -1,
/* Keyboard Help                75 */ 0x8A,
/* Keyboard Menu                76 */ 0x8B,
/* Keyboard Select              77 */ 0x161,
/* Keyboard Stop                78 */ 0x80,
/* Keyboard Again               79 */ 0x81,
/* Keyboard Undo                7A */ 0x83,
/* Keyboard Cut                 7B */ 0x89,
/* Keyboard Copy                7C */ 0x85,
/* Keyboard Paste               7D */ 0x87,
/* Keyboard Find                7E */ 0x88,
/* Keyboard Mute                7F */ 0x71,
/* Keyboard Volume Up           80 */ 0x73,
/* Keyboard Volume Dn           81 */ 0x72,
/* Keyboard Locking Caps Lock   82 */ -1,
/* Keyboard Locking Num Lock    83 */ -1,
/* Keyboard Locking Scroll Lock 84 */ -1,
/* Keypad comma                 85 */ 0x79,
/* Keyboard Equal Sign          86 */ -1,
/* Keyboard Int'l 1             87 */ -1,   /* ??? */
/* Keyboard Int'l 2             88 */ 0x5D, /* HIRAGANA/KATAKANA */
/* Keyboard Int'l 2             89 */ 0x7C, /* YEN */
/* Keyboard Int'l 4             8A */ 0x5C, /* HENKAN/ZENKOUHO */
/* Keyboard Int'l 5             8B */ 0x5E, /* MUHENKAN */
/* Keyboard Int'l 6             8C */ -1,   /* ??? */
/* Keyboard Int'l 7             8D */ -1,
/* Keyboard Int'l 8             8E */ -1,
/* Keyboard Int'l 9             8F */ -1,
/* Keyboard Lang 1              90 */ -1,   /* ??? */
/* Keyboard Lang 2              91 */ -1,   /* ??? */
/* Keyboard Lang 3              92 */ -1,   /* ??? */
/* Keyboard Lang 4              93 */ -1,   /* ??? */
/* Keyboard Lang 5              94 */ -1,   /* ??? */
/* Keyboard Lang 6              95 */ -1,
/* Keyboard Lang 7              96 */ -1,
/* Keyboard Lang 8              97 */ -1,
/* Keyboard Lang 9              98 */ -1,
/* Keyboard Alternate Erase     99 */ 0xDE,
/* Keyboard SysReq/Attention    9A */ -1,
/* Keyboard Cancel              9B */ -1,
/* Keyboard Clear               9C */ 0x163,
/* Keyboard Prior               9D */ -1,
/* Keyboard Return              9E */ -1,
/* Keyboard Separator           9F */ -1,
/* Keyboard Out                 A0 */ -1,
/* Keyboard Oper                A1 */ -1,
/* Keyboard Clear/Again         A2 */ -1,
/* Keyboard CrSel/Props         A3 */ -1,
/* Keyboard ExSel               A4 */ -1,
/* Reserved                     A5 */ -1,
/* Reserved                     A6 */ -1,
/* Reserved                     A7 */ -1,
/* Reserved                     A8 */ -1,
/* Reserved                     A9 */ -1,
/* Reserved                     AA */ -1,
/* Reserved                     AB */ -1,
/* Reserved                     AC */ -1,
/* Reserved                     AD */ -1,
/* Reserved                     AE */ -1,
/* Reserved                     AF */ -1,
/* Keypad 00                    B0 */ -1,
/* Keypad 000                   B1 */ -1,
/* Thousands Separator          B2 */ -1,
/* Decimal Separator            B3 */ -1,
/* Currency Unit                B4 */ -1,
/* Currency Sub-unit            B5 */ -1,
/* Keypad (                     B6 */ 0xB3,
/* Keypad )                     B7 */ 0xB4,
/* Keypad {                     B8 */ -1,
/* Keypad }                     B9 */ -1,
/* Keypad Tab                   BA */ -1,
/* Keypad Backspace             BB */ -1,
/* Keypad A                     BC */ -1,
/* Keypad B                     BD */ -1,
/* Keypad C                     BE */ -1,
/* Keypad D                     BF */ -1,
/* Keypad E                     C0 */ -1,
/* Keypad F                     C1 */ -1,
/* Keypad XOR                   C2 */ -1,
/* Keypad ^                     C3 */ -1,
/* Keypad %                     C4 */ -1,
/* Keypad <                     C5 */ -1,
/* Keypad >                     C6 */ -1,
/* Keypad &                     C7 */ -1,
/* Keypad &&                    C8 */ -1,
/* Keypad |                     C9 */ -1,
/* Keypad ||                    CA */ -1,
/* Keypad :                     CB */ -1,
/* Keypad #                     CC */ -1,
/* Keypad Space                 CD */ -1,
/* Keypad @                     CE */ -1,
/* Keypad !                     CF */ -1,
/* Keypad Memory Store          D0 */ -1,
/* Keypad Memory Recall         D1 */ -1,
/* Keypad Memory Clear          D2 */ -1,
/* Keypad Memory Add            D3 */ -1,
/* Keypad Memory Subtract       D4 */ -1,
/* Keypad Memory Multiply       D5 */ -1,
/* Keypad Memory Divide         D6 */ -1,
/* Keypad +/-                   D7 */ 0x76,
/* Keypad Clear                 D8 */ -1,
/* Keypad Clear Entry           D9 */ -1,
/* Keypad Binary                DA */ -1,
/* Keypad Octal                 DB */ -1,
/* Keypad Decimal               DC */ -1,
/* Keypad Hexadecimal           DD */ -1,
/* Reserved                     DE */ -1,
/* Reserved                     DF */ -1,
/* Left Control                 E0 */ 0x1D,
/* Left Shift                   E1 */ 0x2A,
/* Left Alt                     E2 */ 0x38,
/* Left GUI                     E3 */ 0x7D,
/* Right Control                E4 */ 0x61,
/* Right Shift                  E5 */ 0x36,
/* Right Alt                    E6 */ 0x64,
/* Right GUI                    E7 */ 0x7E,
};

/*
 * Translation table for HUP_CONSUMER usages to evdev key codes.
 */
static int
evdev_consumer2key(uint16_t u)
{

	switch(u) {
	case 0x000: /* Unassigned                           */ return -1;
	case 0x001: /* Consumer Control                     */ return -1;
	case 0x002: /* Numeric Key Pad                      */ return -1;
	case 0x003: /* Programmable Buttons                 */ return -1;
	case 0x004: /* Microphone                           */ return -1;
	case 0x005: /* Headphone                            */ return -1;
	case 0x006: /* Graphic Equalizer                    */ return -1;
	case 0x020: /* +10                                  */ return -1;
	case 0x021: /* +100                                 */ return -1;
	case 0x022: /* AM/PM                                */ return -1;
	case 0x030: /* Power                                */ return -1;
	case 0x031: /* Reset                                */ return -1;
	case 0x032: /* Sleep                                */ return -1;
	case 0x033: /* Sleep After                          */ return -1;
	case 0x034: /* Sleep Mode                           */ return -1;
	case 0x035: /* Illumination                         */ return -1;
	case 0x036: /* Function Buttons                     */ return -1;
	case 0x040: /* Menu                                 */ return 0x8B;
	case 0x041: /* Menu Pick                            */ return -1;
	case 0x042: /* Menu Up                              */ return -1;
	case 0x043: /* Menu Down                            */ return -1;
	case 0x044: /* Menu Left                            */ return -1;
	case 0x045: /* Menu Right                           */ return -1;
	case 0x046: /* Menu Escape                          */ return -1;
	case 0x047: /* Menu Value Increase                  */ return -1;
	case 0x048: /* Menu Value Decrease                  */ return -1;
	case 0x060: /* Data On Screen                       */ return -1;
	case 0x061: /* Closed Caption                       */ return -1;
	case 0x062: /* Closed Caption Select                */ return -1;
	case 0x063: /* VCR/TV                               */ return -1;
	case 0x064: /* Broadcast Mode                       */ return -1;
	case 0x065: /* Snapshot                             */ return -1;
	case 0x066: /* Still                                */ return -1;
	case 0x080: /* Selection                            */ return -1;
	case 0x081: /* Assign Selection                     */ return -1;
	case 0x082: /* Mode Step                            */ return -1;
	case 0x083: /* Recall Last                          */ return 0x195;
	case 0x084: /* Enter Channel                        */ return -1;
	case 0x085: /* Order Movie                          */ return -1;
	case 0x086: /* Channel                              */ return 0x16B;
	case 0x087: /* Media Selection                      */ return -1;
	case 0x088: /* Media Select Computer                */ return 0x178;
	case 0x089: /* Media Select TV                      */ return 0x179;
	case 0x08A: /* Media Select WWW                     */ return -1;
	case 0x08B: /* Media Select DVD                     */ return 0x185;
	case 0x08C: /* Media Select Telephone               */ return 0xA9;
	case 0x08D: /* Media Select Program Guide           */ return 0x16A;
	case 0x08E: /* Media Select Video Phone             */ return 0x1A0;
	case 0x08F: /* Media Select Games                   */ return 0x1A1;
	case 0x090: /* Media Select Messages                */ return 0x18C;
	case 0x091: /* Media Select CD                      */ return 0x17F;
	case 0x092: /* Media Select VCR                     */ return 0x17B;
	case 0x093: /* Media Select Tuner                   */ return 0x182;
	case 0x094: /* Quit                                 */ return -1;
	case 0x095: /* Help                                 */ return 0x8A;
	case 0x096: /* Media Select Tape                    */ return 0x180;
	case 0x097: /* Media Select Cable                   */ return 0x17A;
	case 0x098: /* Media Select Satellite               */ return 0x17D;
	case 0x099: /* Media Select Security                */ return -1;
	case 0x09A: /* Media Select Home                    */ return 0x16E;
	case 0x09B: /* Media Select Call                    */ return -1;
	case 0x09C: /* Channel Increment                    */ return 0x192;
	case 0x09D: /* Channel Decrement                    */ return 0x193;
	case 0x09E: /* Media Select SAP                     */ return -1;
	case 0x0A0: /* VCR Plus                             */ return 0x17C;
	case 0x0A1: /* Once                                 */ return -1;
	case 0x0A2: /* Daily                                */ return -1;
	case 0x0A3: /* Weekly                               */ return -1;
	case 0x0A4: /* Monthly                              */ return -1;
	case 0x0B0: /* Play                                 */ return 0xC8;
	case 0x0B1: /* Pause                                */ return 0xC9;
	case 0x0B2: /* Record                               */ return 0xA7;
	case 0x0B3: /* Fast Forward                         */ return 0xD0;
	case 0x0B4: /* Rewind                               */ return 0xA8;
	case 0x0B5: /* Scan Next Track                      */ return 0xA3;
	case 0x0B6: /* Scan Previous Track                  */ return 0xA5;
	case 0x0B7: /* Stop                                 */ return 0xA6;
	case 0x0B8: /* Eject                                */ return 0xA1;
	case 0x0B9: /* Random Play                          */ return -1;
	case 0x0BA: /* Select DisC                          */ return -1;
	case 0x0BB: /* Enter Disc                           */ return -1;
	case 0x0BC: /* Repeat                               */ return -1;
	case 0x0BD: /* Tracking                             */ return -1;
	case 0x0BE: /* Track Normal                         */ return -1;
	case 0x0BF: /* Slow Tracking                        */ return -1;
	case 0x0C0: /* Frame Forward                        */ return -1;
	case 0x0C1: /* Frame Back                           */ return -1;
	case 0x0C2: /* Mark                                 */ return -1;
	case 0x0C3: /* Clear Mark                           */ return -1;
	case 0x0C4: /* Repeat From Mark                     */ return -1;
	case 0x0C5: /* Return To Mark                       */ return -1;
	case 0x0C6: /* Search Mark Forward                  */ return -1;
	case 0x0C7: /* Search Mark Backwards                */ return -1;
	case 0x0C8: /* Counter Reset                        */ return -1;
	case 0x0C9: /* Show Counter                         */ return -1;
	case 0x0CA: /* Tracking Increment                   */ return -1;
	case 0x0CB: /* Tracking Decrement                   */ return -1;
	case 0x0CC: /* Stop/Eject                           */ return -1;
	case 0x0CD: /* Play/Pause                           */ return 0xA4;
	case 0x0CE: /* Play/Skip                            */ return -1;
	case 0x0E0: /* Volume                               */ return -1;
	case 0x0E1: /* Balance                              */ return -1;
	case 0x0E2: /* Mute                                 */ return 0x71;
	case 0x0E3: /* Bass                                 */ return -1;
	case 0x0E4: /* Treble                               */ return -1;
	case 0x0E5: /* Bass Boost                           */ return 0xD1;
	case 0x0E6: /* Surround Mode                        */ return -1;
	case 0x0E7: /* Loudness                             */ return -1;
	case 0x0E8: /* MPX                                  */ return -1;
	case 0x0E9: /* Volume Increment                     */ return 0x73;
	case 0x0EA: /* Volume Decrement                     */ return 0x72;
	case 0x0F0: /* Speed Select                         */ return -1;
	case 0x0F1: /* Playback Speed                       */ return -1;
	case 0x0F2: /* Standard Play                        */ return -1;
	case 0x0F3: /* Long Play                            */ return -1;
	case 0x0F4: /* Extended Play                        */ return -1;
	case 0x0F5: /* Slow                                 */ return -1;
	case 0x100: /* Fan Enable                           */ return -1;
	case 0x101: /* Fan Speed                            */ return -1;
	case 0x102: /* Light Enable                         */ return -1;
	case 0x103: /* Light Illumination Level             */ return -1;
	case 0x104: /* Climate Control Enable               */ return -1;
	case 0x105: /* Room Temperature                     */ return -1;
	case 0x106: /* Security Enable                      */ return -1;
	case 0x107: /* Fire Alarm                           */ return -1;
	case 0x108: /* Police Alarm                         */ return -1;
	case 0x109: /* Proximity                            */ return -1;
	case 0x10A: /* Motion                               */ return -1;
	case 0x10B: /* Duress Alarm                         */ return -1;
	case 0x10C: /* Holdup Alarm                         */ return -1;
	case 0x10D: /* Medical Alarm                        */ return -1;
	case 0x150: /* Balance Right                        */ return -1;
	case 0x151: /* Balance Left                         */ return -1;
	case 0x152: /* Bass Increment                       */ return -1;
	case 0x153: /* Bass Decrement                       */ return -1;
	case 0x154: /* Treble Increment                     */ return -1;
	case 0x155: /* Treble Decrement                     */ return -1;
	case 0x160: /* Speaker System                       */ return -1;
	case 0x161: /* Channel Left                         */ return -1;
	case 0x162: /* Channel Right                        */ return -1;
	case 0x163: /* Channel Center                       */ return -1;
	case 0x164: /* Channel Front                        */ return -1;
	case 0x165: /* Channel Center Front                 */ return -1;
	case 0x166: /* Channel Side                         */ return -1;
	case 0x167: /* Channel Surround                     */ return -1;
	case 0x168: /* Channel Low Frequency Enhancement    */ return -1;
	case 0x169: /* Channel Top                          */ return -1;
	case 0x16A: /* Channel Unknown                      */ return -1;
	case 0x170: /* Sub-channel                          */ return -1;
	case 0x171: /* Sub-channel Increment                */ return -1;
	case 0x172: /* Sub-channel Decrement                */ return -1;
	case 0x173: /* Alternate Audio Increment            */ return -1;
	case 0x174: /* Alternate Audio Decrement            */ return -1;
	case 0x180: /* Application Launch Buttons           */ return -1;
	case 0x181: /* AL Launch Button Configuration Tool  */ return 0x240;
	case 0x182: /* AL Programmable Button Configuration */ return 0x240;
	case 0x183: /* AL Consumer Control Configuration    */ return 0xAB;
	case 0x184: /* AL Word Processor                    */ return 0x1A5;
	case 0x185: /* AL Text Editor                       */ return 0x1A6;
	case 0x186: /* AL Spreadsheet                       */ return 0x1A7;
	case 0x187: /* AL Graphics Editor                   */ return 0x1A8;
	case 0x188: /* AL Presentation App                  */ return 0x1A9;
	case 0x189: /* AL Database App                      */ return 0x1AA;
	case 0x18A: /* AL Email Reader                      */ return 0xD7;
	case 0x18B: /* AL Newsreader                        */ return 0x1AB;
	case 0x18C: /* AL Voicemail                         */ return 0x1AC;
	case 0x18D: /* AL Contacts/Address Book             */ return 0x1AD;
	case 0x18E: /* AL Calendar/Schedule                 */ return 0x18D;
	case 0x18F: /* AL Task/Project Manager              */ return 0x241;
	case 0x190: /* AL Log/Journal/Timecard              */ return 0x242;
	case 0x191: /* AL Checkbook/Finance                 */ return 0xDB;
	case 0x192: /* AL Calculator                        */ return 0x8C;
	case 0x193: /* AL A/V Capture/Playback              */ return -1;
	case 0x194: /* AL Local Machine Browser             */ return 0x90;
	case 0x195: /* AL LAN/WAN Browser                   */ return -1;
	case 0x196: /* AL Internet Browser                  */ return 0x96;
	case 0x197: /* AL Remote Networking/ISP Connect     */ return 0xDA;
	case 0x198: /* AL Network Conference                */ return -1;
	case 0x199: /* AL Network Chat                      */ return 0xD8;
	case 0x19A: /* AL Telephony/Dialer                  */ return -1;
	case 0x19B: /* AL Logon                             */ return -1;
	case 0x19C: /* AL Logoff                            */ return 0x1B1;
	case 0x19D: /* AL Logon/Logoff                      */ return -1;
	case 0x19E: /* AL Terminal Lock/Screensaver         */ return 0x98;
	case 0x19F: /* AL Control Panel                     */ return 0x243;
	case 0x1A0: /* AL Command Line Processor/Run        */ return -1;
	case 0x1A1: /* AL Process/Task Manager              */ return -1;
	case 0x1A2: /* AL Select Tast/Application           */ return 0x244;
	case 0x1A3: /* AL Next Task/Application             */ return -1;
	case 0x1A4: /* AL Previous Task/Application         */ return -1;
	case 0x1A5: /* AL Preemptive Halt Task/Application  */ return -1;
	case 0x1A6: /* AL Integrated Help Center            */ return 0x8A;
	case 0x1A7: /* AL Documents                         */ return 0xEB;
	case 0x1A8: /* AL Thesaurus                         */ return -1;
	case 0x1A9: /* AL Dictionary                        */ return -1;
	case 0x1AA: /* AL Desktop                           */ return -1;
	case 0x1AB: /* AC Spell Check                       */ return 0x1B0;
	case 0x1AC: /* AL Grammar Check                     */ return -1;
	case 0x1AD: /* AL Wireless Status                   */ return -1;
	case 0x1AE: /* AL Keyboard Layout                   */ return -1;
	case 0x1AF: /* AL Virus Protection                  */ return -1;
	case 0x1B0: /* AL Encryption                        */ return -1;
	case 0x1B1: /* AL Screen Saver                      */ return 0x245;
	case 0x1B2: /* AL Alarms                            */ return -1;
	case 0x1B3: /* AL Clock                             */ return -1;
	case 0x1B4: /* AL File Browser                      */ return -1;
	case 0x1B5: /* AL Power Status                      */ return -1;
	case 0x1B6: /* AL Image Browser                     */ return 0x1BA;
	case 0x1B7: /* AL Audio Browser                     */ return 0x188;
	case 0x1B8: /* AL Movie Browser                     */ return 0x189;
	case 0x1B9: /* AL Digital Rights Manager            */ return -1;
	case 0x1BA: /* AL Digital Wallet                    */ return -1;
	case 0x1BC: /* AL Instant Messaging                 */ return 0x1AE;
	case 0x1BD: /* AL OEM Feature/Tips/Tutorial Browser */ return 0x166;
	case 0x1BE: /* AL OEM Help                          */ return -1;
	case 0x1BF: /* AL Online Community                  */ return -1;
	case 0x1C0: /* AL Entertainment Content Browser     */ return -1;
	case 0x1C1: /* AL Online Shopping Browser           */ return -1;
	case 0x1C2: /* AL SmartCard Information/Help        */ return -1;
	case 0x1C3: /* AL Market Monitor/Finance Browser    */ return -1;
	case 0x1C4: /* AL Customized Corporate News Browser */ return -1;
	case 0x1C5: /* AL Online Activity Browser           */ return -1;
	case 0x1C6: /* AL Research/Search Browser           */ return -1;
	case 0x1C7: /* AL Audio Player                      */ return 0x183;
	case 0x200: /* Generic GUI Application Controls     */ return -1;
	case 0x201: /* AC New                               */ return 0xB5;
	case 0x202: /* AC Open                              */ return 0x86;
	case 0x203: /* AC Close                             */ return 0xCE;
	case 0x204: /* AC Exit                              */ return 0xAE;
	case 0x205: /* AC Maximize                          */ return -1;
	case 0x206: /* AC Minimize                          */ return -1;
	case 0x207: /* AC Save                              */ return 0xEA;
	case 0x208: /* AC Print                             */ return 0xD2;
	case 0x209: /* AC Properties                        */ return 0x82;
	case 0x21A: /* AC Undo                              */ return 0x83;
	case 0x21B: /* AC Copy                              */ return 0x85;
	case 0x21C: /* AC Cut                               */ return 0x89;
	case 0x21D: /* AC Paste                             */ return 0x87;
	case 0x21E: /* AC Select All                        */ return -1;
	case 0x21F: /* AC Find                              */ return 0x88;
	case 0x220: /* AC Find and Replace                  */ return -1;
	case 0x221: /* AC Search                            */ return 0x88;
	case 0x222: /* AC Go To                             */ return -1;
	case 0x223: /* AC Home                              */ return 0xAC;
	case 0x224: /* AC Back                              */ return 0x9E;
	case 0x225: /* AC Forward                           */ return 0x9F;
	case 0x226: /* AC Stop                              */ return 0x80;
	case 0x227: /* AC Refresh                           */ return 0xAD;
	case 0x228: /* AC Previous Link                     */ return -1;
	case 0x229: /* AC Next Link                         */ return -1;
	case 0x22A: /* AC Bookmarks                         */ return 0x9C;
	case 0x22B: /* AC History                           */ return -1;
	case 0x22C: /* AC Subscriptions                     */ return -1;
	case 0x22D: /* AC Zoom In                           */ return 0x1A2;
	case 0x22E: /* AC Zoom Out                          */ return 0x1A3;
	case 0x22F: /* AC Zoom                              */ return 0x1A4;
	case 0x230: /* AC Full Screen View                  */ return -1;
	case 0x231: /* AC Normal View                       */ return -1;
	case 0x232: /* AC View Toggle                       */ return -1;
	case 0x233: /* AC Scroll Up                         */ return 0xB1;
	case 0x234: /* AC Scroll Down                       */ return 0xB2;
	case 0x235: /* AC Scroll                            */ return -1;
	case 0x236: /* AC Pan Left                          */ return -1;
	case 0x237: /* AC Pan Right                         */ return -1;
	case 0x238: /* AC Pan                               */ return -1;
	case 0x239: /* AC New Window                        */ return -1;
	case 0x23A: /* AC Tile Horizontally                 */ return -1;
	case 0x23B: /* AC Tile Vertically                   */ return -1;
	case 0x23C: /* AC Format                            */ return -1;
	case 0x23D: /* AC Edit                              */ return 0xB0;
	case 0x23E: /* AC Bold                              */ return -1;
	case 0x23F: /* AC Italics                           */ return -1;
	case 0x240: /* AC Underline                         */ return -1;
	case 0x241: /* AC Strikethrough                     */ return -1;
	case 0x242: /* AC Subscript                         */ return -1;
	case 0x243: /* AC Superscript                       */ return -1;
	case 0x244: /* AC All Caps                          */ return -1;
	case 0x245: /* AC Rotate                            */ return -1;
	case 0x246: /* AC Resize                            */ return -1;
	case 0x247: /* AC Flip Horizontal                   */ return -1;
	case 0x248: /* AC Flip Vertical                     */ return -1;
	case 0x249: /* AC Mirror Horizontal                 */ return -1;
	case 0x24A: /* AC Mirror Vertical                   */ return -1;
	case 0x24B: /* AC Font Select                       */ return -1;
	case 0x24C: /* AC Font Color                        */ return -1;
	case 0x24D: /* AC Font Size                         */ return -1;
	case 0x24E: /* AC Justify Left                      */ return -1;
	case 0x24F: /* AC Justify Center H                  */ return -1;
	case 0x250: /* AC Justify Right                     */ return -1;
	case 0x251: /* AC Justify Block H                   */ return -1;
	case 0x252: /* AC Justify Top                       */ return -1;
	case 0x253: /* AC Justify Center V                  */ return -1;
	case 0x254: /* AC Justify Bottom                    */ return -1;
	case 0x255: /* AC Justify Block V                   */ return -1;
	case 0x256: /* AC Justify Decrease                  */ return -1;
	case 0x257: /* AC Justify Increase                  */ return -1;
	case 0x258: /* AC Numbered List                     */ return -1;
	case 0x259: /* AC Restart Numbering                 */ return -1;
	case 0x25A: /* AC Bulleted List                     */ return -1;
	case 0x25B: /* AC Promote                           */ return -1;
	case 0x25C: /* AC Demote                            */ return -1;
	case 0x25D: /* AC Yes                               */ return -1;
	case 0x25E: /* AC No                                */ return -1;
	case 0x25F: /* AC Cancel                            */ return 0xDF;
	case 0x260: /* AC Catalog                           */ return -1;
	case 0x261: /* AC Buy/Checkout                      */ return -1;
	case 0x262: /* AC Add to Cart                       */ return -1;
	case 0x263: /* AC Expand                            */ return -1;
	case 0x264: /* AC Expand All                        */ return -1;
	case 0x265: /* AC Collapse                          */ return -1;
	case 0x266: /* AC Collapse All                      */ return -1;
	case 0x267: /* AC Print Preview                     */ return -1;
	case 0x268: /* AC Paste Special                     */ return -1;
	case 0x269: /* AC Insert Mode                       */ return -1;
	case 0x26A: /* AC Delete                            */ return -1;
	case 0x26B: /* AC Lock                              */ return -1;
	case 0x26C: /* AC Unlock                            */ return -1;
	case 0x26D: /* AC Protect                           */ return -1;
	case 0x26E: /* AC Unprotect                         */ return -1;
	case 0x26F: /* AC Attach Comment                    */ return -1;
	case 0x270: /* AC Delete Comment                    */ return -1;
	case 0x271: /* AC View Comment                      */ return -1;
	case 0x272: /* AC Select Word                       */ return -1;
	case 0x273: /* AC Select Sentence                   */ return -1;
	case 0x274: /* AC Select Paragraph                  */ return -1;
	case 0x275: /* AC Select Column                     */ return -1;
	case 0x276: /* AC Select Row                        */ return -1;
	case 0x277: /* AC Select Table                      */ return -1;
	case 0x278: /* AC Select Object                     */ return -1;
	case 0x279: /* AC Redo/Repeat                       */ return 0xB6;
	case 0x27A: /* AC Sort                              */ return -1;
	case 0x27B: /* AC Sort Ascending                    */ return -1;
	case 0x27C: /* AC Sort Descending                   */ return -1;
	case 0x27D: /* AC Filter                            */ return -1;
	case 0x27E: /* AC Set Clock                         */ return -1;
	case 0x27F: /* AC View Clock                        */ return -1;
	case 0x280: /* AC Select Time Zone                  */ return -1;
	case 0x281: /* AC Edit Time Zones                   */ return -1;
	case 0x282: /* AC Set Alarm                         */ return -1;
	case 0x283: /* AC Clear Alarm                       */ return -1;
	case 0x284: /* AC Snooze Alarm                      */ return -1;
	case 0x285: /* AC Reset Alarm                       */ return -1;
	case 0x286: /* AC Synchronize                       */ return -1;
	case 0x287: /* AC Send/Receive                      */ return -1;
	case 0x288: /* AC Send To                           */ return -1;
	case 0x289: /* AC Reply                             */ return 0xE8;
	case 0x28A: /* AC Reply All                         */ return -1;
	case 0x28B: /* AC Forward Msg                       */ return 0xE9;
	case 0x28C: /* AC Send                              */ return 0xE7;
	case 0x28D: /* AC Attach File                       */ return -1;
	case 0x28E: /* AC Upload                            */ return -1;
	case 0x28F: /* AC Download (Save Target As)         */ return -1;
	case 0x290: /* AC Set Borders                       */ return -1;
	case 0x291: /* AC Insert Row                        */ return -1;
	case 0x292: /* AC Insert Column                     */ return -1;
	case 0x293: /* AC Insert File                       */ return -1;
	case 0x294: /* AC Insert Picture                    */ return -1;
	case 0x295: /* AC Insert Object                     */ return -1;
	case 0x296: /* AC Insert Symbol                     */ return -1;
	case 0x297: /* AC Save and Close                    */ return -1;
	case 0x298: /* AC Rename                            */ return -1;
	case 0x299: /* AC Merge                             */ return -1;
	case 0x29A: /* AC Split                             */ return -1;
	case 0x29B: /* AC Distribute Horizontally           */ return -1;
	case 0x29C: /* AC Distribute Vertically             */ return -1;
	default:    /* No Translation                       */ return -1;
	}
}

int
evdev_hid2key(struct hid_key *hk)
{

	if (hk->up == HUP_KEYBOARD) {
		if (hk->code < nitems(k))
			return (k[hk->code]);
		else
			return (-1);
	}

	if (hk->up == HUP_CONSUMER)
		return (evdev_consumer2key(hk->code));

	return (-1);
}
