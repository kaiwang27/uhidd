/*-
 * Copyright (c) 2009, 2010, 2012, 2015 Kai Wang
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

#define _TR_BUFSIZE 4096
#define _MAX_RDESC_SIZE	16384
#define _MAX_REPORT_IDS	256
#define	_MAX_MM_KEY	1024
#define MAXUSAGE 4096
#define HID_PAGE(u) (((u) >> 16) & 0xffff)
#define HID_USAGE(u) ((u) & 0xffff)
#define HID_CUSAGE(u,v) (((u) & 0xffff) << 16 | ((v) & 0xffff))

/*
 * HID usage.
 */

#define	HUG_CONSUMER_CONTROL		0x0001
#define	HUG_MENU			0x0040
#define	HUG_RECALL_LAST			0x0083
#define	HUG_MEDIA_SEL_PC		0x0088
#define	HUG_MEDIA_SEL_TV		0x0089
#define	HUG_MEDIA_SEL_WWW		0x008A
#define	HUG_MEDIA_SEL_DVD		0x008B
#define	HUG_MEDIA_SEL_PHONE		0x008C
#define	HUG_MEDIA_SEL_PROGRAM		0x008D
#define	HUG_MEDIA_SEL_VIDEOPHONE	0x008E
#define	HUG_MEDIA_SEL_GAMES		0x008F
#define	HUG_MEDIA_SEL_MEMO		0x0090
#define	HUG_MEDIA_SEL_CD		0x0091
#define	HUG_MEDIA_SEL_VCR		0x0092
#define	HUG_MEDIA_SEL_TUNER		0x0093
#define	HUG_QUIT			0x0094
#define	HUG_HELP			0x0095
#define	HUG_MEDIA_SEL_TAPE		0x0096
#define	HUG_MEDIA_SEL_CABLE		0x0097
#define	HUG_MEDIA_SEL_SATELLITE		0x0098
#define	HUG_MEDIA_SEL_HOME		0x009A
#define	HUG_CHANNEL_UP			0x009C
#define	HUG_CHANNEL_DOWN		0x009D
#define	HUG_VCR_PLUS			0x00A0
#define	HUG_PLAY			0x00B0
#define	HUG_PAUSE			0x00B1
#define	HUG_RECORD			0x00B2
#define	HUG_FAST_FORWARD		0x00B3
#define	HUG_REWIND			0x00B4
#define	HUG_NEXT_TRACK			0x00B5
#define	HUG_PREVIOUS_TRACK		0x00B6
#define	HUG_STOP			0x00B7
#define	HUG_EJECT			0x00B8
#define	HUG_RANDOM_PLAY			0x00B9
#define	HUG_REPEAT			0x00BC
#define	HUG_PLAYPAUSE			0x00CD
#define	HUG_VOLUME			0x00E0
#define	HUG_BALANCE			0x00E1
#define	HUG_MUTE			0x00E2
#define	HUG_BASE			0x00E3
#define	HUG_TREBLE			0x00E4
#define	HUG_BASE_BOOST			0x00E5
#define	HUG_VOLUME_UP			0x00E9
#define	HUG_VOLUME_DOWN			0x00EA
#define	HUG_AL_WORDPROCESSOR		0x0184
#define	HUG_AL_TEXTEDITOR		0x0185
#define	HUG_AL_SPREADSHEET		0x0186
#define	HUG_AL_GRAPHICSEDITOR		0x0187
#define	HUG_AL_PRESENTATION		0x0188
#define	HUG_AL_DATABASE			0x0189
#define	HUG_AL_EMAIL			0x018A
#define	HUG_AL_NEWS			0x018B
#define	HUG_AL_VOICEMAIL		0x018C
#define	HUG_AL_ADDRESSBOOK		0x018D
#define	HUG_AL_CALENDAR			0x018E
#define	HUG_AL_PROJECT			0x018F
#define	HUG_AL_JOURNAL			0x0190
#define	HUG_AL_FINANCE			0x0191
#define	HUG_AL_CALCULATOR		0x0192
#define	HUG_AL_CAPTURE			0x0193
#define	HUG_AL_FILEBROWSER		0x0194
#define	HUG_AL_LANBROWSER		0x0195
#define	HUG_AL_INTERNETBROWSER		0x0196
#define	HUG_AL_LOGOFF			0x019C
#define	HUG_AL_TERMINALLOCK		0x019E
#define	HUG_AL_HELP			0x01A6
#define	HUG_AL_DOCUMENTS		0x01A7
#define	HUG_AL_SPELLCHECK		0x01AB
#define	HUG_AL_IMAGE_BROWSER		0x01B6
#define	HUG_AL_SOUND_BROWSER		0x01B7
#define	HUG_AL_MESSENGER		0x01BC
#define	HUG_AC_NEW			0x0201
#define	HUG_AC_OPEN			0x0202
#define	HUG_AC_CLOSE			0x0203
#define	HUG_AC_EXIT			0x0204
#define	HUG_AC_MAXIMIZE			0x0205
#define	HUG_AC_MINIMIZE			0x0206
#define	HUG_AC_SAVE			0x0207
#define	HUG_AC_PRINT			0x0208
#define	HUG_AC_PROPERTIES		0x0209
#define	HUG_AC_UNDO			0x021A
#define	HUG_AC_COPY			0x021B
#define	HUG_AC_CUT			0x021C
#define	HUG_AC_PASTE			0x021D
#define	HUG_AC_FIND			0x021F
#define	HUG_AC_SEARCH			0x0221
#define	HUG_AC_GOTO			0x0222
#define	HUG_AC_HOME			0x0223
#define	HUG_AC_BACK			0x0224
#define	HUG_AC_FORWARD			0x0225
#define	HUG_AC_STOP			0x0226
#define	HUG_AC_REFRESH			0x0227
#define	HUG_AC_BOOKMARKS		0x022A
#define	HUG_AC_HISTORY			0x022B
#define	HUG_AC_ZOOMIN			0x022D
#define	HUG_AC_ZOOMOUT			0x022E
#define	HUG_AC_ZOOM			0x022F
#define	HUG_AC_SCROLL_UP		0x0233
#define	HUG_AC_SCROLL_DOWN		0x0234
#define	HUG_AC_PAN			0x0238
#define	HUG_AC_CANCEL			0x025F
#define	HUG_AC_REDO			0x0279
#define	HUG_AC_REPLY			0x0289
#define	HUG_AC_FORWARDMSG		0x028B
#define	HUG_AC_SEND			0x028C

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
	int hf_nusage_count;
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

