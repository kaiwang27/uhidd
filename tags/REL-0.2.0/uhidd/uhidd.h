/*-
 * Copyright (c) 2009, 2010 Kai Wang
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
 *
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <libgen.h>

/*
 * HID parser.
 */

#define _MAX_RDESC_SIZE	16384
#define _MAX_REPORT_IDS	256
#define	_MAX_MM_KEY	1024
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
	int logminsize;
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
	int hf_usage_page;
	int hf_usage_min;
	int hf_usage_max;
	int hf_logic_min;
	int hf_logic_max;
	unsigned int hf_nusage[MAXUSAGE];
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
	struct hid_appcol_driver *ha_drv;
	struct hid_parser *ha_hp;
	unsigned char ha_rdesc[_MAX_RDESC_SIZE];
	int ha_rsz;
	STAILQ_HEAD(, hid_report) ha_hrlist;
	STAILQ_ENTRY(hid_appcol) ha_next;
};

struct hid_parser {
	unsigned char		 rdesc[_MAX_RDESC_SIZE];
	int			 rsz;
	int			 rid[_MAX_REPORT_IDS];
	int			 nr;
	int			 hp_attached;
	void			*hp_data;
	int			 (*hp_write_callback)(void *, int, char *, int);
	STAILQ_HEAD(, hid_appcol) halist;
};

#define	HID_MATCH_NONE		0
#define	HID_MATCH_GHID		5
#define	HID_MATCH_GENERAL	10
#define	HID_MATCH_DEVICE	20

struct hid_appcol_driver {
	int (*ha_drv_match)(struct hid_appcol *);
	int (*ha_drv_attach)(struct hid_appcol *);
	void (*ha_drv_recv)(struct hid_appcol *, struct hid_report *);
	void (*ha_drv_recv_raw)(struct hid_appcol *, uint8_t *, int);
};

/*
 * Configuration.
 */

struct device_config {
	int vendor_id;
	int product_id;
	int interface;
	int mouse_attach;
	int kbd_attach;
	int vhid_attach;
	int cc_attach;
	int cc_keymap_set;
	uint8_t cc_keymap[_MAX_MM_KEY];
	int detach_kernel_driver;
	int vhid_strip_id;
	STAILQ_ENTRY(device_config) next;
};

struct uhidd_config {
	struct device_config gconfig;
	STAILQ_HEAD(, device_config) dclist;
};

/*
 * HID parent and child data structures.
 */

enum uhidd_ctype {
	UHIDD_MOUSE,
	UHIDD_KEYBOARD,
	UHIDD_HID
};

struct hid_interface {
	const char			*dev;
	struct libusb20_device		*pdev;
	struct libusb20_interface	*iface;
	int				 vendor_id;
	int				 product_id;
	struct hid_parser		*hp;
	int				 ndx;
	unsigned char			 rdesc[_MAX_RDESC_SIZE];
	int				 rsz;
	uint8_t				 ep;
	int				 pkt_sz;
	uint8_t				 cc_keymap[_MAX_MM_KEY];
	int				 free_key_pos;
	pthread_t			 thread;
	STAILQ_ENTRY(hid_interface)	 next;
};

/*
 * Macros used for debugging/error/information output.
 */

#define PRINT0(d, n, ...)						\
	do {								\
		char pb[64], pb2[1024];					\
									\
		snprintf(pb, sizeof(pb), "%s[%d]", basename(d), n);	\
		snprintf(pb2, sizeof(pb2), __VA_ARGS__);		\
		printf("%s-> %s", pb, pb2);				\
	} while (0);

#define PRINT1(...)							\
	do {								\
		char pb[64], pb2[1024];					\
									\
		snprintf(pb, sizeof(pb), "%s[%d]", basename(hi->dev),	\
		    hi->ndx);						\
		snprintf(pb2, sizeof(pb2), __VA_ARGS__);		\
		printf("%s-> %s", pb, pb2);				\
	} while (0);

