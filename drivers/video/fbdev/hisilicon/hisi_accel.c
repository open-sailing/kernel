#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
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
#include "hisi_accel.h"
#include "hisi_help.h"
#include "hisi_reg.h"

static inline void write_dpr(struct hisi_accel *accel, int offset, u32 value)
{
	writel(value, accel->dpr_base + offset);
}

static inline u32 read_dpr(struct hisi_accel *accel, int offset)
{
	return readl(accel->dpr_base + offset);
}

static inline void write_dpport(struct hisi_accel *accel, u32 data)
{
	writel(data, accel->dp_port_base);
}

int hw_de_init(struct hisi_accel *accel)
{
	/* setup 2d engine registers */
	u32 reg;
	u32 clr;

	write_dpr(accel, DE_MASKS, 0xFFFFFFFF);
	/* dpr1c */
	reg = FIELD_SET(0, DE_STRETCH_FORMAT, PATTERN_XY, NORMAL) |
	FIELD_VALUE(0, DE_STRETCH_FORMAT, PATTERN_Y, 0) |
	FIELD_VALUE(0, DE_STRETCH_FORMAT, PATTERN_X, 0) |
	FIELD_SET(0, DE_STRETCH_FORMAT, ADDRESSING, XY) |
	FIELD_VALUE(0, DE_STRETCH_FORMAT, SOURCE_HEIGHT, 3);

	clr = FIELD_CLEAR(DE_STRETCH_FORMAT, PATTERN_XY) &
		FIELD_CLEAR(DE_STRETCH_FORMAT, PATTERN_Y) &
		FIELD_CLEAR(DE_STRETCH_FORMAT, PATTERN_X) &
		FIELD_CLEAR(DE_STRETCH_FORMAT, ADDRESSING) &
		FIELD_CLEAR(DE_STRETCH_FORMAT, SOURCE_HEIGHT);

	/* DE_STRETCH bpp format need be initilized in setMode routine */
	write_dpr(accel, DE_STRETCH_FORMAT,
	(read_dpr(accel, DE_STRETCH_FORMAT) & clr) | reg);

	/* disable clipping and transparent */
	write_dpr(accel, DE_CLIP_TL, 0);/*dpr2c*/
	write_dpr(accel, DE_CLIP_BR, 0);/*dpr30*/

	write_dpr(accel, DE_COLOR_COMPARE_MASK, 0);/*dpr24*/
	write_dpr(accel, DE_COLOR_COMPARE, 0);

	reg = FIELD_SET(0, DE_CONTROL, TRANSPARENCY, DISABLE) |
		FIELD_SET(0, DE_CONTROL, TRANSPARENCY_MATCH, OPAQUE) |
		FIELD_SET(0, DE_CONTROL, TRANSPARENCY_SELECT, SOURCE);

	clr = FIELD_CLEAR(DE_CONTROL, TRANSPARENCY) &
		FIELD_CLEAR(DE_CONTROL, TRANSPARENCY_MATCH) &
		FIELD_CLEAR(DE_CONTROL, TRANSPARENCY_SELECT);

	/* dpr0c */
	write_dpr(accel, DE_CONTROL, (read_dpr(accel, DE_CONTROL) & clr) | reg);

	return 0;
}

/* set2dformat only be called from setmode functions
 * but if you need dual framebuffer driver,need call set2dformat
 * every time you use 2d function
 */
void hw_set_2dformat(struct hisi_accel *accel, int fmt)
{
	u32 reg;
	/* fmt=0,1,2 for 8,16,32 */
	reg = read_dpr(accel, DE_STRETCH_FORMAT);
	reg = FIELD_VALUE(reg, DE_STRETCH_FORMAT, PIXEL_FORMAT, fmt);
	write_dpr(accel, DE_STRETCH_FORMAT, reg);
}

