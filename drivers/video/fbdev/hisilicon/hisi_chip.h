#ifndef HISI_CHIP_H__
#define HISI_CHIP_H__

#define DEFAULT_INPUT_CLOCK 14318181 /* Default reference clock */


enum clock_type {
	MXCLK_PLL,
	PRIMARY_PLL,
	SECONDARY_PLL,
	VGA0_PLL,
	VGA1_PLL,
};

struct pll {
	enum clock_type clock_type;
	unsigned long intput_freq; /* Input clock frequency to the PLL */

	/* Use this when clock_type = PANEL_PLL */
	unsigned long M;
	unsigned long N;
	unsigned long OD;
	unsigned long POD;
};

/* input struct to initChipParam() function */
struct _initchip_param_t {
	unsigned short powerMode;    /* Use power mode 0 or 1 */
	unsigned short chipClock;
				/* Speed of main chip clock in MHz unit
				 *0 = keep the current clock setting
				 *Others = the new main chip clock
				 */
	unsigned short memClock;
				/* Speed of memory clock in MHz unit
				 * 0 = keep the current clock setting
				 * Others = the new memory clock
				 */
	unsigned short masterClock;
				/* Speed of master clock in MHz unit
				 *0 = keep the current clock setting
				 *Others = the new master clock
				 */
	unsigned short setAllEngOff;
				/* 0 = leave all engine state untouched.
				 *1 = make sure they are off: 2D, Overlay,
				 *video alpha, alpha, hardware cursors
				 */
	unsigned char resetMemory;
				/* 0 = Do not reset the memory controller
				 *1 = Reset the memory controller
				 */

	/* More initialization parameter can be added if needed */
};


unsigned int format_pll_reg(struct pll *pll);
int hisi_init_hw(struct _initchip_param_t *);
void set_vclock_hisilicon(unsigned long pll);

#endif
