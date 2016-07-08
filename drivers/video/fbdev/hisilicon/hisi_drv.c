#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/kernel.h>
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
#include "hisi_cursor.h"
#include "hisi_hw.h"
#include "hisi_accel.h"


/* chip specific setup routine */
static void hisi_fb_setup(struct hisi_share *share, char *src);
static int hisifb_pci_probe(struct pci_dev *, const struct pci_device_id *);
static void hisifb_pci_remove(struct pci_dev *);

#ifdef CONFIG_PM
static int hisifb_suspend(struct pci_dev *, pm_message_t);
static int hisifb_resume(struct pci_dev *);
#endif

static int hisifb_set_fbinfo(struct fb_info *, int);
static int hisifb_ops_check_var(struct fb_var_screeninfo *, struct fb_info *);
static int hisifb_ops_set_par(struct fb_info *);
static int hisifb_ops_setcolreg(unsigned, unsigned, unsigned,
	unsigned, unsigned, struct fb_info *);
static int hisifb_ops_blank(int, struct fb_info *);
static int hisifb_ops_cursor(struct fb_info *, struct fb_cursor *);

typedef void (*PROC_SPEC_SETUP)(struct hisi_share *, char *);
typedef int (*PROC_SPEC_MAP)(struct hisi_share *, struct pci_dev *);
typedef int (*PROC_SPEC_INITHW)(struct hisi_share *, struct pci_dev *);

/* common var for all device */
static int g_hwcursor = 1;
static int g_noaccel = 1;
#ifdef CONFIG_MTRR
static int g_nomtrr;
#endif
static const char *g_fbmode[2];

/*modify for hisilicon default mode*/
static const char *g_def_fbmode = "800x600-16@60";
static char *g_settings;
static int g_dualview;

#ifdef MODULE
static char *g_option;
#endif

