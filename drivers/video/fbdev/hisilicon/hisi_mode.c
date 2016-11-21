#include "hisi_help.h"
#include "hisi_mode.h"
#include "hisi_reg.h"

/*
 *  Timing parameter for modes supported by Hisilicon
 *  Note that most timings in this table is made according to standard VESA
 *  parameters.
 *  It is a subset of gDefaultModeParamTable[], all timings are copy from it.
 */
static struct mode_para g_hisi_mode_para_table[] = {
/* 640 x 480  [4:3] */
{ 800, 640, 656, 96, NEG, 525, 480, 490, 2, NEG, 25175000, 31469, 60, NEG},

/* 800 x 600  [4:3] */
/* The first 2 commented lines below are taken from SM502, the rest timing are
 * taken from the VESA Monitor Timing Standard
 */
{1056, 800, 840, 128, POS, 628, 600, 601, 4, POS, 40000000, 37879, 60, POS},

/* 1024 x 768  [4:3] */
/* The first 2 commented lines below are taken from SM502, the rest timing are
 * taken from the VESA Monitor Timing Standard
 */
{1344, 1024, 1048, 136, NEG, 806, 768, 771, 6, NEG, 65000000, 48363, 60, NEG},

/* 1152 x 864  [4:3] -- Widescreen eXtended Graphics Array */
{1475, 1152, 1208, 96, POS, 888, 864, 866, 3, POS, 78600000, 53288, 60, NEG},

/* 1280 x 720  [16:9] -- HDTV (WXGA) */
{1650, 1280, 1390, 40, POS, 750, 720, 725, 5, POS, 74250000, 45000, 60, NEG},

/* 1280 x 768  [5:3] -- Not a popular mode */
{1664, 1280, 1344, 128, POS, 798, 768, 771, 7, POS, 79500000, 47776, 60, NEG},

/* 1280 x 960  [4:3] */
/* The first commented line below are taken from SM502, the rest timing are
 * taken from the VESA Monitor Timing Standard
 */
{1800, 1280, 1376, 112, POS, 1000, 960, 961, 3, POS, 108000000, 60000, 60, NEG},

/* 1280 x 1024 [5:4] */
/* GTF with C = 40, M = 600, J = 20, K = 128 */
{1688, 1280, 1328, 112, NEG, 1066, 1024,
	1025, 3, POS, 108000000, 63900, 60, NEG},

/* 1600 x 1200 [4:3]. -- Ultra eXtended Graphics Array */
/* VESA */
{2160, 1600, 1664, 192, POS, 1250, 1200,
	1201, 3, POS, 162000000, 75000, 60, NEG},

/* 1920 x 1080 [16:9]. This is a make-up value, need to be proven.
 * The Pixel clock is calculated based on the maximum resolution of
 * "Single Link" DVI, which support a maximum 165MHz pixel clock.
 * The second values are taken from:
 *http://www.tek.com/Measurement/App_Notes/25_14700/eng/25W_14700_3.pdf
 */
{2200, 1920, 2008, 44, POS, 1125, 1080,
	1084, 5, POS, 148500000, 67500, 60, NEG},

/* 1920 x 1200 [8:5]. -- Widescreen Ultra eXtended Graphics Array (WUXGA) */
{2592, 1920, 2048, 208, NEG, 1242, 1200,
	1201, 3, POS, 193160000, 74522, 60, NEG},

/* End of table. */
{ 0, 0, 0, 0, NEG, 0, 0, 0, 0, NEG, 0, 0, 0, NEG},
};

/*
 *   This function takes care the extra registers and bit fields required to set
 *   up a mode in board.
 *
 *	Explanation about Display Control register:
 *  FPGA only supports 7 predefined pixel clocks, and clock select is
 *  in bit 4:0 of new register 0x802a8.
 */
