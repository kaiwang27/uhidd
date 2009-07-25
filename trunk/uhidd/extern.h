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
 * $FreeBSD: trunk/uhidd/extern.h 22 2009-07-24 01:10:34Z kaiw27 $
 */

#include <sys/queue.h>

#define	_MAX_RDESC_SIZE	16384
#define	_MAX_REPORT_IDS	256
#define MAXUSAGE 100

extern int debug;

enum uhidd_ctype {
	UHIDD_MOUSE,
	UHIDD_KEYBOARD,
	UHIDD_HID
};

struct hid_parser {
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
};

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

struct hid_data {
	u_char *start;
	u_char *end;
	u_char *p;
	hid_item_t cur;
	unsigned int usages[MAXUSAGE];
	int nusage;
	int minset;
	int logminsize;
	int multi;
	int multimax;
	int kindset;
	/*
	 * Absolute data position (bits) for input/output/feature of each
	 * report id. Assumes that hid_input, hid_output and hid_feature have
	 * values 0, 1 and 2.
	 */
	unsigned int kindpos[_MAX_REPORT_IDS][3];
};

#define	BUTTON_MAX	31

struct mouse_dev {
	hid_item_t x;
	hid_item_t y;
	hid_item_t wheel;
	hid_item_t btn[BUTTON_MAX];
	int btn_cnt;
	int flags;
};

struct hid_child;

struct hid_parent {
	const char			*dev;
	struct libusb20_device		*pdev;
	struct libusb20_interface	*iface;
	int				 ndx;
	unsigned char			 rdesc[_MAX_RDESC_SIZE];
	int				 rsz;
	uint8_t				 ep;
	int				 pkt_sz;
	pthread_t			 thread;
	STAILQ_HEAD(, hid_child)	 hclist;
	STAILQ_ENTRY(hid_parent)	 next;
};

struct hid_child {
	struct hid_parent	*parent;
	enum uhidd_ctype	 type;
	int			 cons_fd;
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
	hid_item_t		 env;
	hid_parser_t		 p;
	union {
		struct mouse_dev md;
	} u;
	STAILQ_ENTRY(hid_child)	 next;
};

#define HID_PAGE(u) (((u) >> 16) & 0xffff)
#define HID_USAGE(u) ((u) & 0xffff)

void		dump_report_desc(unsigned char *, int);
hid_parser_t	hid_parser_alloc(unsigned char *, int);
void		hid_parser_free(hid_parser_t);
int		hid_get_report_id_num(hid_parser_t);
void		hid_get_report_ids(hid_parser_t, int *, int);
hid_data_t	hid_start_parse(hid_parser_t, int);
void		hid_end_parse(hid_data_t);
int		hid_get_item(hid_data_t, hid_item_t *, int);
int		hid_report_size(hid_parser_t, enum hid_kind, int);
int		hid_locate(hid_parser_t, unsigned int, enum hid_kind,
		    hid_item_t *);
int		hid_get_data(const void *, const hid_item_t *);
void		hid_set_data(void *, const hid_item_t *, int);
void		mouse_attach(struct hid_child *);
void		mouse_recv(struct hid_child *, char *, int);
const char	*usage_page(int);
const char	*usage_in_page(int, int);