static const struct fb_videomode hisi_vedio_mode_ext[] = {
	/*024x600-60 VESA [1.71:1]*/
	{NULL,  60, 1024, 600, 20423, 144,  40, 18, 1, 104, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/* 024x600-70 VESA */
	{NULL,  70, 1024, 600, 17211, 152,  48, 21, 1, 104, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*  024x600-75 VESA */
	{NULL,  75, 1024, 600, 15822, 160,  56, 23, 1, 104, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*  024x600-85 VESA */
	{NULL,  85, 1024, 600, 13730, 168,  56, 26, 1, 112, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*20x480	*/
	{NULL, 60,  720,  480,  37427, 88, 16, 13, 1,   72,  3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*280x720		[1.78:1]	*/
	{NULL, 60,  1280,  720,  13426, 162, 86, 22, 1,  136, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/* 1280x768@60 */
	{NULL, 60, 1280, 768, 12579, 192, 64, 20, 3, 128, 7,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	{NULL, 60, 1360, 768, 11804, 208, 64, 23, 1, 144, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_VMODE_NONINTERLACED},

	/*360 x 768	[1.77083:1]	*/
	{NULL,  60, 1360, 768, 11804, 208,  64, 23, 1, 144, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*368 x 768      [1.78:1]	*/
	{NULL, 60,  1368,  768,  11647, 216, 72, 23, 1,  144, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/* 440 x 900		[16:10]	*/
	{NULL, 60, 1440, 900, 9392, 232, 80, 28, 1, 152, 3,
	FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*440x960		[15:10]	*/
	{NULL, 60, 1440, 960, 8733, 240, 88, 30, 1, 152, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},

	/*920x1080	[16:9]	*/
	{NULL, 60, 1920, 1080, 6734, 148, 88, 41, 1, 44, 3,
	FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED},
};

static struct pci_device_id hisi_pci_table[] = {
	{PCI_VENDOR_ID_HIS, PCI_DEVID_HS_VGA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

static struct pci_driver hisifb_driver = {
	.name = _moduleName_,
	.id_table = hisi_pci_table,
	.probe = hisifb_pci_probe,
	.remove = hisifb_pci_remove,
#ifdef CONFIG_PM
	.suspend = hisifb_suspend,
	.resume = hisifb_resume,
#endif
};

static int hisifb_ops_cursor(struct fb_info *info, struct fb_cursor *fbcursor)
{
	struct hisifb_par *par;
	struct hisifb_crtc *crtc;
	struct hisi_cursor *cursor;

	par = info->par;
	crtc = &par->crtc;
	cursor = &crtc->cursor;

	if (fbcursor->image.width > cursor->maxW ||
		fbcursor->image.height > cursor->maxH ||
		 fbcursor->image.depth > 1){
		return -ENXIO;
	}

	cursor->disable(cursor);
	if (fbcursor->set & FB_CUR_SETSIZE)
		cursor->set_size(cursor, fbcursor->image.width,
				 fbcursor->image.height);


	if (fbcursor->set & FB_CUR_SETPOS)
		cursor->set_pos(cursor, fbcursor->image.dx - info->var.xoffset,
				fbcursor->image.dy - info->var.yoffset);

	if (fbcursor->set & FB_CUR_SETCMAP) {
		/* get the 16bit color of kernel means */
		u16 fg, bg;

		fg =
		((info->cmap.red[fbcursor->image.fg_color] & 0xf800)) |
		((info->cmap.green[fbcursor->image.fg_color] & 0xfc00) >> 5)|
		((info->cmap.blue[fbcursor->image.fg_color] & 0xf800) >> 11);

		bg =
		((info->cmap.red[fbcursor->image.bg_color] & 0xf800))|
		((info->cmap.green[fbcursor->image.bg_color] & 0xfc00) >> 5)|
		((info->cmap.blue[fbcursor->image.bg_color] & 0xf800) >> 11);

		cursor->set_color(cursor, fg, bg);
	}


	if (fbcursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		cursor->set_data(cursor,
				 fbcursor->rop,
				 fbcursor->image.data,
				 fbcursor->mask);
	}

	if (fbcursor->enable)
		cursor->enable(cursor);

	return 0;
}

static void hisifb_ops_fillrect(struct fb_info *info,
	const struct fb_fillrect *region)
{
	struct hisifb_par *par;
	struct hisi_share *share;
	unsigned int base, pitch, Bpp, rop;
	u32 color;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	par = info->par;
	share = par->share;

	/*
	 * each time 2d function begin to work,below three variable always need
	 * be set, seems we can put them together in some place
	 */
	base = par->crtc.oscreen;
	pitch = info->fix.line_length;
	Bpp = info->var.bits_per_pixel >> 3;

	color = (Bpp == 1) ? region->color :
		((u32 *)info->pseudo_palette)[region->color];
	rop = (region->rop != ROP_COPY) ?
		HW_ROP2_XOR : HW_ROP2_COPY;

	share->accel.de_fillrect(&share->accel,
				 base, pitch, Bpp, region->dx,
				 region->dy, region->width,
				 region->height, color, rop);
}

static void hisifb_ops_copyarea(struct fb_info *info,
	const struct fb_copyarea *region)
{
	struct hisifb_par *par;
	struct hisi_share *share;
	unsigned int base, pitch, Bpp;

	par = info->par;
	share = par->share;

	/*
	 * each time 2d function begin to work,below three variable always need
	 * be set, seems we can put them together in some place
	 */
	base = par->crtc.oscreen;
	pitch = info->fix.line_length;
	Bpp = info->var.bits_per_pixel >> 3;

	share->accel.de_copyarea(&share->accel, base, pitch,
				 region->sx, region->sy, base,
				 pitch, Bpp, region->dx, region->dy,
				 region->width, region->height,
				 HW_ROP2_COPY);
}

static void hisifb_ops_imageblit(struct fb_info *info,
	const struct fb_image *image)
{
	unsigned int base, pitch, Bpp;
	unsigned int fgcol, bgcol;
	struct hisifb_par *par;
	struct hisi_share *share;

	par = info->par;
	share = par->share;
	/*
	 * each time 2d function begin to work,below three variable always need
	 * be set, seems we can put them together in some place
	 */
	base = par->crtc.oscreen;
	pitch = info->fix.line_length;
	Bpp = info->var.bits_per_pixel >> 3;

	if (image->depth == 1) {
		if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
			info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
			fgcol = ((u32 *)info->pseudo_palette)[image->fg_color];
			bgcol = ((u32 *)info->pseudo_palette)[image->bg_color];
		} else {
			fgcol = image->fg_color;
			bgcol = image->bg_color;
		}
		goto _do_work;
	}
	return;
_do_work:
	share->accel.de_imageblit(&share->accel,
				  image->data, image->width >> 3, 0,
				  base, pitch, Bpp,
				  image->dx, image->dy,
				  image->width, image->height,
				  fgcol, bgcol, HW_ROP2_COPY);
}

static struct fb_ops hisifb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var =  hisifb_ops_check_var,
	.fb_set_par = hisifb_ops_set_par,
	.fb_setcolreg = hisifb_ops_setcolreg,
	.fb_blank = hisifb_ops_blank,
	/* will be hooked by hardware */
	.fb_fillrect = cfb_fillrect,
	.fb_imageblit = cfb_imageblit,
	.fb_copyarea = cfb_copyarea,
	/* cursor */
	.fb_cursor = hisifb_ops_cursor,
};

#ifdef CONFIG_PM
static int hisifb_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct fb_info *info;
	struct hisi_share *share;
	int ret;

	if (mesg.event == pdev->dev.power.power_state.event)
		return 0;

	ret = 0;
	share = pci_get_drvdata(pdev);
	switch (mesg.event) {
	case PM_EVENT_FREEZE:
	case PM_EVENT_PRETHAW:
		pdev->dev.power.power_state = mesg;
		return 0;
	}

	console_lock();
	if (mesg.event & PM_EVENT_SLEEP) {
		info = share->fbinfo[0];
		if (info)
			fb_set_suspend(info, 1);/* 1 means do suspend*/

		info = share->fbinfo[1];
		if (info)
			fb_set_suspend(info, 1);/* 1 means do suspend*/

		ret = pci_save_state(pdev);
		if (ret) {
			err_msg("error:%d occurred in pci_save_state\n", ret);
			console_unlock();
			return ret;
		}

		pci_disable_device(pdev);
		ret = pci_set_power_state(pdev, pci_choose_state(pdev, mesg));
		if (ret) {
			err_msg("error:%d occurred in pci_set_power_state\n",
				ret);
			console_unlock();
			return ret;
		}
		/* set chip to sleep mode	*/
		if (share->suspend)
			(*share->suspend)();
	}

	pdev->dev.power.power_state = mesg;
	console_unlock();
	return ret;
}

static int hisifb_resume(struct pci_dev *pdev)
{
	struct fb_info *info;
	struct hisi_share *share;

	struct hisifb_par *par;
	struct hisifb_crtc *crtc;
	struct hisi_cursor *cursor;

	int ret;

	ret = 0;
	share = pci_get_drvdata(pdev);

	console_lock();
	ret = pci_set_power_state(pdev, PCI_D0);
	if (ret != 0) {
		err_msg("error:%d occurred in pci_set_power_state\n", ret);
		console_lock();
		return ret;
	}

	if (pdev->dev.power.power_state.event != PM_EVENT_FREEZE) {
		pci_restore_state(pdev);
		ret = pci_enable_device(pdev);
		if (ret != 0) {
			err_msg("error:%d occurred in pci_enable_device\n",
				ret);
			console_lock();
			return ret;
		}
		pci_set_master(pdev);
	}

	hw_hisi_inithw(share, pdev);

	info = share->fbinfo[0];
	par = info->par;
	crtc = &par->crtc;
	cursor = &crtc->cursor;

	if (info) {
		hisifb_ops_set_par(info);
		fb_set_suspend(info, 0);
	}

	info = share->fbinfo[1];

	if (info) {
		hisifb_ops_set_par(info);
		fb_set_suspend(info, 0);
	}

	if (share->resume)
		(*share->resume)();

	console_unlock();
	return ret;
}
#endif

static int hisifb_ops_check_var(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	struct hisifb_par *par;
	struct hisifb_crtc *crtc;
	struct hisifb_output *output;
	struct hisi_share *share;
	int ret;
	resource_size_t request;

	par = info->par;
	crtc = &par->crtc;
	output = &par->output;
	share = par->share;
	ret = 0;

	dbg_msg("check var:%dx%d-%d\n",
		 var->xres,
		 var->yres,
		 var->bits_per_pixel);

	switch (var->bits_per_pixel) {
	case 8:
	case 16:
	case 24: /* support 24 bpp for only lynx712/722/720 */
	case 32:
		break;
	default:
		err_msg("bpp %d not supported\n", var->bits_per_pixel);
		ret = -EINVAL;
		goto exit;
	}

	switch (var->bits_per_pixel) {
	case 8:
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.length = 0;
		var->transp.offset = 0;
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 24:
	case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	var->height = var->width = -1;
	var->accel_flags = FB_ACCELF_TEXT;

	/* check if current fb's video memory
	 * big enought to hold the onscreen
	 */
	request = var->xres_virtual * (var->bits_per_pixel >> 3);
	/* defaulty crtc->channel go with par->index */

	request = PADDING(crtc->line_pad, request);
	request = request * var->yres_virtual;
	if (crtc->vidmem_size < request) {
		err_msg("not enough video memory for mode\n");
		return -ENOMEM;
	}

	ret = crtc->proc_check_mode(crtc, var);

exit:
	return ret;
}

static int hisifb_ops_set_par(struct fb_info *info)
{
	struct hisifb_par *par;
	struct hisi_share *share;
	struct hisifb_crtc *crtc;
	struct hisifb_output *output;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;
	int ret;
	unsigned int line_length;


	if (!info)
		return -EINVAL;

	ret = 0;
	par = info->par;
	share = par->share;
	crtc = &par->crtc;
	output = &par->output;
	var = &info->var;
	fix = &info->fix;

	/* fix structur is not so FIX ... */
	line_length = var->xres_virtual * var->bits_per_pixel / 8;
	line_length = PADDING(crtc->line_pad, line_length);
	fix->line_length = line_length;
	dbg_msg("fix->line_length = %d\n", fix->line_length);

	/* var->red,green,blue,transp are need to be set by driver
	 * and these data should be set before setcolreg routine
	 */

	switch (var->bits_per_pixel) {
	case 8:
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.length = 0;
		var->transp.offset = 0;
		fix->visual = FB_VISUAL_TRUECOLOR;
		break;
	case 24:
	case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		fix->visual = FB_VISUAL_TRUECOLOR;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	var->height = var->width = -1;
	var->accel_flags = FB_ACCELF_TEXT;

	if (ret) {
		err_msg("pixel bpp format not satisfied\n.");
		return ret;
	}
	ret = crtc->proc_set_mode(crtc, var, fix);
	if (!ret)
		ret = output->proc_set_mode(output, var, fix);

	return ret;
}
static inline unsigned int chan_to_field(unsigned int chan,
	struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int hisifb_ops_setcolreg(unsigned regno, unsigned red,
	unsigned green, unsigned blue, unsigned transp, struct fb_info *info)
{
	struct hisifb_par *par;
	struct hisifb_crtc *crtc;
	struct fb_var_screeninfo *var;
	int ret;

	par = info->par;
	crtc = &par->crtc;
	var = &info->var;
	ret = 0;


	if (regno > 256) {
		err_msg("regno = %d\n", regno);
		return -EINVAL;
	}

	if (info->var.grayscale)
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;

	if (var->bits_per_pixel == 8 &&
		info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		ret = crtc->proc_set_col_reg(crtc, regno, red, green, blue);
		goto exit;
	}


	if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 256) {
		u32 val;

		if (var->bits_per_pixel == 16 ||
			var->bits_per_pixel == 32 ||
			var->bits_per_pixel == 24) {
			val = chan_to_field(red, &var->red);
			val |= chan_to_field(green, &var->green);
			val |= chan_to_field(blue, &var->blue);
			par->pseudo_palette[regno] = val;
			goto exit;
		}
	}

	ret = -EINVAL;

exit:
	return ret;
}

static int hisifb_ops_blank(int blank, struct fb_info *info)
{
	return 0;
}

/*
 * HS_F,HS_A all go through this routine
 */
static int hisifb_set_drv(struct hisifb_par *par)
{
	int ret = 0;
	struct hisi_share *share;
	struct hisi_a_share *spec_share;
	struct hisifb_output *output;
	struct hisifb_crtc *crtc;


	share = par->share;
	spec_share = container_of(share, struct hisi_a_share, share);
	output = &par->output;
	crtc = &par->crtc;

	crtc->vidmem_size = (share->dual) ?
		share->vidmem_size >> 1 : share->vidmem_size;

	/* setup crtc and output member */
	spec_share->hwcursor = g_hwcursor;

	crtc->proc_set_mode = hw_hisi_crtc_setmode;
	crtc->proc_check_mode = hw_hisi_crtc_checkmode;
	crtc->proc_set_col_reg = hw_hisi_set_col_reg;
	crtc->clear = NULL;
	crtc->line_pad = 16;
	crtc->xpanstep = crtc->ypanstep = crtc->ywrapstep = 0;

	output->proc_set_mode = hw_hisi_output_setmode;
	output->proc_check_mode = NULL;

	output->proc_set_blank = hw_hisile_set_blank;
	share->accel.de_wait = hw_hisile_dewait;
	output->clear = NULL;

	switch (spec_share->state.dataflow) {
	case hisi_simul_pri:
		output->paths = hisi_pnc;
		crtc->channel = hisi_primary;
		crtc->oscreen = 0;
		crtc->vscreen = share->pvmem;
		inf_msg("use simul primary mode\n");
		break;
	case hisi_simul_sec:
		output->paths = hisi_pnc;
		crtc->channel = hisi_secondary;
		crtc->oscreen = 0;
		crtc->vscreen = share->pvmem;
		break;
	case hisi_dual_normal:
		if (par->index == 0) {
			output->paths = hisi_panel;
			crtc->channel = hisi_primary;
			crtc->oscreen = 0;
			crtc->vscreen = share->pvmem;
		} else {
			output->paths = hisi_crt;
			crtc->channel = hisi_secondary;
		/* not consider of padding stuffs for oscreen,need fix*/
			crtc->oscreen = (share->vidmem_size >> 1);
			crtc->vscreen = share->pvmem + crtc->oscreen;
		}
		break;
	case hisi_dual_swap:
		if (par->index == 0) {
			output->paths = hisi_panel;
			crtc->channel = hisi_secondary;
			crtc->oscreen = 0;
			crtc->vscreen = share->pvmem;
		} else {
			output->paths = hisi_crt;
			crtc->channel = hisi_primary;
		/* not consider of padding stuffs for oscreen,need fix*/
			crtc->oscreen = (share->vidmem_size >> 1);
			crtc->vscreen = share->pvmem + crtc->oscreen;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int hisifb_set_fbinfo(struct fb_info *info, int index)
{
	int i;
	struct hisifb_par *par;
	struct hisi_share *share;
	struct hisifb_crtc *crtc;
	struct hisifb_output *output;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;

	const struct fb_videomode *pdb[] = {
		hisi_vedio_mode_ext, NULL, vesa_modes,
	};
	int cdb[] = {ARRAY_SIZE(hisi_vedio_mode_ext), 0, VESA_MODEDB_SIZE};
	static const char *const mdb_desc[] = {
		"driver prepared modes",
		"kernel prepared default modedb",
		"kernel HELPERS prepared vesa_modes",
	};

	static const char *fixId[2] = {
		"hisi_fb1", "hisi_fb2",
	};

	int ret, line_length;


	ret = 0;
	par = (struct hisifb_par *)info->par;
	share = par->share;
	crtc = &par->crtc;
	output = &par->output;
	var = &info->var;
	fix = &info->fix;

	/* set index */
	par->index = index;
	output->channel = &crtc->channel;
	hisifb_set_drv(par);

	/*
	 * Set current cursor variable and proc pointer,
	 * must be set after crtc member initialized.
	 */
	crtc->cursor.offset = crtc->oscreen +
		crtc->vidmem_size - 1024;
	crtc->cursor.mmio = share->pvreg + 0x800f0 +
		(int)crtc->channel * 0x140;

	inf_msg("crtc->cursor.mmio = %p\n", crtc->cursor.mmio);
	crtc->cursor.maxH = crtc->cursor.maxW = 64;
	crtc->cursor.size = crtc->cursor.maxH *
		crtc->cursor.maxW * 2 / 8;
	crtc->cursor.disable = hw_cursor_disable;
	crtc->cursor.enable = hw_cursor_enable;
	crtc->cursor.set_color = hw_cursor_set_color;
	crtc->cursor.set_pos = hw_cursor_set_pos;
	crtc->cursor.set_size = hw_cursor_set_size;
	crtc->cursor.set_data = hw_cursor_set_data;
	crtc->cursor.vstart = crtc->vscreen + crtc->cursor.offset;

	crtc->cursor.share = share;
	if (!g_hwcursor) {
		hisifb_ops.fb_cursor = NULL;
		crtc->cursor.disable(&crtc->cursor);
	}

	/* set info->fbops, must be set before fb_find_mode */
	if (!share->accel_off) {
		/* use 2d acceleration */
		hisifb_ops.fb_fillrect = hisifb_ops_fillrect;
		hisifb_ops.fb_copyarea = hisifb_ops_copyarea;
		hisifb_ops.fb_imageblit = hisifb_ops_imageblit;
	}
	info->fbops = &hisifb_ops;

	if (!g_fbmode[index]) {
		g_fbmode[index] = g_def_fbmode;
		if (index)
			g_fbmode[index] = g_fbmode[0];
	}

	for (i = 0; i < 3; i++) {
		ret = fb_find_mode(var, info, g_fbmode[index],
				   pdb[i], cdb[i], NULL, 8);

		if (ret == 1) {
			inf_msg("success! use specified mode:%s in %s\n",
				 g_fbmode[index],
				 mdb_desc[i]);
			break;
		} else if (ret == 2) {
			war_msg("use specified mode:%s in %s,\n"
				 "with an ignored refresh rate\n",
				 g_fbmode[index],
				 mdb_desc[i]);
			break;
		} else if (ret == 3) {
			war_msg("wanna use default mode\n");
		} else if (ret == 4) {
			war_msg("fall back to any valid mode\n");
		} else {
			err_msg("ret = %d,fb_find_mode failed,with %s\n",
				 ret, mdb_desc[i]);
		}
	}

	/* some member of info->var had been set by fb_find_mode */

	inf_msg("Member of info->var is :\n"
		 "xres=%d\n"
		 "yres=%d\n"
		 "xres_virtual=%d\n"
		 "yres_virtual=%d\n"
		 "xoffset=%d\n"
		 "yoffset=%d\n"
		 "bits_per_pixel=%d\n"
		 "...\n", var->xres, var->yres, var->xres_virtual,
		 var->yres_virtual, var->xoffset, var->yoffset,
		 var->bits_per_pixel);

	/* set par */
	par->info = info;

	/* set info */
	line_length = PADDING(crtc->line_pad,
	(var->xres_virtual * var->bits_per_pixel / 8));

	info->pseudo_palette = &par->pseudo_palette[0];
	info->screen_base = crtc->vscreen;
	dbg_msg("screen_base vaddr = %p\n", info->screen_base);
	info->screen_size = line_length * var->yres_virtual;
	info->flags = FBINFO_FLAG_DEFAULT | 0;

	/* set info->fix */
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	fix->xpanstep = crtc->xpanstep;
	fix->ypanstep = crtc->ypanstep;
	fix->ywrapstep = crtc->ywrapstep;
	fix->accel = FB_ACCEL_NONE;

	strlcpy(fix->id, fixId[index], sizeof(fix->id));


	fix->smem_start = crtc->oscreen + share->vidmem_start;
	inf_msg("fix->smem_start = %lx\n", fix->smem_start);

	/*
	 * according to mmap experiment from user space application,
	 * fix->mmio_len should not larger than virtual size
	 * (xres_virtual x yres_virtual x ByPP)
	 * Below line maybe buggy when user mmap fb dev node and write
	 * data into the bound over virtual size
	 */
	fix->smem_len = crtc->vidmem_size;
	inf_msg("fix->smem_len = %x\n", fix->smem_len);
	fix->line_length = line_length;
	fix->mmio_start = share->vidreg_start;
	inf_msg("fix->mmio_start = %lx\n", fix->mmio_start);
	fix->mmio_len = share->vidreg_size;
	inf_msg("fix->mmio_len = %x\n", fix->mmio_len);
	switch (var->bits_per_pixel) {
	case 8:
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 16:
	case 32:
		fix->visual = FB_VISUAL_TRUECOLOR;
		break;
	}

	/* set var */
	var->activate = FB_ACTIVATE_NOW;
	var->accel_flags = FB_ACCELF_TEXT;
	var->vmode = FB_VMODE_NONINTERLACED;

	dbg_msg("#1 show info->cmap :\n"
		 "start=%d,len=%d,red=%p,green=%p,blue=%p,transp=%p\n",
		 info->cmap.start, info->cmap.len,
		 info->cmap.red, info->cmap.green,
		 info->cmap.blue, info->cmap.transp);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret < 0) {
		err_msg("Could not allcate memory for cmap.\n");
		goto exit;
	}

	dbg_msg("#2 show info->cmap :\n"
		 "start=%d,len=%d,red=%p,green=%p,blue=%p,transp=%p\n",
		 info->cmap.start, info->cmap.len,
		 info->cmap.red, info->cmap.green, info->cmap.blue,
		 info->cmap.transp);
exit:
	return ret;
}

static int hisifb_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct fb_info *info[] = {NULL, NULL};
	struct hisi_share *share = NULL;

	void *spec_share = NULL;
	size_t spec_offset = 0;
	int fbidx;


	/* enable device */
	if (pci_enable_device(pdev)) {
		err_msg("can not enable device.\n");
		goto err_enable;
	}

	/* though offset of share in hisi_a_share is 0,
	 * we use this marcro as the same
	 */
	spec_offset = offsetof(struct hisi_a_share, share);


	dbg_msg("spec_offset = %ld\n", spec_offset);
	spec_share = kzalloc(sizeof(struct hisi_a_share), GFP_KERNEL);

	if (!spec_share) {
		err_msg("Could not allocate memory for share.\n");
		goto err_share;
	}

	/* setting share structure */
	share = (struct hisi_share *)(spec_share + spec_offset);
	share->fbinfo[0] = share->fbinfo[1] = NULL;

	inf_msg("share->revid = %02x\n", pdev->revision);
	share->pdev = pdev;
#ifdef CONFIG_MTRR
	share->mtrr_off = g_nomtrr;
	share->mtrr.vram = 0;
	share->mtrr.vram_added = 0;
#endif
	share->accel_off = g_noaccel;
	share->dual = g_dualview;


	if (!share->accel_off) {
		/*
		 * hook deInit and 2d routines, notes that below hw_xxx
		 * routine can work on most of hisi chips,if some chip need
		 *  specific function,please hook it in smXXX_set_drv
		 * routine
		 */
		share->accel.de_init = hw_de_init;
		share->accel.de_fillrect = hw_fillrect;
		share->accel.de_copyarea = hw_copyarea;
		share->accel.de_imageblit = hw_imageblit;
		inf_msg("enable 2d acceleration\n");
	} else {
		inf_msg("disable 2d acceleration\n");
	}

	/* call chip specific setup routine  */
	hisi_fb_setup(share, g_settings);

	/* call chip specific mmap routine */
	if (hw_hisi_map(share, pdev)) {
		err_msg("Memory map failed\n");
		goto err_map;
	}

#ifdef CONFIG_MTRR
	if (!share->mtrr_off) {
		inf_msg("enable mtrr\n");
		share->mtrr.vram = mtrr_add(share->vidmem_start,
					    share->vidmem_size,
					    MTRR_TYPE_WRCOMB, 1);
		if (share->mtrr.vram < 0) {
			/* don't block driver with the failure of MTRR */
			err_msg("Unable to setup MTRR.\n");
		} else {
			share->mtrr.vram_added = 1;
			inf_msg("MTRR added successfully\n");
		}
	}
#endif

	/*memset(share->pvmem,0,share->vidmem_size);*/

	inf_msg("sm%3x mmio address = %p\n", pdev->device, share->pvreg);

	pci_set_drvdata(pdev, share);

	/* call chipInit routine */
	hw_hisi_inithw(share, pdev);

	/* allocate frame buffer info structor according to g_dualview */
	fbidx = 0;
	info[fbidx] = framebuffer_alloc(sizeof(struct hisifb_par), &pdev->dev);
	if (!info[fbidx]) {
		err_msg("Could not allocate framebuffer #%d.\n", fbidx);
		if (fbidx == 0)
			goto err_info0_alloc;
		else
			goto err_info1_alloc;
	} else {
		struct hisifb_par *par;

		inf_msg("framebuffer #%d alloc okay\n", fbidx);
		share->fbinfo[fbidx] = info[fbidx];
		par = info[fbidx]->par;
		par->share = share;

		/* set fb_info structure */
		if (hisifb_set_fbinfo(info[fbidx], fbidx)) {
			err_msg("Failed to initial fb_info #%d.\n", fbidx);
			if (fbidx == 0)
				goto err_info0_set;
			else
				goto err_info1_set;
		}

		/* register frame buffer*/
		inf_msg("Ready to register framebuffer #%d.\n", fbidx);
		if (register_framebuffer(info[fbidx]) < 0) {
			err_msg("Failed to register fb_info #%d.\n", fbidx);
			if (fbidx == 0)
				goto err_register0;
			else
				goto err_register1;
		}
		inf_msg("Accomplished register framebuffer #%d.\n", fbidx);
	}

	return 0;

err_register1:
err_info1_set:
	framebuffer_release(info[1]);
err_info1_alloc:
	unregister_framebuffer(info[0]);
err_register0:
err_info0_set:
	framebuffer_release(info[0]);
err_info0_alloc:
err_map:
	kfree(spec_share);
err_share:
err_enable:
	return -ENODEV;
}

static void hisifb_pci_remove(struct pci_dev *pdev)
{
	struct fb_info *info;
	struct hisi_share *share;
	void *spec_share;
	struct hisifb_par *par;
	int cnt;


	cnt = 2;
	share = pci_get_drvdata(pdev);

	while (cnt-- > 0) {
		info = share->fbinfo[cnt];
		if (!info)
			continue;
		par = info->par;

		unregister_framebuffer(info);
		/* clean crtc & output allocations*/
		par->crtc.clear(&par->crtc);
		par->output.clear(&par->output);
		/* release frame buffer*/
		framebuffer_release(info);
	}
#ifdef CONFIG_MTRR
	if (share->mtrr.vram_added)
		mtrr_del(share->mtrr.vram, share->vidmem_start,
		share->vidmem_size);
#endif

	iounmap(share->pvreg);
	iounmap(share->pvmem);

	spec_share = share;

	kfree(g_settings);
	kfree(spec_share);
	pci_set_drvdata(pdev, NULL);
}

/*chip specific g_option configuration routine */
static void hisi_fb_setup(struct hisi_share *share, char *src)
{
	struct hisi_a_share *spec_share;
	char *opt;
#ifdef CAP_EXPENSION
	char *exp_res;
#endif
	int swap;

	spec_share = container_of(share, struct hisi_a_share, share);
#ifdef CAP_EXPENSIION
	exp_res = NULL;
#endif
	swap = 0;

	spec_share->state.initParm.chip_clk = 0;
	spec_share->state.initParm.mem_clk = 0;
	spec_share->state.initParm.master_clk = 0;
	spec_share->state.initParm.power_mode = 0;
	spec_share->state.initParm.all_eng_off = 0;
	spec_share->state.initParm.reset_memory = 1;

	/*defaultly turn g_hwcursor on for both view */
	g_hwcursor = 0;

	if (!src || !*src) {
		war_msg("no specific g_option.\n");
		goto NO_PARAM;
	}


	while ((opt = strsep(&src, ",")) != NULL) {
		if (!strncmp(opt, "swap", strlen("swap")))
			swap = 1;
		else if (!strncmp(opt, "nocrt", strlen("nocrt")))
			spec_share->state.nocrt = 1;
		else if (!strncmp(opt, "36bit", strlen("36bit")))
			spec_share->state.pnltype = TYPE_DOUBLE_TFT;
		else if (!strncmp(opt, "18bit", strlen("18bit")))
			spec_share->state.pnltype = TYPE_DUAL_TFT;
		else if (!strncmp(opt, "24bit", strlen("24bit")))
			spec_share->state.pnltype = TYPE_24TFT;
#ifdef CAP_EXPANSION
		else if (!strncmp(opt, "exp:", strlen("exp:")))
			exp_res = opt + strlen("exp:");
#endif
		else if (!strncmp(opt, "nohwc0", strlen("nohwc0")))
			g_hwcursor &= ~0x1;
		else if (!strncmp(opt, "nohwc1", strlen("nohwc1")))
			g_hwcursor &= ~0x2;
		else if (!strncmp(opt, "nohwc", strlen("nohwc")))
			g_hwcursor = 0;
		else {
			if (!g_fbmode[0]) {
				g_fbmode[0] = opt;
				inf_msg("find fbmode0 : %s.\n", g_fbmode[0]);
			} else if (!g_fbmode[1]) {
				g_fbmode[1] = opt;
				inf_msg("find fbmode1 : %s.\n", g_fbmode[1]);
			} else {
				war_msg("How many view you wann set?\n");
			}
		}
	}
#ifdef CAP_EXPANSION
	if (getExpRes(exp_res, &spec_share->state.xLCD,
		&spec_share->state.yLCD)) {
		/* seems exp_res is not valid*/
		spec_share->state.xLCD = spec_share->state.yLCD = 0;
	}
#endif

NO_PARAM:
	if (share->dual) {
		if (swap)
			spec_share->state.dataflow = hisi_dual_swap;
		else
			spec_share->state.dataflow = hisi_dual_normal;
	} else {
		if (swap)
			spec_share->state.dataflow = hisi_simul_sec;
		else
			spec_share->state.dataflow = hisi_simul_pri;
	}

}

static int __init hisifb_setup(char *options)
{
	int len;
	char *opt, *tmp;


	if (!options || !*options) {
		war_msg("no options.\n");
		return 0;
	}

	inf_msg("options:%s\n", options);
	len = strlen(options) + 1;
	g_settings = kzalloc(len, GFP_KERNEL);
	if (!g_settings)
		return -ENOMEM;

	tmp = g_settings;

	/*    Notes:
	 *	char * strsep(char **s,const char * ct);
	 *	@s: the string to be searched
	 *	@ct :the characters to search for
	 *	strsep() updates @options to pointer after the first found token
	 *	it also returns the pointer ahead the token.
	 */

	while ((opt = strsep(&options, ",")) != NULL) {
		/* options that mean for any  chips are configured here */
		if (!strncmp(opt, "noaccel", strlen("noaccel")))
			g_noaccel = 1;
#ifdef CONFIG_MTRR
		else if (!strncmp(opt, "nomtrr", strlen("nomtrr")))
			g_nomtrr = 1;
#endif
		else if (!strncmp(opt, "dual", strlen("dual")))
			g_dualview = 1;
		else {
			strcat(tmp, opt);
			tmp += strlen(opt);
			if (options != NULL)
				*tmp++ = ',';
			else
				*tmp++ = 0;
		}
	}

	/* misc g_settings are transport to chip specific routines */
	inf_msg("parameter left for chip specific analysis:%s\n", g_settings);
	return 0;
}

static void claim(void)
{
	inf_msg("Hisilicon Driver version: v" _version_ "\n");
}


static int __init hisifb_init(void)
{
	char *option;
	int ret;


	claim();
#ifdef MODULE
	option = g_option;
#else
	if (fb_get_options("hisifb", &option))
		return -ENODEV;
#endif

	hisifb_setup(option);
	ret = pci_register_driver(&hisifb_driver);
	return ret;
}

module_init(hisifb_init);

#ifdef MODULE
static void __exit hisifb_exit(void)
{
	inf_msg(_moduleName_ " exit\n");
	pci_unregister_driver(&hisifb_driver);
}
module_exit(hisifb_exit);

module_param(g_option, charp, S_IRUGO);

MODULE_PARM_DESC(g_option,
"\n\t\tCommon options:\n"
"\t\tnoaccel:disable 2d capabilities\n"
"\t\tnomtrr:disable MTRR attribute for video memory\n"
"\t\tdualview:dual frame buffer feature enabled\n"
"\t\tnohwc:disable hardware cursor\n"
"\t\tUsual example:\n"
"\t\tinsmod ./hisiliconfb.ko g_option=\"noaccel,nohwc,1280x1024-8@60\"\n"
"\t\tFor more detail chip specific options,please contact hisilicon\n"
);
#endif

MODULE_AUTHOR("lixiancai<lixiancai@huawei.com>");
MODULE_DESCRIPTION("Frame buffer driver for Hisilicon graphic device");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, hisi_pci_table);
