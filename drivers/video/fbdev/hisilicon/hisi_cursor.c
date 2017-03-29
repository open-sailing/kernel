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
#include "hisi_help.h"
#include "hisi_reg.h"


#define WRITE_REG(addr, data) writel((data), cursor->mmio + (addr))


void hw_cursor_enable(struct hisi_cursor *cursor)
{
	u32 reg;

	reg = FIELD_VALUE(0, HWC_ADDRESS, ADDRESS, cursor->offset) |
		FIELD_SET(0, HWC_ADDRESS, EXT, LOCAL) |
		FIELD_SET(0, HWC_ADDRESS, ENABLE, ENABLE);
	WRITE_REG(HWC_ADDRESS, reg);
}

void hw_cursor_disable(struct hisi_cursor *cursor)
{
	WRITE_REG(HWC_ADDRESS, 0);
}

void hw_cursor_set_size(struct hisi_cursor *cursor, int w, int h)
{
	cursor->w = w;
	cursor->h = h;
}

void hw_cursor_set_pos(struct hisi_cursor *cursor, int x, int y)
{
	u32 reg;

	reg = FIELD_VALUE(0, HWC_LOCATION, Y, y) |
		FIELD_VALUE(0, HWC_LOCATION, X, x);
	WRITE_REG(HWC_LOCATION, reg);
}

void hw_cursor_set_color(struct hisi_cursor *cursor, u32 fg,  u32 bg)
{
	WRITE_REG(HWC_COLOR_12, (fg << 16)|(bg & 0xffff));
	WRITE_REG(HWC_COLOR_3, 0xffe0);
}

void hw_cursor_set_data(struct hisi_cursor *cursor,
			u16 rop, const u8 *pcol, const u8 *pmsk)
{
	struct hisifb_crtc *crtc;

	int i, j, count, pitch, offset;
	u8 color, mask, opr;
	u16 data;
	u16  *pbuffer, *pstart;

	crtc = container_of(cursor, struct hisifb_crtc, cursor);
	/*  in byte*/
	pitch = cursor->w >> 3;

	/* in byte	*/
	count = pitch * cursor->h;

	/* in ushort */
	offset = cursor->maxW * 2 / 8 / 2;

	data = 0;
	pstart = (u16 *)cursor->vstart;
	pbuffer = pstart;


	for (i = 0; i < count; i++) {
		color = *pcol++;
		mask = *pmsk++;
		data = 0;

		/* either method below works well,
		 * but method 2 shows no lag
		 * and method 1 seems a bit wrong
		 */

		for (j = 0; j < 8; j++) {
			if (mask & (0x80 >> j)) {
				if (rop == ROP_XOR)
					opr = mask ^ color;
				else
					opr = mask & color;

				/* 2 stands for forecolor and 1 for backcolor */
				data |= ((opr & (0x80 >> j)) ? 2 : 1) <<
					(j * 2);
			}
		}

		*pbuffer = data;

		/* assume pitch is 1,2,4,8,...*/
		if ((i+1) % pitch == 0) {
			/* need a return */
			pstart += offset;
			pbuffer = pstart;
		} else {
			pbuffer++;
		}
	}
}
