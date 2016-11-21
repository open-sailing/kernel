#ifndef HISI_MODE_H__
#define HISI_MODE_H__

#include "hisi_chip.h"


enum spolarity_t {
	POS = 0, /* positive */
	NEG, /* negative */
};


struct mode_para {
	/* Horizontal timing. */
	unsigned long horizontal_total;
	unsigned long h_display_end;
	unsigned long h_sync_start;
	unsigned long h_sync_width;
	enum spolarity_t hsync_polarity;

	/* Vertical timing. */
	unsigned long vertical_total;
	unsigned long v_display_end;
	unsigned long v_sync_start;
	unsigned long v_sync_height;
	enum spolarity_t vsync_polarity;

	/* Refresh timing. */
	unsigned long pixel_clock;
	unsigned long horizontal_freq;
	unsigned long vertical_freq;

	/* Clock Phase. This clock phase only applies to Panel. */
	enum spolarity_t clock_phase_polarity;
};

int hisi_set_mode_timing(struct mode_para *, enum clock_type);

#endif
