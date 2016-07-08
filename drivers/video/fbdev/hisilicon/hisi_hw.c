#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include "hisi_drv.h"
#include "hisi_accel.h"
#include "hisi_help.h"
#include "hisi_hw.h"
#include "hisi_mode.h"
#include "hisi_power.h"
#include "hisi_reg.h"

unsigned char __iomem *mmio_hisi_vga;

int hw_hisi_map(struct hisi_share *share, struct pci_dev *pdev)
{
	int ret;
	struct hisi_a_share *spec_share;

	spec_share = container_of(share, struct hisi_a_share, share);
	ret = 0;

	share->vidreg_start  = pci_resource_start(pdev, 1);
	share->vidreg_size = MB(2);

	/* now map mmio and vidmem*/
	share->pvreg = ioremap_nocache(share->vidreg_start, share->vidreg_size);
	if (!share->pvreg) {
		err_msg("mmio failed\n");
		ret = -EFAULT;
		goto exit;
	}

	share->accel.dpr_base = share->pvreg + DE_BASE_ADDR_TYPE1;
	share->accel.dp_port_base = share->pvreg + DE_PORT_ADDR_TYPE1;

	mmio_hisi_vga = share->pvreg;

	share->vidmem_start = pci_resource_start(pdev, 0);
	/* don't use pdev_resource[x].end - resource[x].start to
	 * calculate the resource size,its only the maximum available
	 * size but not the actual size,hisilicon provide 16MB buffer.
	 */
	share->vidmem_size = MB(16);
	inf_msg("video memory size = %llu mb\n", share->vidmem_size >> 20);

	share->pvmem = ioremap(share->vidmem_start, share->vidmem_size);

	if (!share->pvmem) {
		err_msg("Map video memory failed\n");
		ret = -EFAULT;
		goto exit;
	}

	inf_msg("video memory vaddr = %p\n", share->pvmem);

exit:
	return ret;
}

int hw_hisi_inithw(struct hisi_share *share, struct pci_dev *pdev)
{
	struct hisi_a_share *spec_share;
	struct init_status *parm;


	spec_share = container_of(share, struct hisi_a_share, share);
	parm = &spec_share->state.initParm;

	if (parm->chip_clk == 0)
		parm->chip_clk = DEFAULT_HISILE_CHIP_CLOCK;

	if (parm->mem_clk == 0)
		parm->mem_clk = parm->chip_clk;

	if (parm->master_clk == 0)
		parm->master_clk = parm->chip_clk / 3;

	hisi_init_hw((struct _initchip_param_t *)&spec_share->state.initParm);

	/* init 2d engine */
	if (!share->accel_off)
		hw_hisi_init_accel(share);

	return 0;
}

int hw_hisi_output_setmode(struct hisifb_output *output,
	struct fb_var_screeninfo *var,
	struct fb_fix_screeninfo *fix)
{
	u32 reg;

	/* just open DISPLAY_CONTROL_HISILE register bit 3:0*/
	reg = PEEK32(DISPLAY_CONTROL_HISILE);
	reg |= 0xf;
	POKE32(DISPLAY_CONTROL_HISILE, reg);

	inf_msg("set output mode done\n");

	return 0;
}

