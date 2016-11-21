#ifndef HISI_HW_H__
#define HISI_HW_H__


#define DEFAULT_HISILE_CHIP_CLOCK	333

/* notes: below address are the offset value from de_base_address (0x100000)*/

/* de_base is at mmreg_1mb*/
#define DE_BASE_ADDR_TYPE1 0x100000

/* type1 data port address is at mmreg_0x110000*/
#define DE_PORT_ADDR_TYPE1 0x110000



enum hisilicon_pnltype {

	TYPE_24TFT = 0,/* 24bit tft */

	TYPE_DUAL_TFT = 2,/* dual 18 bit tft */

	TYPE_DOUBLE_TFT = 1,/* 36 bit double pixel tft */
};

/* vga channel is not concerned  */
enum hisilicon_dataflow {
	hisi_simul_pri,/* primary => all head */
	hisi_simul_sec,/* secondary => all head */
	hisi_dual_normal,/* primary => panel head and secondary => crt */
	hisi_dual_swap,/* primary => crt head and secondary => panel */
};


enum hisi_channel {
	hisi_primary = 0,
	/* enum value equal to the register filed data */
	hisi_secondary = 1,
};

enum hisi_display_path {
	hisi_panel = 1,
	hisi_crt = 2,
	hisi_pnc = 3,/* panel and crt */
};

struct init_status {
	ushort power_mode;
	/* below three clocks are in unit of MHZ*/
	ushort chip_clk;
	ushort mem_clk;
	ushort master_clk;
	ushort all_eng_off;
	ushort reset_memory;
};

struct hisi_state {
	struct init_status initParm;
	enum hisilicon_pnltype pnltype;
	enum hisilicon_dataflow dataflow;
	int nocrt;
	int xlcd;
	int ylcd;
};

/* hisi_a_share stands for a presentation of two frame buffer
 * that use one hisi adaptor
*/

struct hisi_a_share {
	/* Put hisi_share struct to the first place of hisi_a_share */
	struct hisi_share share;
	struct hisi_state state;
	int hwcursor;
	/*0: no hardware cursor
	* 1: primary crtc hw cursor enabled,
	* 2: secondary crtc hw cursor enabled
	* 3: both ctrc hw cursor enabled
	*/
};

int hw_hisi_map(struct hisi_share *share, struct pci_dev *pdev);
int hw_hisi_inithw(struct hisi_share*, struct pci_dev *);
void hw_hisi_init_accel(struct hisi_share *);
int hw_hisile_dewait(void);

int hw_hisi_output_setmode(struct hisifb_output*, struct fb_var_screeninfo*,
			struct fb_fix_screeninfo*);
int hw_hisi_crtc_checkmode(struct hisifb_crtc*, struct fb_var_screeninfo*);
int hw_hisi_crtc_setmode(struct hisifb_crtc*, struct fb_var_screeninfo*,
			struct fb_fix_screeninfo*);
int hw_hisi_set_col_reg(struct hisifb_crtc*, ushort, ushort, ushort, ushort);
int hw_hisile_set_blank(struct hisifb_output*, int);

#endif