/*
 * Globals.
 */

extern int verbose;
extern struct uhidd_config uconfig;
extern struct device_config clconfig;
extern const char *config_file;
extern int usage_consumer_num;
extern const char **usage_consumer;
extern int hid_appcol_driver_num;
extern struct hid_appcol_driver *hid_appcol_driver_list;
extern struct hid_appcol_driver *kbd_driver;
extern struct hid_appcol_driver *mouse_driver;
extern struct hid_appcol_driver *ghid_driver;
extern struct hid_appcol_driver *cc_driver;

/*
 * Prototypes.
 */

int		cc_match(struct hid_appcol *);
int		cc_attach(struct hid_appcol *);
void		cc_recv(struct hid_appcol *, struct hid_report *);
void		dump_report_desc(unsigned char *, int);
void		hexdump_report_desc(unsigned char *, int);
struct hid_parser *hid_parser_alloc(unsigned char *, int, void *);
void		hid_parser_free(struct hid_parser *);
void		hid_parser_input_data(struct hid_parser *, char *, int);
void		hid_parser_output_data(struct hid_parser *, int, char *,
		    int);
void		*hid_parser_get_private(struct hid_parser *);
void		hid_parser_set_private(struct hid_parser *, void *);
void		hid_parser_set_write_callback(struct hid_parser *,
		    int (*)(void *, int, char *, int));
void		hid_parser_attach_drivers(struct hid_parser *);
unsigned int	hid_appcol_get_usage(struct hid_appcol *);
void		hid_appcol_set_private(struct hid_appcol *, void *);
void		*hid_appcol_get_private(struct hid_appcol *);
struct hid_report *hid_appcol_get_next_report(struct hid_appcol *,
		    struct hid_report *);
void		*hid_appcol_get_parser_private(struct hid_appcol *);
void		hid_appcol_recv_data(struct hid_appcol *, struct hid_report *,
		    uint8_t *, int);
void		hid_appcol_xfer_data(struct hid_appcol *, struct hid_report *);
void		hid_appcol_xfer_raw_data(struct hid_appcol *, int, char *, int);
int		hid_report_get_id(struct hid_report *);
struct hid_field *hid_report_get_next_field(struct hid_report *,
    struct hid_field *, enum hid_kind);
int		hid_field_get_flags(struct hid_field *);
unsigned	hid_field_get_usage_page(struct hid_field *);
int		hid_field_get_usage_count(struct hid_field *);
void		hid_field_get_usage_value(struct hid_field *, int,
		    unsigned int *, int *);
int		hid_field_get_usage_min(struct hid_field *);
int		hid_field_get_usage_max(struct hid_field *);
void		hid_field_set_value(struct hid_field *, int, int);
int		kbd_match(struct hid_appcol *);
int		kbd_attach(struct hid_appcol *);
void		kbd_input(struct hid_appcol *, uint8_t, uint16_t *, int);
void		kbd_recv(struct hid_appcol *, struct hid_report *);
void		kbd_set_context(struct hid_appcol *, void *);
void		kbd_set_tr(struct hid_appcol *, int (*)(void *, int));
int		mouse_match(struct hid_appcol *);
int		mouse_attach(struct hid_appcol *);
void		mouse_recv(struct hid_appcol *, struct hid_report *);
struct device_config *config_find_device(int, int, int);
int		config_mouse_attach(struct hid_interface *);
int		config_kbd_attach(struct hid_interface *);
int		config_vhid_attach(struct hid_interface *);
int		config_cc_attach(struct hid_interface *);
void		config_init(void);
int		config_read_file(void);
int		config_vhid_strip_id(struct hid_interface *);
const char	*usage_page(int);
const char	*usage_in_page(int, int);
int		vhid_match(struct hid_appcol *);
int		vhid_attach(struct hid_appcol *);
void		vhid_recv_raw(struct hid_appcol *, uint8_t *, int);
