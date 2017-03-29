#include <linux/delay.h>
#include "hisi_chip.h"
#include "hisi_help.h"
#include "hisi_power.h"
#include "hisi_reg.h"

void set_vclock_hisilicon(unsigned long pll)
{
	unsigned long tmp0, tmp1;

    /* 1. outer_bypass_n=0 */
	tmp0 = PEEK32(CRT_PLL1_HS);
	tmp0 &= 0xBFFFFFFF;
	POKE32(CRT_PLL1_HS, tmp0);

	/* 2. pll_pd=1?inter_bypass=1 */
	POKE32(CRT_PLL1_HS, 0x21000000);

	/* 3. config pll */
	POKE32(CRT_PLL1_HS, pll);

	/* 4. delay  */
	mdelay(1);

	/* 5. pll_pd =0 */
	tmp1 = pll & ~0x01000000;
	POKE32(CRT_PLL1_HS, tmp1);

	/* 6. delay  */
	mdelay(1);

	/* 7. inter_bypass=0 */
	tmp1 &= ~0x20000000;
	POKE32(CRT_PLL1_HS, tmp1);

    /* 8. delay  */
	mdelay(1);

	/* 9. outer_bypass_n=1 */
	tmp1 |= 0x40000000;
	POKE32(CRT_PLL1_HS, tmp1);
}


int hisi_init_hw(struct _initchip_param_t *p_init_param)
{
	unsigned int reg;

	if (p_init_param->powerMode != 0)
		p_init_param->powerMode = 0;
	set_power_mode(p_init_param->powerMode);

	/* Enable display power gate & LOCALMEM power gate*/
	reg = PEEK32(CURRENT_GATE);
	reg = FIELD_SET(reg, CURRENT_GATE, DISPLAY, ON);
	reg = FIELD_SET(reg, CURRENT_GATE, LOCALMEM, ON);
	set_current_gate(reg);

	{
#if defined(__i386__) || defined(__x86_64__)
		/* set graphic mode via IO method */
		outb_p(0x88, 0x3d4);
		outb_p(0x06, 0x3d5);
#endif
	}

	/* Reset the memory controller. If the memory controller
	 * is not reset in chip,the system might hang when sw accesses
	 * the memory.The memory should be resetted after
	 * changing the MXCLK.
	 */
	if (p_init_param->resetMemory == 1) {
		reg = PEEK32(MISC_CTRL);
		reg = FIELD_SET(reg, MISC_CTRL, LOCALMEM_RESET, RESET);
		POKE32(MISC_CTRL, reg);

		reg = FIELD_SET(reg, MISC_CTRL, LOCALMEM_RESET, NORMAL);
		POKE32(MISC_CTRL, reg);
	}

	if (p_init_param->setAllEngOff == 1) {
		enable_2d_engine(0);

		/* Disable Overlay, if a former application left it on */
		reg = PEEK32(VIDEO_DISPLAY_CTRL);
		reg = FIELD_SET(reg, VIDEO_DISPLAY_CTRL, PLANE, DISABLE);
		POKE32(VIDEO_DISPLAY_CTRL, reg);

		/* Disable video alpha, if a former application left it on */
		reg = PEEK32(VIDEO_ALPHA_DISPLAY_CTRL);
		reg = FIELD_SET(reg, VIDEO_ALPHA_DISPLAY_CTRL, PLANE, DISABLE);
		POKE32(VIDEO_ALPHA_DISPLAY_CTRL, reg);

		/* Disable alpha plane, if a former application left it on */
		reg = PEEK32(ALPHA_DISPLAY_CTRL);
		reg = FIELD_SET(reg, ALPHA_DISPLAY_CTRL, PLANE, DISABLE);
		POKE32(ALPHA_DISPLAY_CTRL, reg);

		/* Disable DMA Channel, if a former application left it on */
		reg = PEEK32(DMA_ABORT_INTERRUPT);
		reg = FIELD_SET(reg, DMA_ABORT_INTERRUPT, ABORT_1, ABORT);
		POKE32(DMA_ABORT_INTERRUPT, reg);

		/* Disable DMA Power, if a former application left it on */
		enable_dma(0);
	}

	/* We can add more initialization as needed. */

	return 0;
}

unsigned int format_pll_reg(struct pll *ppll)
{
	unsigned int pllreg = 0;

    /* Note that all PLL's have the same format. Here,
     *we just use Panel PLL parameter to work out the bit
     * fields in the register.On returning a 32 bit number, the value can
     * be applied to any PLL in the calling function.
     */
	pllreg = FIELD_SET(0, PANEL_PLL_CTRL, BYPASS, OFF) |
	FIELD_SET(0, PANEL_PLL_CTRL, POWER,  ON)|
	FIELD_SET(0, PANEL_PLL_CTRL, INPUT,  OSC)|
	FIELD_VALUE(0, PANEL_PLL_CTRL, POD,    ppll->POD)|
	FIELD_VALUE(0, PANEL_PLL_CTRL, OD,     ppll->OD)|
	FIELD_VALUE(0, PANEL_PLL_CTRL, N,      ppll->N)|
	FIELD_VALUE(0, PANEL_PLL_CTRL, M,      ppll->M);

	return pllreg;
}