unsigned int display_ctrl_adjust(struct mode_para *para, unsigned int ctrl)
{
	unsigned long x, y;
	unsigned long pll1; /* bit[31:0] of PLL */
	unsigned long pll2; /* bit[63:32] of PLL */

	x = para->h_display_end;
	y = para->v_display_end;

	/* Hisilicon has to set up a new register for PLL control
	 *(CRT_PLL1_HS & CRT_PLL2_HS).
	 */
	if (x == 800 && y == 600) {
		pll1 = CRT_PLL1_HS_40MHZ;
		pll2 = CRT_PLL2_HS_40MHZ;
	} else if (x == 1024 && y == 768) {
		pll1 = CRT_PLL1_HS_65MHZ;
		pll2 = CRT_PLL2_HS_65MHZ;
	} else if (x == 1152 && y == 864) {
		pll1 = CRT_PLL1_HS_80MHZ_1152;
		pll2 = CRT_PLL2_HS_80MHZ;
	} else if (x == 1280 && y == 768) {
		pll1 = CRT_PLL1_HS_80MHZ;
		pll2 = CRT_PLL2_HS_80MHZ;
	} else if (x == 1280 && y == 720) {
		pll1 = CRT_PLL1_HS_74MHZ;
		pll2 = CRT_PLL2_HS_74MHZ;
	} else if (x == 1280 && y == 960) {
		pll1 = CRT_PLL1_HS_108MHZ;
		pll2 = CRT_PLL2_HS_108MHZ;
	} else if (x == 1280 && y == 1024) {
		pll1 = CRT_PLL1_HS_108MHZ;
		pll2 = CRT_PLL2_HS_108MHZ;
	} else if (x == 1600 && y == 1200) {
		pll1 = CRT_PLL1_HS_162MHZ;
		pll2 = CRT_PLL2_HS_162MHZ;
	} else if (x == 1920 && y == 1080) {
		pll1 = CRT_PLL1_HS_148MHZ;
		pll2 = CRT_PLL2_HS_148MHZ;
	} else if (x == 1920 && y == 1200) {
		pll1 = CRT_PLL1_HS_193MHZ;
		pll2 = CRT_PLL2_HS_193MHZ;
	} else /* default to VGA clock */ {
		pll1 = CRT_PLL1_HS_25MHZ;
		pll2 = CRT_PLL2_HS_25MHZ;
	}

	POKE32(CRT_PLL2_HS, pll2);
	set_vclock_hisilicon(pll1);


	/* Hisilicon has to set up the top-left and bottom-right
	 * registers as well.
	 * Note that normal chip only use those two register for
	 * auto-centering mode.
	 */
	POKE32(CRT_AUTO_CENTERING_TL,
	  FIELD_VALUE(0, CRT_AUTO_CENTERING_TL, TOP, 0)
	| FIELD_VALUE(0, CRT_AUTO_CENTERING_TL, LEFT, 0));

	POKE32(CRT_AUTO_CENTERING_BR,
	  FIELD_VALUE(0, CRT_AUTO_CENTERING_BR, BOTTOM, y-1)
	| FIELD_VALUE(0, CRT_AUTO_CENTERING_BR, RIGHT, x-1));

	/* Assume common fields in ctrl have been properly set before
	 * calling this function.
	 * This function only sets the extra fields in ctrl.
	 */

	/* Set bit 25 of display controller: Select CRT or VGA clock */
	ctrl = FIELD_SET(ctrl, CRT_DISPLAY_CTRL, CRTSELECT, CRT);

	/* Set bit 14 of display controller */
	ctrl &= FIELD_CLEAR(CRT_DISPLAY_CTRL, CLOCK_PHASE);
	if (para->clock_phase_polarity == NEG)
		ctrl = FIELD_SET(ctrl,
			CRT_DISPLAY_CTRL, CLOCK_PHASE, ACTIVE_LOW);
	else
		ctrl = FIELD_SET(ctrl, CRT_DISPLAY_CTRL,
			CLOCK_PHASE, ACTIVE_HIGH);

	POKE32(CRT_DISPLAY_CTRL, ctrl);

	return ctrl;
}

