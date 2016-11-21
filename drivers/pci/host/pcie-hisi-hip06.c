/*
 * PCIe host controller driver for HiSilicon Hip05 SoC
 *
 * Copyright (C) 2015 HiSilicon Co., Ltd. http://www.hisilicon.com
 *
 * Authors: Zhou Wang <wangzhou1@hisilicon.com>
 *          Gabriele Paoloni <gabriele.paoloni@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irqreturn.h>
#include "pcie-designware.h"
#include "pcie-hisi.h"

#define HIP06_PCIE_CLK_CTRL                             0x7
#define HIP06_PCS_SERDES_STATUS                         0x504
#define HIP06_PCIE_CORE_RESET                           0x3
#define HIP06_AUTO_LANE_FLIP_CTRL_EN                    BIT(16)

#define PCIE_SYS_CTRL_13_REG                            0x4
#define PCIE_MST_BYPASS_SMMU_EN                         BIT(10)
#define PCIE_MST_AW_BYPASS_SMMU_EN                      BIT(12)
#define PCIE_MST_AR_BYPASS_SMMU_EN                      BIT(13)

#define HIP06_PCS_RXVALID_CFG_EN                        BIT(14)
#define HIP06_PCS_RXVALID_CFG_MASK                      BIT(15)

#define PCIE_MAX_LANE_NUM                               8

static void hisi_pcie_rx_valid_ctrl_hip06(struct hisi_pcie *pcie, bool on)
{
	u32 i;
	u32 lane_num;
	u32 val;
	u32 lane_id;
	u32 loop_cnt;
	u32 locked_cnt[PCIE_MAX_LANE_NUM];

	lane_num = pcie->pp.lanes;
	memset(locked_cnt, 0, sizeof(locked_cnt));

	if (on) {
		/*
		 * to valid the RX, firstly, we should check and make
		 * sure the RX lanes have been steadily locked.
		 */
		for (loop_cnt = 500 * lane_num; loop_cnt > 0; loop_cnt--) {
			lane_id = loop_cnt % lane_num;

			val = hisi_pcie_pcs_readl(pcie, 0xf4 + lane_id * 4);
			if (((val >> 21) & 0x7) >= 4)
				locked_cnt[lane_id]++;
			else
				locked_cnt[lane_id] = 0;

			/*
			 * If we get a locked status above 8 times incessantly
			 * on anyone of the lanes, we get a stable lock.
			 */
			if (locked_cnt[lane_id] >= 8)
				break;
			if (lane_id == (lane_num - 1))
				udelay(500);
		}

		if (loop_cnt == 0)
			dev_err(pcie->pp.dev, "pcs locked timeout!\n");

		for (i = 0; i < lane_num; i++) {
			val = hisi_pcie_pcs_readl(pcie, 0x204 + i * 0x4);
			val &= (~HIP06_PCS_RXVALID_CFG_EN);
			hisi_pcie_pcs_writel(pcie, val, 0x204 + i * 0x4);
		}
	} else {
		for (i = 0; i < lane_num; i++) {
			val = hisi_pcie_pcs_readl(pcie, 0x204 + i * 0x4);
			val |= HIP06_PCS_RXVALID_CFG_EN;
			val &= (~HIP06_PCS_RXVALID_CFG_MASK);
			hisi_pcie_pcs_writel(pcie, val, 0x204 + i * 0x4);
		}
	}
}

