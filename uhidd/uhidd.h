/*-
 * Copyright (c) 2009 Kai Wang
 * All rights reserved.
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

#include <sys/queue.h>

/*
 * HID parser.
 */

#define _MAX_RDESC_SIZE	16384
#define _MAX_REPORT_IDS	256
#define MAXUSAGE 100
#define HID_PAGE(u) (((u) >> 16) & 0xffff)
#define HID_USAGE(u) ((u) & 0xffff)

struct hid_parser {
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
};

typedef struct hid_parser *hid_parser_t;

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

typedef struct hid_data *hid_data_t;

/*
 * Configuration.
 */

struct hidaction_config {
	char *usage;
	int value;
	int anyvalue;
	int debounce;
	int lastseen;
	int lastused;
	char *action;
	STAILQ_ENTRY(hidaction_config) next;
};

struct device_config {
	int vendor_id;
	int product_id;
	int interface;
	int attach_mouse;
	int attach_kbd;
	int attach_hid;
	int detach_kernel_driver;
	int strip_report_id;
	STAILQ_HEAD(, hidaction_config) halist;
	STAILQ_ENTRY(device_config) next;
};

struct uhidd_config {
	struct device_config gconfig;
	STAILQ_HEAD(, hidaction_config) halist;
	STAILQ_HEAD(, device_config) dclist;
};

/*
 * Mouse device.
 */

#define	BUTTON_MAX	31

struct mouse_dev {
	int cons_fd;
	hid_item_t x;
	hid_item_t y;
	hid_item_t wheel;
	hid_item_t btn[BUTTON_MAX];
	int btn_cnt;
	int flags;
};

/*
 * Keyboard device.
 */

#define	MAX_KEYCODE	16

struct kbd_data {
	uint8_t mod;

#define	MOD_CONTROL_L	0x01
#define	MOD_CONTROL_R	0x10
#define	MOD_SHIFT_L	0x02
#define	MOD_SHIFT_R	0x20
#define	MOD_ALT_L	0x04
#define	MOD_ALT_R	0x40
#define	MOD_WIN_L	0x08
#define	MOD_WIN_R	0x80

	uint8_t keycode[MAX_KEYCODE];
	uint32_t time[MAX_KEYCODE];
};

struct kbd_dev {
	int vkbd_fd;
	hid_item_t mods;
	hid_item_t keys;
	int key_cnt;
	struct kbd_data ndata;
	struct kbd_data odata;
	pthread_t kbd_task;
	pthread_mutex_t kbd_mtx;
	uint32_t now;
	int delay1;
	int delay2;

#define KB_DELAY1	500
#define KB_DELAY2	100
};

/*
 * General HID device.
 */

struct hid_dev {
	int hidctl_fd;
	char *name;
};

/*
 * HID parent and child data structures.
 */

enum uhidd_ctype {
	UHIDD_MOUSE,
	UHIDD_KEYBOARD,
	UHIDD_HID
};

struct hidaction {
	struct hidaction_config *conf;
	hid_item_t item;
	int lastseen;
	int lastused;
	STAILQ_ENTRY(hidaction) next;
};

struct hid_child;

struct hid_parent {
	const char			*dev;
	struct libusb20_device		*pdev;
	struct libusb20_interface	*iface;
	int				 vendor_id;
	int				 product_id;
	int				 ndx;
	unsigned char			 rdesc[_MAX_RDESC_SIZE];
	int				 rsz;
	uint8_t				 ep;
	int				 pkt_sz;
	int				 child_cnt;
	pthread_t			 thread;
	STAILQ_HEAD(, hid_child)	 hclist;
	STAILQ_ENTRY(hid_parent)	 next;
};

struct hid_child {
	struct hid_parent	*parent;
	enum uhidd_ctype	 type;
	int			 ndx;
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
	hid_item_t		 env;
	hid_parser_t		 p;
	union {
		struct mouse_dev md;
		struct kbd_dev kd;
		struct hid_dev hd;
	} u;
	STAILQ_HEAD(, hidaction) halist;
	STAILQ_ENTRY(hid_child)	 next;
};

/*
 * Macros used for debugging/error/information output.
 */

#define PRINT0(d, n, ...)						\
	do {								\
		char pb[64], pb2[1024];					\
									\
		snprintf(pb, sizeof(pb), "%s[iface:%d]", d, n);		\
		snprintf(pb2, sizeof(pb2), __VA_ARGS__);		\
		printf("%s=> %s", pb, pb2);				\
	} while (0);

#define PRINT1(...)							\
	do {								\
		char pb[64], pb2[1024];					\
									\
		snprintf(pb, sizeof(pb), "%s[iface:%d]", hp->dev,	\
		    hp->ndx);						\
		snprintf(pb2, sizeof(pb2), __VA_ARGS__);		\
		printf("%s=> %s", pb, pb2);				\
	} while (0);

static inline const char *
type_name(enum uhidd_ctype t)
{

	switch (t) {
	case UHIDD_MOUSE: return "mouse";
	case UHIDD_KEYBOARD: return "kbd";
	case UHIDD_HID: return "hid";
	default: return "unknown";
	}
}

#define PRINT2(...)							\
	do {								\
		char pb[64], pb2[1024];					\
									\
		snprintf(pb, sizeof(pb), "%s[iface:%d][c%d:%s]",	\
		    hp->dev, hp->ndx, hc->ndx, type_name(hc->type));	\
		snprintf(pb2, sizeof(pb2), __VA_ARGS__);		\
		printf("%s=> %s", pb, pb2);				\
	} while (0);

/*
 * Globals.
 */

extern int verbose;
extern struct uhidd_config uconfig;
extern struct device_config clconfig;
extern const char *config_file;

/*
 * Prototypes.
 */

void		dump_report_desc(unsigned char *, int);
void		find_device_hidaction(struct hid_child *);
void		find_global_hidaction(struct hid_child *);
void		hexdump(unsigned char *, int);
int		hid_attach(struct hid_child *);
void		hid_recv(struct hid_child *, char *, int);
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
int		hid_get_array8(const void *, uint8_t *, const hid_item_t *);
void		hid_set_data(void *, const hid_item_t *, int);
int		kbd_attach(struct hid_child *);
void		kbd_recv(struct hid_child *, char *, int);
void		match_hidaction(struct hid_child *, struct hidaction_config *);
int		mouse_attach(struct hid_child *);
void		mouse_recv(struct hid_child *, char *, int);
struct device_config *config_find_device(int, int, int);
int		config_attach_mouse(struct hid_parent *);
int		config_attach_kbd(struct hid_parent *);
int		config_attach_hid(struct hid_parent *);
void		config_init(void);
int		config_read_file(void);
int		config_strip_report_id(struct hid_parent *);
void		run_hidaction(struct hid_child *, struct hidaction *, char *,
		    int);
const char	*usage_page(int);
const char	*usage_in_page(int, int);