/* only timing related registers will be programed */
static int program_mode_registers(struct mode_para *para, struct pll *pll)
{
	int ret = 0;
	unsigned int val;

	if (pll->clock_type == SECONDARY_PLL) {
		/* programe secondary pixel clock */
		POKE32(CRT_PLL_CTRL, format_pll_reg(pll));
		POKE32(CRT_HORIZONTAL_TOTAL,
			FIELD_VALUE(0, CRT_HORIZONTAL_TOTAL,
			TOTAL, para->horizontal_total - 1)|
			FIELD_VALUE(0, CRT_HORIZONTAL_TOTAL,
			DISPLAY_END, para->h_display_end - 1));

		POKE32(CRT_HORIZONTAL_SYNC,
			FIELD_VALUE(0, CRT_HORIZONTAL_SYNC, WIDTH,
			para->h_sync_width)|
			FIELD_VALUE(0, CRT_HORIZONTAL_SYNC,
			START, para->h_sync_start - 1));

		POKE32(CRT_VERTICAL_TOTAL,
			FIELD_VALUE(0, CRT_VERTICAL_TOTAL,
			TOTAL, para->vertical_total - 1)|
			FIELD_VALUE(0, CRT_VERTICAL_TOTAL,
			DISPLAY_END, para->v_display_end - 1));

		POKE32(CRT_VERTICAL_SYNC,
			FIELD_VALUE(0, CRT_VERTICAL_SYNC,
			HEIGHT, para->v_sync_height)|
			FIELD_VALUE(0, CRT_VERTICAL_SYNC,
			START, para->v_sync_start - 1));

		val = FIELD_VALUE(0, CRT_DISPLAY_CTRL, VSYNC_PHASE,
				para->vsync_polarity)|
			FIELD_VALUE(0, CRT_DISPLAY_CTRL,
				HSYNC_PHASE, para->hsync_polarity)|
			FIELD_SET(0, CRT_DISPLAY_CTRL, TIMING, ENABLE)|
			FIELD_SET(0, CRT_DISPLAY_CTRL, PLANE, ENABLE);

		display_ctrl_adjust(para, val);
	}  else {
		ret = -1;
	}
	return ret;
}

/*
 *  find_mode_para
 *      This function locates the requested mode in the given parameter table
 *
 *  Input:
 *      width           - Mode width
 *      height          - Mode height
 *      refresh_rate    - Mode refresh rate
 *      index           - Index that is used for multiple search of the same
 *                        mode that have the same width, height, and refresh
 *                        rate,but have different timing parameters.
 *
 *  Output:
 *      Success: return a pointer to the mode_para entry.
 *      Fail: a NULL pointer.
 */
struct mode_para *find_mode_para(
	unsigned long width,
	unsigned long height,
	unsigned long refresh_rate,
	unsigned short index,
	struct mode_para *para
)
{
	unsigned short modeIndex = 0, tempIndex = 0;

	/* Walk the entire mode table. */
	while (para[modeIndex].pixel_clock != 0) {
		if (((width == (unsigned long)(-1))
			|| (para[modeIndex].h_display_end == width))
		    && ((height == (unsigned long)(-1))
			|| (para[modeIndex].v_display_end == height))
		    && ((refresh_rate == (unsigned long)(-1))
			|| (para[modeIndex].vertical_freq == refresh_rate))) {

			if (tempIndex < index)
				tempIndex++;
			else
				return &para[modeIndex];
		}

		/* Next entry */
		modeIndex++;
	}

	/* No match */
	return NULL;
}

int hisi_set_mode_timing(struct mode_para *para, enum clock_type clock)
{
	struct pll pll;
	unsigned long width;
	unsigned long height;
	unsigned long refresh_rate;


	pll.intput_freq = DEFAULT_INPUT_CLOCK;
	pll.clock_type = clock;

	width = para->h_display_end;
	height = para->v_display_end;
	refresh_rate = 60;

	para = find_mode_para(width, height, refresh_rate, 0,
						g_hisi_mode_para_table);

	/* fix Segmentation fault on arm platform */
#if defined(__i386__) || defined(__x86_64__)
		/* set graphic mode via IO method */
	outb_p(0x88, 0x3d4);
	outb_p(0x06, 0x3d5);
#endif

	program_mode_registers(para, &pll);
	return 0;
}