/*
 * HID action related structures.
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

struct hidaction {
	struct hidaction_config *conf;
	struct hid_report *hr;
	struct hid_field *hf;
	unsigned int usage;
	int lastseen;
	int lastused;
	STAILQ_ENTRY(hidaction) next;
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
	STAILQ_HEAD(, hidaction) ha_haclist;
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

struct hid_key {
	uint16_t up;
	uint16_t code;
};

struct hid_scancode {
	int sc;
	int make;
};

typedef int (*hid_translator)(struct hid_appcol *, struct hid_key, int,
    struct hid_scancode *, int);

/*
 * Configuration.
 */

enum attach_mode {
	ATTACH_UNKNOWN = 0,
	ATTACH_NO,
	ATTACH_YES,
	ATTACH_VMS,
	ATTACH_EVDEV,
	ATTACH_EVDEVP,
	ATTACH_DEDEVP_VMS,
};

struct device_config {
	int vendor_id;
	int product_id;
	int interface;
	enum attach_mode mouse_attach;
	enum attach_mode kbd_attach;
	enum attach_mode vhid_attach;
	enum attach_mode cc_attach;
	uint8_t cc_keymap_set;
	uint8_t cc_keymap[_MAX_MM_KEY];
	int8_t detach_kernel_driver;
	int8_t forced_attach;
	int8_t vhid_strip_id;
	char *vhid_devname;
	STAILQ_HEAD(, hidaction_config) haclist;
	STAILQ_ENTRY(device_config) next;
};

struct uhidd_config {
	struct device_config gconfig;
	STAILQ_HEAD(, device_config) dclist;
};

struct uhidd_devid {
	int vendor_id;
	int product_id;
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
	int (*cc_recv_filter)(struct hid_appcol *, unsigned, int, unsigned *,
	    int *);
	STAILQ_ENTRY(hid_interface)	 next;
};

/*
 * HID driver structures.
 */