int hw_fillrect(
	struct hisi_accel *accel, u32 base,
	u32 pitch, u32 Bpp,
	u32 x, u32 y, u32 width,
	u32 height, u32 color, u32 rop)
{
	u32 deCtrl;

	if (accel->de_wait() != 0) {
		/* int time wait and always busy,seems hardware
		 * got something error
		 */
		dbg_msg("%s:De engine always bussy\n", __func__);
		return -1;
	}

	write_dpr(accel, DE_WINDOW_DESTINATION_BASE, base);/*dpr40*/
	write_dpr(accel, DE_PITCH,
		   FIELD_VALUE(0, DE_PITCH, DESTINATION, pitch / Bpp) |
		   FIELD_VALUE(0, DE_PITCH, SOURCE, pitch/Bpp));/*dpr10*/

	write_dpr(accel, DE_WINDOW_WIDTH,
		   FIELD_VALUE(0, DE_WINDOW_WIDTH, DESTINATION, pitch / Bpp) |
		   FIELD_VALUE(0, DE_WINDOW_WIDTH, SOURCE, pitch / Bpp));

	write_dpr(accel, DE_FOREGROUND, color);/*DPR14*/

	write_dpr(accel, DE_DESTINATION,
		   FIELD_SET(0, DE_DESTINATION, WRAP, DISABLE) |
		   FIELD_VALUE(0, DE_DESTINATION, X, x) |
		   FIELD_VALUE(0, DE_DESTINATION, Y, y));/*dpr4*/

	write_dpr(accel, DE_DIMENSION,
		   FIELD_VALUE(0, DE_DIMENSION, X, width) |
		   FIELD_VALUE(0, DE_DIMENSION, Y_ET, height));/*dpr8*/

	deCtrl = FIELD_SET(0, DE_CONTROL, STATUS, START) |
		FIELD_SET(0, DE_CONTROL, DIRECTION, LEFT_TO_RIGHT) |
		FIELD_SET(0, DE_CONTROL, LAST_PIXEL, ON) |
		FIELD_SET(0, DE_CONTROL, COMMAND, RECTANGLE_FILL) |
		FIELD_SET(0, DE_CONTROL, ROP_SELECT, ROP2) |
		FIELD_VALUE(0, DE_CONTROL, ROP, rop);/*dpr0xc*/

	write_dpr(accel, DE_CONTROL, deCtrl);

	return 0;
}

