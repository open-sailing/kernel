#ifndef HISI_ACCEL_H__
#define HISI_ACCEL_H__

#define HW_ROP2_COPY 0xc
#define HW_ROP2_XOR 0x6


/* blt direction */
#define TOP_TO_BOTTOM 0
#define LEFT_TO_RIGHT 0
#define BOTTOM_TO_TOP 1
#define RIGHT_TO_LEFT 1

void hw_set_2dformat(struct hisi_accel *accel, int fmt);
int hw_de_init(struct hisi_accel *accel);
int hw_fillrect(struct hisi_accel *accel, u32 base, u32 pitch, u32 bpp,
		u32 x, u32 y, u32 width, u32 height, u32 color, u32 rop);

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
	unsigned int rop2
);

int hw_imageblit(
	struct hisi_accel *accel,
	/* pointer to start of source buffer in system memory */
	const char *psrcbuf,
	/* Pitch value (in bytes) of the source buffer,
	 * +ive means top down and -ive mean button up
	 */
	u32 srcdelta,
	/* Mono data can start at any bit in a byte, this value
	 * should be 0 to 7
	 */
	u32 startbit,
	u32 dbase,    /* Address of destination: offset in frame buffer */
	u32 dpitch,   /* Pitch value of destination surface in BYTE */
	u32 byteperpixel,      /* Color depth of destination surface */
	u32 dx,
	u32 dy,       /* Starting coordinate of destination surface */
	u32 width,
	u32 height,   /* width and height of rectange in pixel value */
	/* Foreground color (corresponding to a 1 in the monochrome data */
	u32 fcolor,
	/* Background color (corresponding to a 0 in the monochrome data */
	u32 bcolor,
	u32 rop2
);

#endif