int hw_hisi_crtc_checkmode(struct hisifb_crtc *crtc,
	struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 8:
	case 16:
	case 32:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
* set the controller's mode for @crtc
* charged with @var and @fix parameters
*/
int hw_hisi_crtc_setmode(struct hisifb_crtc *crtc,
	struct fb_var_screeninfo *var,
	struct fb_fix_screeninfo *fix)
{
	int ret, fmt;
	u32 reg;
	struct mode_para modparm;
	enum clock_type clock;
	struct hisi_share *share;
	struct hisifb_par *par;

	ret = 0;
	par = container_of(crtc, struct hisifb_par, crtc);
	share = par->share;

	if (!share->accel_off) {
		/* set 2d engine pixel format according to mode bpp */
		switch (var->bits_per_pixel) {
		case 8:
			fmt = 0;
			break;
		case 16:
			fmt = 1;
			break;
		case 32:
		default:
			fmt = 2;
			break;
		}
		hw_set_2dformat(&share->accel, fmt);
	}

	/* set timing */
	modparm.pixel_clock = ps_to_hz(var->pixclock);
	modparm.vsync_polarity =
		(var->sync & FB_SYNC_HOR_HIGH_ACT) ? POS : NEG;
	modparm.hsync_polarity =
		(var->sync & FB_SYNC_VERT_HIGH_ACT) ? POS : NEG;
	modparm.clock_phase_polarity =
		(var->sync & FB_SYNC_COMP_HIGH_ACT) ? POS : NEG;
	modparm.h_display_end = var->xres;
	modparm.h_sync_width = var->hsync_len;
	modparm.h_sync_start = var->xres + var->right_margin;
	modparm.horizontal_total = var->xres +
		var->left_margin + var->right_margin + var->hsync_len;
	modparm.v_display_end = var->yres;
	modparm.v_sync_height = var->vsync_len;
	modparm.v_sync_start = var->yres + var->lower_margin;
	modparm.vertical_total = var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len;

	/* choose pll */
	clock = SECONDARY_PLL;

	dbg_msg("Request pixel clock = %lu\n", modparm.pixel_clock);
	ret = hisi_set_mode_timing(&modparm, clock);
	if (ret) {
		err_msg("Set mode timing failed\n");
		goto exit;
	}

	/* not implemented now */
	POKE32(CRT_FB_ADDRESS, crtc->oscreen);
	reg = var->xres * (var->bits_per_pixel >> 3);
	/* crtc->channel is not equal to
	 * par->index on numeric,be aware of that
	 */
	reg = PADDING(crtc->line_pad, reg);

	POKE32(CRT_FB_WIDTH,
		FIELD_VALUE(0, CRT_FB_WIDTH, WIDTH, reg) |
		FIELD_VALUE(0, CRT_FB_WIDTH, OFFSET, fix->line_length));

	/* SET PIXEL FORMAT */
	reg = PEEK32(CRT_DISPLAY_CTRL);
	reg = FIELD_VALUE(reg, CRT_DISPLAY_CTRL,
			   FORMAT, var->bits_per_pixel >> 4);
	POKE32(CRT_DISPLAY_CTRL, reg);

exit:
	return ret;
}

int hw_hisi_set_col_reg(struct hisifb_crtc *crtc,
	ushort index, ushort red,
	ushort green, ushort blue)
{
	static unsigned int add[] = {PANEL_PALETTE_RAM, CRT_PALETTE_RAM};


	POKE32(add[crtc->channel] + index * 4,
		(red << 16) | (green << 8) | blue);

	return 0;
}

int hw_hisile_set_blank(struct hisifb_output *output, int blank)
{
	int dpms, crtdb;


	switch (blank) {
	case FB_BLANK_UNBLANK:
		dpms = CRT_DISPLAY_CTRL_DPMS_0;
		crtdb = CRT_DISPLAY_CTRL_BLANK_OFF;
		break;
	case FB_BLANK_NORMAL:
		dpms = CRT_DISPLAY_CTRL_DPMS_0;
		crtdb = CRT_DISPLAY_CTRL_BLANK_ON;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		dpms = CRT_DISPLAY_CTRL_DPMS_2;
		crtdb = CRT_DISPLAY_CTRL_BLANK_ON;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		dpms = CRT_DISPLAY_CTRL_DPMS_1;
		crtdb = CRT_DISPLAY_CTRL_BLANK_ON;
		break;
	case FB_BLANK_POWERDOWN:
		dpms = CRT_DISPLAY_CTRL_DPMS_3;
		crtdb = CRT_DISPLAY_CTRL_BLANK_ON;
		break;
	default:
		return -EINVAL;
	}

	if (output->paths & hisi_crt) {
		POKE32(CRT_DISPLAY_CTRL,
			FIELD_VALUE(PEEK32(CRT_DISPLAY_CTRL),
			CRT_DISPLAY_CTRL, DPMS, dpms));
		POKE32(CRT_DISPLAY_CTRL,
			FIELD_VALUE(PEEK32(CRT_DISPLAY_CTRL),
			CRT_DISPLAY_CTRL, BLANK, crtdb));
	}

	return 0;
}

void hw_hisi_init_accel(struct hisi_share *share)
{
	u32 reg;


	enable_2d_engine(1);

	reg = PEEK32(DE_STATE1);
	reg = FIELD_SET(reg, DE_STATE1, DE_ABORT, ON);
	POKE32(DE_STATE1, reg);

	reg = PEEK32(DE_STATE1);
	reg = FIELD_SET(reg, DE_STATE1, DE_ABORT, OFF);
	POKE32(DE_STATE1, reg);

	/* call 2d init */
	share->accel.de_init(&share->accel);
}

int hw_hisile_dewait(void)
{
	int i = 0x10000000;

	while (i--) {
		unsigned int dwval = PEEK32(DE_STATE2);

		if ((FIELD_GET(dwval, DE_STATE2, DE_STATUS)
			== DE_STATE2_DE_STATUS_IDLE) &&
		    (FIELD_GET(dwval, DE_STATE2, DE_FIFO)
			== DE_STATE2_DE_FIFO_EMPTY) &&
		    (FIELD_GET(dwval, DE_STATE2, DE_MEM_FIFO)
			== DE_STATE2_DE_MEM_FIFO_EMPTY)) {
			return 0;
		}
	}

	/* timeout error */
	return -1;
}
