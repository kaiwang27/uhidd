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
 *
 * $FreeBSD$
 */

#include <sys/queue.h>

/*
 * HID parser.
 */

#define _MAX_RDESC_SIZE	16384
#define _MAX_REPORT_IDS	256
#define MAXUSAGE 4096
#define HID_PAGE(u) (((u) >> 16) & 0xffff)
#define HID_USAGE(u) ((u) & 0xffff)

/*
 * HID Main item kind.
 */
enum hid_kind {
	HID_INPUT = 0,
	HID_OUTPUT,
	HID_FEATURE
};

/*
 * HID item state table.
 */
struct hid_state {
	/* Global */
	unsigned int usage_page;
	int logical_minimum;
	int logical_maximum;
	int physical_minimum;
	int physical_maximum;
	int unit_exponent;
	int unit;
	int report_size;
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

	struct hid_state *stack_next;
};

struct hid_field {
	int hf_flags;
	int hf_pos;
	int hf_count;
	int hf_size;
	int hf_type;
	int hf_usage_min;
	int hf_usage_max;
	unsigned int *hf_usage;
	int *hf_value;
	STAILQ_ENTRY(hid_field) hf_next;
};

struct hid_report {
	int hr_id;
	unsigned int hr_pos[3];
	STAILQ_HEAD(, hid_field) hr_hflist[3];
	STAILQ_ENTRY(hid_report) hr_next;
};

/* HID application collection. */
struct hid_appcol {
	unsigned int ha_usage;
	void *ha_data;
	STAILQ_HEAD(, hid_report) ha_hrlist;
	STAILQ_ENTRY(hid_appcol) ha_next;
};

struct hid_interface {
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
	STAILQ_HEAD(, hid_appcol) halist;
};

#define	HID_MATCH_NONE		0
#define	HID_MATCH_GENERAL	10
#define	HID_MATCH_DEVICE	20

struct hid_driver {
	int (*match)(struct hid_appcol *);
	int (*attach)(struct hid_appcol *);
	void (*recv)(struct hid_appcol *, struct hid_report *);
};

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

#if 0
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
 * General HID device.
 */

struct hid_dev {
	int hidctl_fd;
	char *name;
};

struct hidaction {
	struct hidaction_config *conf;
	hid_item_t item;
	int lastseen;
	int lastused;
	STAILQ_ENTRY(hidaction) next;
};
#endif

/*
 * HID parent and child data structures.
 */

enum uhidd_ctype {
	UHIDD_MOUSE,
	UHIDD_KEYBOARD,
	UHIDD_HID
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
#if 0
	hid_item_t		 env;
#endif
	struct hid_interface	*hi;
#if 0
	union {
		struct mouse_dev md;
		struct kbd_dev kd;
		struct hid_dev hd;
	} u;
#endif
	STAILQ_HEAD(, hid_report) hrlist;
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
void		hexdump_report_desc(unsigned char *, int);
int		hid_attach(struct hid_child *);
void		hid_recv(struct hid_child *, char *, int);
struct hid_interface *hid_interface_alloc(unsigned char *, int);
void		hid_interface_free(struct hid_interface *);
unsigned int	hid_appcol_get_usage(struct hid_appcol *);
void		hid_appcol_set_private(struct hid_appcol *, void *);
void		*hid_appcol_get_private(struct hid_appcol *);
struct hid_report *hid_appcol_get_next_report(struct hid_appcol *,
    struct hid_report *);
int		hid_report_get_id(struct hid_report *);
struct hid_field *hid_report_get_next_field(struct hid_report *,
    struct hid_field *, enum hid_kind);
int		hid_field_get_flags(struct hid_field *);
int		hid_field_get_usage_count(struct hid_field *);
void		hid_field_get_usage_value(struct hid_field *, int,
    unsigned int *, int *);

#if 0
hid_data_t	hid_start_parse(hid_parser_t, int);
void		hid_end_parse(hid_data_t);
int		hid_get_item(hid_data_t, hid_item_t *, int);
int		hid_report_size(hid_parser_t, enum hid_kind, int);
int		hid_locate(hid_parser_t, unsigned int, enum hid_kind,
		    hid_item_t *);
int		hid_get_data(const void *, const hid_item_t *);
int		hid_get_array8(const void *, uint8_t *, const hid_item_t *);
void		hid_set_data(void *, const hid_item_t *, int);
#endif
void		kbd_cleanup(struct hid_child *);
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