#define	HID_MATCH_NONE		0
#define	HID_MATCH_GHID		5
#define	HID_MATCH_GENERAL	10
#define	HID_MATCH_DEVICE	20

#define HID_FILTER_DISCARD	-1
#define HID_FILTER_KEEP		0
#define HID_FILTER_REPLACE	1

struct hid_interface_driver {
	int (*hi_drv_match)(struct hid_interface *);
	int (*hi_drv_attach)(struct hid_interface *);
};

struct hid_appcol_driver {
	const char *ha_drv_name;
	int (*ha_drv_match)(struct hid_appcol *);
	int (*ha_drv_attach)(struct hid_appcol *);
	void (*ha_drv_recv)(struct hid_appcol *, struct hid_report *);
	void (*ha_drv_recv_raw)(struct hid_appcol *, uint8_t *, int);
};

/* evdev callbacks. */
struct evdev_cb {
	void *(*get_hid_interface)(void *);
	void *(*get_hid_appcol)(void *);
	void (*get_repeat_delay)(void *, int *, int *);
	void (*set_repeat_delay)(void *, int, int);
};

/*
 * Macros used for debugging/error/information output.
 */

#define PRINT0(v, d, n, ...)						\
	do {								\
		if (verbose >= (v)) {					\
			char pb[64], pb2[1024];				\
			snprintf(pb, sizeof(pb), "%s[%d]", basename(d),	\
			    (n));					\
			snprintf(pb2, sizeof(pb2), __VA_ARGS__);	\
			printf("%s-> %s", pb, pb2);			\
		}							\
	} while (0)

#define PRINT1(v, ...)							\
	do {								\
		if (verbose >= (v)) {					\
			char pb[64], pb2[1024];				\
			snprintf(pb, sizeof(pb), "%s[%d]",		\
			    basename(hi->dev), hi->ndx);		\
			snprintf(pb2, sizeof(pb2), __VA_ARGS__);	\
			printf("%s-> %s", pb, pb2);			\
		}							\
	} while (0)

/*
 * Globals.
 */

extern int verbose;
extern struct uhidd_config uconfig;
extern struct device_config clconfig;
extern const char *config_file;
extern int usage_consumer_num;
extern const char **usage_consumer;
extern const int hid_appcol_driver_num;
extern const int hid_interface_driver_num;
extern struct hid_appcol_driver hid_appcol_driver_list[];
extern struct hid_interface_driver hid_interface_driver_list[];

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
const char	*hid_appcol_get_driver_name(struct hid_appcol *);
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
int		hid_handle_kernel_driver(struct hid_parser *);
int		hid_match_devid(struct hid_interface *, struct uhidd_devid *,
		    int);
int		hid_match_interface(struct hid_interface *, int, int, int);
int		kbd_match(struct hid_appcol *);
int		kbd_attach(struct hid_appcol *);
int		kbd_hid2key(struct hid_appcol *, struct hid_key, int,
    struct hid_scancode *, int);
void		kbd_input(struct hid_appcol *, uint8_t, struct hid_key *, int);
void		kbd_recv(struct hid_appcol *, struct hid_report *);
void		kbd_set_tr(struct hid_appcol *, hid_translator);
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
char		*config_vhid_devname(struct hid_interface *);
int		config_detach_kernel_driver(struct hid_interface *);
int		config_forced_attach(struct hid_interface *);
void		find_hidaction(struct hid_appcol *);
void		run_hidaction(struct hid_appcol *, struct hid_report *);
int		ucuse_init(void);
int		ucuse_create_worker(void);
int		ucuse_copy_out_string(const char *, void *, int);
const char	*usage_page(int);
const char	*usage_in_page(int, int);
int		vhid_match(struct hid_appcol *);
int		vhid_attach(struct hid_appcol *);
void		vhid_recv_raw(struct hid_appcol *, uint8_t *, int);
struct evdev_dev *evdev_register_device(void *, struct evdev_cb *);
void		evdev_report_key_event(struct evdev_dev *, int, int, int);
void		evdev_report_key_repeat_event(struct evdev_dev *, int);
void		evdev_sync_report(struct evdev_dev *);
const char	*evdev_devname(struct evdev_dev *);
int		evdev_hid2key(struct hid_key *);
int		microsoft_match(struct hid_interface *);
int		microsoft_attach(struct hid_interface *);
