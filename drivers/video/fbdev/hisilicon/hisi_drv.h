#ifndef HISI_DRV_H_
#define HISI_DRV_H_

#include "hisi_chip.h"


#define _version_	"1.0.0"

/* Vendor and Device id for HISILICON Graphics chip*/
#define PCI_VENDOR_ID_HIS 0x19e5
#define PCI_DEVID_HS_VGA 0x1711


#define _moduleName_ "hisifb"
#define PFX _moduleName_ ": "
#define err_msg(fmt, args...) pr_err(PFX fmt, ## args)
#define war_msg(fmt, args...) pr_warn(PFX fmt, ## args)
#define inf_msg(fmt, args...) pr_info(PFX fmt, ## args)
#define dbg_msg(fmt, args...) pr_debug(PFX fmt, ## args)

#define MB(x) ((x) << 20)
#define MHZ(x) ((x) * 1000000)
/* align should be 2,4,8,16 */
#define PADDING(align, data) (((data) + (align) - 1)&(~((align) - 1)))


struct hisi_accel {
	/* base virtual address of DPR registers */
	unsigned char __iomem *dpr_base;
	/* base virtual address of de data port */
	unsigned char __iomem *dp_port_base;

	/* function fointers */
	int (*de_init)(struct hisi_accel *);

	int (*de_wait)(void);/* see if hardware ready to work */

	int (*de_fillrect)(struct hisi_accel *, u32, u32, u32,
		u32, u32, u32, u32, u32, u32);

	int (*de_copyarea)(struct hisi_accel *, u32, u32, u32,
		u32, u32, u32, u32, u32, u32, u32, u32, u32);

	int (*de_imageblit)(struct hisi_accel *, const char *,
		u32, u32, u32, u32, u32, u32, u32,
		u32, u32, u32, u32, u32);
};

/* hisi_share stands for a presentation of two frame buffer
* that use one smi adaptor , it is similar to a basic class of C++
*/
struct hisi_share {
	/* driver members */
	struct pci_dev *pdev;
	struct fb_info *fbinfo[2];
	struct hisi_accel accel;
	int accel_off;
	int dual;
#ifdef CONFIG_MTRR
	int mtrr_off;
	struct {
		int vram;
		int vram_added;
	} mtrr;
#endif
	/* all smi graphic adaptor get below attributes */
	resource_size_t vidmem_start;
	resource_size_t vidreg_start;
	resource_size_t vidmem_size;
	resource_size_t vidreg_size;
	unsigned char __iomem *pvreg;
	unsigned char __iomem *pvmem;
	/* function pointers */
	void (*suspend)(void);
	void (*resume)(void);
};

struct hisi_cursor {
	/* cursor width ,height and size */
	int w;
	int h;
	int size;
	/* hardware limitation */
	int maxW;
	int maxH;
	/* base virtual address and offset  of cursor image */
	char __iomem *vstart;
	int offset;
	/* mmio addr of hw cursor */
	char __iomem *mmio;
	/* the hisi_share of this adaptor */
	struct hisi_share *share;
	/* proc_routines */
	void (*enable)(struct hisi_cursor *);
	void (*disable)(struct hisi_cursor *);
	void (*set_size)(struct hisi_cursor *, int, int);
	void (*set_pos)(struct hisi_cursor *, int, int);
	void (*set_color)(struct hisi_cursor *, u32, u32);
	void (*set_data)(struct hisi_cursor *, u16, const u8*, const u8*);
};

struct hisifb_crtc {
	unsigned char __iomem *vcursor;/*virtual address of cursor*/
	unsigned char __iomem *vscreen;/*virtual address of on_screen*/
	int ocursor;/*cursor address offset in vidmem*/
	int oscreen;/*onscreen address offset in vidmem*/
	int channel;/* which channel this crtc stands for*/
	/* this view's video memory max size */
	resource_size_t vidmem_size;

	/* below attributes belong to info->fix,
	 * their value depends on specific adaptor
	 */
	u16 line_pad;/* padding information:0,1,2,4,8,16,... */
	u16 xpanstep;
	u16 ypanstep;
	u16 ywrapstep;

	void *priv;

	int (*proc_set_mode)(struct hisifb_crtc *,
						struct fb_var_screeninfo *,
						struct fb_fix_screeninfo *);
	int (*proc_check_mode)(struct hisifb_crtc *,
		struct fb_var_screeninfo*);
	int (*proc_set_col_reg)(struct hisifb_crtc *,
		ushort, ushort, ushort, ushort);
	void (*clear)(struct hisifb_crtc *);
	/* cursor information */
	struct hisi_cursor cursor;
};

struct hisifb_output {
	int dpms;
	int paths;
	/*which paths(s) this output stands for,hisi vga may not metioned:
	* paths=1:means output for panel paths
	* paths=2:means output for crt paths
	* paths=3:means output for both panel and crt paths
	*/

	int *channel;
	/*which channel these outputs linked with, hisi vga may
	* not metioned:channel=0 means primary channel
	* channel=1 means secondary channel
	* output->channel ==> &crtc->channel
	*/
	void *priv;
	int (*proc_set_mode)(struct hisifb_output *,
						struct fb_var_screeninfo *,
						struct fb_fix_screeninfo *);
	int (*proc_check_mode)(struct hisifb_output *,
		struct fb_var_screeninfo *);
	int (*proc_set_blank)(struct hisifb_output *, int);
	void (*clear)(struct hisifb_output *);
};

struct hisifb_par {
	/* either 0 or 1 for dual head adaptor,0 is the older one registered */
	int index;
	unsigned int pseudo_palette[256];
	struct hisifb_crtc crtc;
	struct hisifb_output output;
	struct fb_info *info;
	struct hisi_share *share;
};

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif


static inline unsigned long ps_to_hz(unsigned int psvalue)
{
	unsigned long long numerator = 1000 * 1000 * 1000 * 1000ULL;
	/* 10^12 / picosecond period gives frequency in Hz */
	do_div(numerator, psvalue);
	return (unsigned long)numerator;
}

#endif