int hw_copyarea(
	struct hisi_accel *accel,
	unsigned int sbase,  /* Address of source: offset in frame buffer */
	unsigned int spitch, /* Pitch value of source surface in BYTE */
	unsigned int sx,
	unsigned int sy,     /* Starting coordinate of source surface */
	/* Address of destination: offset in frame buffer */
	unsigned int dbase,
	unsigned int dpitch, /* Pitch value of destination surface in BYTE */
	unsigned int bpp,    /* Color depth of destination surface */
	unsigned int dx,
	unsigned int dy,     /* Starting coordinate of destination surface */
	unsigned int width,
	unsigned int height,
	unsigned int rop2)
{
	unsigned int direction;
	unsigned int de_ctrl;
	int opsign;

	direction = LEFT_TO_RIGHT;
	/* Direction of ROP2 operation:
	 * 1 = Left to Right, (-1) = Right to Left
	 */
	opsign = 1;
	de_ctrl = 0;

	/* If source and destination are the same surface,
	 * need to check for overlay cases
	 */
	if (sbase == dbase && spitch == dpitch) {
		/* Determine direction of operation */
		if (sy < dy) {
			direction = BOTTOM_TO_TOP;
		} else if (sy > dy) {
			direction = TOP_TO_BOTTOM;
		} else {
		/* sy == dy */
			if (sx <= dx) {
				direction = RIGHT_TO_LEFT;
			} else {
			/* sx > dx */
				direction = LEFT_TO_RIGHT;
			}
		}
	}

	if ((direction == BOTTOM_TO_TOP) || (direction == RIGHT_TO_LEFT)) {
		sx += width - 1;
		sy += height - 1;
		dx += width - 1;
		dy += height - 1;
		opsign = (-1);
	}

	/* Note:
	 * DE_FOREGROUND are DE_BACKGROUND are don't care.
	 * DE_COLOR_COMPARE and DE_COLOR_COMPARE_MAKS
	 * are set by set deSetTransparency().
	 */

	/* 2D Source Base.
	 * It is an address offset (128 bit aligned) from the beginning
	 * of frame buffer.
	 */
	write_dpr(accel, DE_WINDOW_SOURCE_BASE, sbase); /* dpr40 */

	/* 2D Destination Base.
	 * It is an address offset (128 bit aligned) from the beginning
	 * of frame buffer.
	 */
	write_dpr(accel, DE_WINDOW_DESTINATION_BASE, dbase); /* dpr44 */

	write_dpr(accel, DE_PITCH,
		   FIELD_VALUE(0, DE_PITCH, DESTINATION, (dpitch / bpp)) |
		   FIELD_VALUE(0, DE_PITCH, SOURCE, (spitch / bpp))); /* dpr10*/

	/* Screen Window width in Pixels.
	 * 2D engine uses this value to calculate the linear address
	 * in frame buffer for a given point.
	 */
	/* dpr3c */
	write_dpr(accel, DE_WINDOW_WIDTH,
		   FIELD_VALUE(0, DE_WINDOW_WIDTH,
				DESTINATION, (dpitch / bpp)) |
		   FIELD_VALUE(0, DE_WINDOW_WIDTH, SOURCE, (spitch / bpp)));

	if (accel->de_wait() != 0)
		return -1;

	write_dpr(accel, DE_SOURCE,
		   FIELD_SET(0, DE_SOURCE, WRAP, DISABLE) |
		   FIELD_VALUE(0, DE_SOURCE, X_K1, sx) |
		   FIELD_VALUE(0, DE_SOURCE, Y_K2, sy)); /* dpr0 */
	write_dpr(accel, DE_DESTINATION,
		   FIELD_SET(0, DE_DESTINATION, WRAP, DISABLE) |
		   FIELD_VALUE(0, DE_DESTINATION, X,    dx) |
		   FIELD_VALUE(0, DE_DESTINATION, Y,    dy)); /* dpr04 */
	write_dpr(accel, DE_DIMENSION,
		   FIELD_VALUE(0, DE_DIMENSION, X, width) |
		   FIELD_VALUE(0, DE_DIMENSION, Y_ET, height)); /* dpr08 */

	de_ctrl = FIELD_VALUE(0, DE_CONTROL, ROP, rop2) |
		FIELD_SET(0, DE_CONTROL, ROP_SELECT, ROP2) |
		FIELD_SET(0, DE_CONTROL, COMMAND, BITBLT) |
		((direction == RIGHT_TO_LEFT) ?
		FIELD_SET(0, DE_CONTROL, DIRECTION, RIGHT_TO_LEFT)
		: FIELD_SET(0, DE_CONTROL, DIRECTION, LEFT_TO_RIGHT)) |
		FIELD_SET(0, DE_CONTROL, STATUS, START);
	write_dpr(accel, DE_CONTROL, de_ctrl); /* dpr0c */

	return 0;
}

static unsigned int de_get_transparency(struct hisi_accel *accel)
{
	unsigned int de_ctrl;

	de_ctrl = read_dpr(accel, DE_CONTROL);

	de_ctrl &= FIELD_MASK(DE_CONTROL_TRANSPARENCY_MATCH) |
		FIELD_MASK(DE_CONTROL_TRANSPARENCY_SELECT) |
		FIELD_MASK(DE_CONTROL_TRANSPARENCY);

	return de_ctrl;
}