/*Check if the link is up*/
static int hisi_pcie_link_up_hip06(struct hisi_pcie *pcie)
{
	u32 val;
	u32 count;

	/*
	 * There are three possible states of the link when
	 * this code is called:
	 * 1) The link is UP stably (in L0 state);
	 * 2) The link is UP, but still in LTSSM training
	 *     Wait for the training to finish, which should take a very short
	 *     time. If the training does not finish, we return 0
	 *     to indicate linkdown state. If the training does finish,
	 *     the link is up and operating correctly.
	 * 3) The link is down.
	 */
	count = 0;
	while (1) {
		val = hisi_pcie_ctrl_readl(pcie, PCIE_SYS_STATE4);
		val = val & PCIE_LTSSM_STATE_MASK;

		if (val == PCIE_LTSSM_LINKUP_STATE)
			return 1;
		/* The longest time for recovery stage could be 150ms,
		 * time delay in each while loop is 500us, therefore
		 * the loop count is limited to 300.
		 */
		else if (count >= 300)
			return 0;
		/*
		 * Wait for training to the L0 state in the following states:
		 * RCVRY_LOCK/RCVRY_SPEED/RCVRY_CFG/RCVRY_IDLE/
		 * RCVRY_EQ0/RCVRY_EQ1/RCVRY_EQ2/RCVRY_EQ3;
		 */
		else if (((val >= PCIE_LTSSM_RCVRY_LOCK_STATE) &&
			       (val <= PCIE_LTSSM_RCVRY_IDLE_STATE)) ||
			      ((val >= PCIE_LTSSM_RCVRY_EQ0_STATE) &&
			       (val <= PCIE_LTSSM_RCVRY_EQ3_STATE))) {
			udelay(500);
			count++;
			continue;
		} else
			return 0;
	}
}

/* Configure vmid/asid table in PCIe host */
static void hisi_pcie_config_context_hip06(struct hisi_pcie *pcie)
{
	u32 val;

	hisi_pcie_ctrl_writel(pcie, pcie->msi_addr & 0xffffffff,
			      PCIE_MSI_LOW_ADDRESS);
	hisi_pcie_ctrl_writel(pcie, pcie->msi_addr >> 32,
			      PCIE_MSI_HIGH_ADDRESS);
	val = hisi_pcie_ctrl_readl(pcie, PCIE_MSI_TRANS_REG);
	val |= PCIE_MSI_TRANS_ENABLE;
	hisi_pcie_ctrl_writel(pcie, val, PCIE_MSI_TRANS_REG);
}

static void hisi_pcie_enable_ltssm_hip06(struct hisi_pcie *pcie, bool on)
{
	u32 val;

	val = hisi_pcie_ctrl_readl(pcie, PCIE_CTRL_7_REG);
	if (on)
		/* In Hip06 host will fill all 1s in CFG cpu address space
		 * when host sends a read CFG TLP with wrong BDF if we set
		 * PCIE_RD_ERR_RESPONSE_EN as 1. And host will trigger a error
		 * interrupt when host sends a read/write CFG TLP with wrong
		 * BDF if we set PCIE_RD_ERR_RESPONSE_EN/PCIE_WR_ERR_RESPONSE_EN
		 * as 1
		 */
		val |= PCIE_LTSSM_ENABLE_SHIFT |
		    PCIE_RD_ERR_RESPONSE_EN | PCIE_WR_ERR_RESPONSE_EN;
	else
		val &= ~(PCIE_LTSSM_ENABLE_SHIFT);
	hisi_pcie_ctrl_writel(pcie, val, PCIE_CTRL_7_REG);

	/* reopen the RX_VALID */
	hisi_pcie_rx_valid_ctrl_hip06(pcie, true);
}

static void hisi_pcie_init_pcs_hip06(struct hisi_pcie *pcie)
{
	u32 i;
	u32 val;

	/*
	 * For hip06, the max lane width of PCIe controller in hardware is
	 * 8, it is probable to link up fail when the lane width is set less
	 * than 8 for a defect of the SOC.
	 * Add the configuration based on the user guide(B05) of the SOC
	 * to fix the problem.
	 */
	for (i = 0; i < PCIE_MAX_LANE_NUM; i++) {
		val = hisi_pcie_pcs_readl(pcie, 0x204 + i * 0x4);
		val |= (1u << 20);
		hisi_pcie_pcs_writel(pcie, val, 0x204 + i * 0x4);

		hisi_pcie_pcs_writel(pcie, 0x6, 0x508 + i * 0x4);
	}

	/* The time that Synopsys PCIe Controller lane polarity inversion
	 * judgment and Serdes CDR lock has an overlapping region. This may
	 * lead a wrong result for lane polarity inversion judgement if the
	 * CDR lock don't completely finished. And then, some lanes will
	 * lost when link up.
	 * To fix the problem ,we close the RX_VALID after PCS config , and
	 * reopen it when we detect a stable lock status of PCS after the
	 * LTSSM is enabled.
	 */
	hisi_pcie_rx_valid_ctrl_hip06(pcie, false);

	hisi_pcie_pcs_writel(pcie, 0x3D090, 0x264);
}

