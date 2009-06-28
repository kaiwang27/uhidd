/*-
 * Copyright (c) 2009 Kai Wang
 * Copyright (c) 1999 Lennart Augustsson <augustss@netbsd.org>
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
 *
 * $FreeBSD$
 */

#define	_MAX_RDESC_SIZE	16384
#define	_MAX_REPORT_IDS	256

enum uhidd_ctype {
	UHIDD_MOUSE,
	UHIDD_KEYBOARD,
	UHIDD_HID
};

struct hid_parser;

typedef struct hid_parser *hid_parser_t;
typedef struct hid_data *hid_data_t;

typedef enum hid_kind {
	hid_input = 0,
	hid_output = 1,
	hid_feature = 2,
	hid_collection,
	hid_endcollection
} hid_kind_t;

typedef struct hid_item {
	/* Global */
	unsigned int _usage_page;
	int logical_minimum;
	int logical_maximum;
	int physical_minimum;
	int physical_maximum;
	int unit_exponent;
	int unit;
	int report_size;
	int report_ID;
#define NO_REPORT_ID 0
	int report_count;
	/* Local */
	unsigned int usage;
	int usage_minimum;
	int usage_maximum;
	int designator_index;
	int designator_minimum;
	int designator_maximum;
	int string_index;
	int string_minimum;
	int string_maximum;
	int set_delimiter;
	/* Misc */
	int collection;
	int collevel;
	enum hid_kind kind;
	unsigned int flags;
	/* Absolute data position (bits) */
	unsigned int pos;
	/* */
	struct hid_item *next;
} hid_item_t;

#define HID_PAGE(u) (((u) >> 16) & 0xffff)
#define HID_USAGE(u) ((u) & 0xffff)

hid_parser_t	hid_parser_alloc(unsigned char *rdesc, int rsz);
void		hid_parser_free(hid_parser_t p);
int		hid_get_report_id_num(hid_parser_t p);
hid_data_t	hid_start_parse(hid_parser_t p, int kindset);
void		hid_end_parse(hid_data_t s);
int		hid_get_item(hid_data_t s, hid_item_t *h, int id);
int		hid_report_size(hid_parser_t p, enum hid_kind k, int id);
int		hid_locate(hid_parser_t p, unsigned int u, enum hid_kind k,
    hid_item_t *h);
int		hid_get_data(const void *p, const hid_item_t *h);
void		hid_set_data(void *p, const hid_item_t *h, int data);