int hw_imageblit(
	struct hisi_accel *accel,
	/* pointer to start of source buffer in system memory */
	const char *psrcbuf,
	/* Pitch value (in bytes) of the source buffer,
	 * +ive means top down and -ive mean button up
	 */
	u32 srcdelta,
	/* Mono data can start at any bit in a byte,
	 * this value should be 0 to 7
	 */
	u32 startbit,
	u32 dbase,    /* Address of destination: offset in frame buffer */
	u32 dpitch,   /* Pitch value of destination surface in BYTE */
	u32 bytperpxl, /* Color depth of destination surface */
	u32 dx,
	u32 dy,       /* Starting coordinate of destination surface */
	u32 width,
	u32 height,   /* width and height of rectange in pixel value */
	/* Foreground color (corresponding to a 1 in the monochrome data */
	u32 fcolor,
	/* Background color (corresponding to a 0 in the monochrome data */
	u32 bcolor,
	u32 rop2)     /* ROP value */
{
	unsigned int bytes_per_scan;
	unsigned int qbytes_per_scan;
	unsigned int bytes_remain;
	unsigned int de_ctrl = 0;
	unsigned char ajRemain[4];
	int i, j;

	/* Just make sure the start bit is within legal range */
	startbit &= 7;
	bytes_per_scan = (width + startbit + 7) / 8;
	qbytes_per_scan = bytes_per_scan & ~3;
	bytes_remain = bytes_per_scan & 3;

	if (accel->de_wait() != 0)
		return -1;

	/* 2D Source Base. Use 0 for HOST Blt.*/
	write_dpr(accel, DE_WINDOW_SOURCE_BASE, 0);

	/* 2D Destination Base.
	 * It is an address offset (128 bit aligned)
	 * from the beginning of frame buffer.
	 */
	write_dpr(accel, DE_WINDOW_DESTINATION_BASE, dbase);
	/* dpr10 */
	write_dpr(accel, DE_PITCH,
		   FIELD_VALUE(0, DE_PITCH, DESTINATION, dpitch / bytperpxl)|
		   FIELD_VALUE(0, DE_PITCH, SOURCE, dpitch / bytperpxl));

	/* Screen Window width in Pixels.
	* 2D engine uses this value to calculate
	* the linear address in frame buffer
	* for a given point.
	*/
	write_dpr(accel, DE_WINDOW_WIDTH,
		   FIELD_VALUE(0, DE_WINDOW_WIDTH, DESTINATION,
				(dpitch / bytperpxl)) |
		   FIELD_VALUE(0, DE_WINDOW_WIDTH, SOURCE,
				(dpitch / bytperpxl))
		);

	/* Note: For 2D Source in Host Write, only X_K1_MONO
	* field is needed, and Y_K2 field is not used.For mono bitmap,
	* use startBit for X_K1.
	*/
	write_dpr(accel, DE_SOURCE,
		   FIELD_SET(0, DE_SOURCE, WRAP, DISABLE) |
		   FIELD_VALUE(0, DE_SOURCE, X_K1_MONO, startbit)); /* dpr00 */

	write_dpr(accel, DE_DESTINATION,
		   FIELD_SET(0, DE_DESTINATION, WRAP, DISABLE) |
		   FIELD_VALUE(0, DE_DESTINATION, X,    dx)    |
		   FIELD_VALUE(0, DE_DESTINATION, Y,    dy)); /* dpr04 */

	write_dpr(accel, DE_DIMENSION,
		   FIELD_VALUE(0, DE_DIMENSION, X,    width) |
		   FIELD_VALUE(0, DE_DIMENSION, Y_ET, height)); /* dpr08 */

	write_dpr(accel, DE_FOREGROUND, fcolor);
	write_dpr(accel, DE_BACKGROUND, bcolor);

	de_ctrl = FIELD_VALUE(0, DE_CONTROL, ROP, rop2) |
		FIELD_SET(0, DE_CONTROL, ROP_SELECT, ROP2)    |
		FIELD_SET(0, DE_CONTROL, COMMAND, HOST_WRITE) |
		FIELD_SET(0, DE_CONTROL, HOST, MONO)          |
		FIELD_SET(0, DE_CONTROL, STATUS, START);

	write_dpr(accel, DE_CONTROL,
		   de_ctrl | de_get_transparency(accel));

	/* Write MONO data (line by line) to 2D Engine data port */
	for (i = 0; i < height; i++) {
		/* For each line, send the data in chunks of 4 bytes */
		for (j = 0; j < (qbytes_per_scan/4); j++)
			write_dpport(accel,
			*(unsigned int *)(psrcbuf + (j * 4)));

		if (bytes_remain) {
			memcpy(ajRemain, psrcbuf+qbytes_per_scan,
				bytes_remain);
			write_dpport(accel, *(unsigned int *)ajRemain);
		}

		psrcbuf += srcdelta;
	}

	return 0;
}