void pcie_equalization_hip06(struct hisi_pcie *pcie)
{
	u32 val;

	hisi_pcie_apb_writel(pcie, 0x1c00, 0x890);

	pcie_equalization_common(pcie);

	hisi_pcie_apb_writel(pcie, 0x44444444, 0x164);
	hisi_pcie_apb_writel(pcie, 0x44444444, 0x168);
	hisi_pcie_apb_writel(pcie, 0x44444444, 0x16c);
	hisi_pcie_apb_writel(pcie, 0x44444444, 0x170);

	val = hisi_pcie_ctrl_readl(pcie, 0x2d0);
	val &= (~0x3f);
	val |= 0x5;
	hisi_pcie_ctrl_writel(pcie, val, 0x2d0);
}

static void hisi_pcie_mode_set_hip06(struct hisi_pcie *pcie)
{
	u32 val;

	hisi_pcie_ctrl_writel(pcie, 0x4 << 28, PCIE_CORE_MODE_REG);

	/*
	 * bypass SMMU. SMMU only allocates memory for stream ID 0, however,
	 * stream ID sent to SMMU by PCIe controller is BDF of PCIe device,
	 * which will bring error.
	 */
	val = hisi_pcie_ctrl_readl(pcie, PCIE_SYS_CTRL_13_REG);
	val |= PCIE_MST_BYPASS_SMMU_EN | PCIE_MST_AW_BYPASS_SMMU_EN |
	    PCIE_MST_AR_BYPASS_SMMU_EN;
	hisi_pcie_ctrl_writel(pcie, val, PCIE_SYS_CTRL_13_REG);
}

static void hisi_pcie_portnum_set_hip06(struct hisi_pcie *pcie, u32 num)
{
	u32 val;

	val = hisi_pcie_ctrl_readl(pcie, PCIE_CTRL_19_REG);
	val &= ~(0xff);
	val |= num;
	hisi_pcie_ctrl_writel(pcie, val, PCIE_CTRL_19_REG);
}

static void hisi_pcie_lane_reversal_set_hip06(struct hisi_pcie *pcie)
{
	u32 val;

	val = hisi_pcie_apb_readl(pcie, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= HIP06_AUTO_LANE_FLIP_CTRL_EN;
	hisi_pcie_apb_writel(pcie, val, PCIE_LINK_WIDTH_SPEED_CONTROL);
}

static struct pcie_soc_ops hip06_ops = {
	&hisi_pcie_link_up_hip06,
	&hisi_pcie_config_context_hip06,
	&hisi_pcie_enable_ltssm_hip06,
	NULL,
	&hisi_pcie_init_pcs_hip06,
	&pcie_equalization_hip06,
	&hisi_pcie_mode_set_hip06,
	&hisi_pcie_portnum_set_hip06,
	&hisi_pcie_lane_reversal_set_hip06,
	NULL,
	HIP06_PCIE_CORE_RESET,
	HIP06_PCIE_CLK_CTRL,
	HIP06_PCS_SERDES_STATUS,
	PCIE_HOST_HIP06
};

static const struct of_device_id hisi_pcie_of_match_hip06[] = {
	{
	 .compatible = "hisilicon,hip06-pcie",
	 .data = (void *)&hip06_ops,
	 },
	{},
};

MODULE_DEVICE_TABLE(of, hisi_pcie_of_match_hip06);

static struct platform_driver hisi_pcie_driver_hip06 = {
	.driver = {
		   .name = "hisi-pcie-hip06",
		   .owner = THIS_MODULE,
		   .of_match_table = hisi_pcie_of_match_hip06,
		   },
};

static int __init hisi_pcie_init_hip06(void)
{
	return platform_driver_probe(&hisi_pcie_driver_hip06, hisi_pcie_probe);
}

subsys_initcall(hisi_pcie_init_hip06);

MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_AUTHOR("Dacai Zhu <zhudacai@hisilicon.com>");
MODULE_AUTHOR("Gabriele Paoloni <gabriele.paoloni@huawei.com>");
MODULE_LICENSE("GPL v2");
