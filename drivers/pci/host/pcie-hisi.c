/*
 * PCIe host controllers common functions for HiSilicon SoCs drivers
 *
 * Copyright (C) 2015 HiSilicon Co., Ltd. http://www.hisilicon.com
 *
 * Authors: Zhou Wang <wangzhou1@hisilicon.com>
 *          Dacai Zhu <zhudacai@hisilicon.com>
 *          Gabriele Paoloni <gabriele.paoloni@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include "pcie-designware.h"
#include "pcie-hisi.h"

/*
 * Change mode to indicate the same reg_base to base of PCIe host configure
 * registers, base of RC configure space or base of vmid/asid context table
 */
void hisi_pcie_change_apb_mode(struct hisi_pcie *pcie, u32 mode)
{
	u32 val;
	u32 bit_mask;
	u32 bit_shift;
	u32 port_id = pcie->port_id;
	u32 reg = PCIE_SUBCTRL_MODE_REG + 0x100 * port_id;

	if ((port_id == 1) || (port_id == 2)) {
		bit_mask = 0xc;
		bit_shift = 0x2;
	} else {
		bit_mask = 0x6;
		bit_shift = 0x1;
	}

	val = hisi_pcie_subctrl_readl(pcie, reg);
	val = (val & (~bit_mask)) | (mode << bit_shift);
	hisi_pcie_subctrl_writel(pcie, val, reg);
}

static void hisi_pcie_core_reset_ctrl(struct hisi_pcie *pcie, bool reset_on)
{
	u32 reg_reset_ctrl;
	u32 reg_dereset_ctrl;
	u32 port_id = pcie->port_id;
	u32 pcie_pcs_core_reset = pcie->soc_ops->pcie_pcs_core_reset;

	if (port_id == 3) {
		reg_reset_ctrl = PCIE3_SUBCTRL_RESET_REQ_REG;
		reg_dereset_ctrl = PCIE3_SUBCTRL_DRESET_REQ_REG;
	} else {
		reg_reset_ctrl = PCIE0_2_SUBCTRL_RESET_REQ_REG(port_id);
		reg_dereset_ctrl = PCIE0_2_SUBCTRL_DRESET_REQ_REG(port_id);
	}

	if (reset_on) {
		/* if core is link up, stop the ltssm state machine first */
		if (pcie->soc_ops->hisi_pcie_link_up(pcie))
			pcie->soc_ops->hisi_pcie_enable_ltssm(pcie, false);

		hisi_pcie_subctrl_writel(pcie, pcie_pcs_core_reset,
					 reg_reset_ctrl);
	} else
		hisi_pcie_subctrl_writel(pcie, pcie_pcs_core_reset,
					 reg_dereset_ctrl);

}

static void hisi_pcie_clock_ctrl(struct hisi_pcie *pcie, bool clock_on)
{
	u32 reg_clock_disable;
	u32 reg_clock_enable;
	u32 reg_clock_status;
	u32 clock_status;
	u32 clock_status_checked;
	unsigned long timeout;
	u32 port_id = pcie->port_id;
	u32 pcie_clk_ctrl = pcie->soc_ops->pcie_clk_ctrl;

	if (port_id == 3) {
		reg_clock_disable = PCIE_SUBCTRL_SC_PCIE3_CLK_DIS_REG;
		reg_clock_enable = PCIE_SUBCTRL_SC_PCIE3_CLK_EN_REG;
		reg_clock_status = PCIE_SUBCTRL_SC_PCIE3_CLK_ST_REG;
	} else {
		reg_clock_disable =
		    PCIE_SUBCTRL_SC_PCIE0_2_CLK_DIS_REG(port_id);
		reg_clock_enable = PCIE_SUBCTRL_SC_PCIE0_2_CLK_EN_REG(port_id);
		reg_clock_status = PCIE_SUBCTRL_SC_PCIE0_2_CLK_ST_REG(port_id);
	}

	if (clock_on)
		hisi_pcie_subctrl_writel(pcie, pcie_clk_ctrl, reg_clock_enable);
	else
		hisi_pcie_subctrl_writel(pcie, pcie_clk_ctrl,
					 reg_clock_disable);
	timeout = jiffies + HZ * 1;
	do {
		clock_status = hisi_pcie_subctrl_readl(pcie, reg_clock_status);
		if (clock_on)
			clock_status_checked =
			    ((clock_status & pcie_clk_ctrl) != pcie_clk_ctrl);
		else
			clock_status_checked =
			    ((clock_status & pcie_clk_ctrl) != 0);
	} while ((clock_status_checked) && (time_before(jiffies, timeout)));

	/* get a timeout error */
	if (clock_status_checked)
		dev_err(pcie->pp.dev, "clock operation failed!\n");
}

static void hisi_pcie_assert_core_reset(struct hisi_pcie *pcie)
{
	hisi_pcie_core_reset_ctrl(pcie, PCIE_ASSERT_RESET_ON);
	hisi_pcie_clock_ctrl(pcie, PCIE_CLOCK_OFF);
}

static void hisi_pcie_deassert_core_reset(struct hisi_pcie *pcie)
{
	hisi_pcie_core_reset_ctrl(pcie, PCIE_DEASSERT_RESET_ON);
}

static void hisi_pcie_deassert_pcs_reset(struct hisi_pcie *pcie)
{
	u32 val;
	u32 hilink_reset_status;
	u32 pcs_local_status;
	u32 hilink_status_checked;
	u32 pcs_local_status_checked;
	unsigned long timeout;
	u32 port_id = pcie->port_id;

	val = 1 << port_id;
	hisi_pcie_subctrl_writel(pcie, val, PCIE_PCS_LOCAL_DRESET_REQ);

	if (pcie->soc_ops->soc_type == PCIE_HOST_HIP06)
		hisi_pcie_subctrl_writel(pcie, val, PCIE_PCS_APB_DRESET_REQ);

	val = 0xff << (port_id << 3);
	hisi_pcie_subctrl_writel(pcie, val, PCIE_PCS_DRESET_REQ_REG);

	timeout = jiffies + HZ * 1;
	/* read reset status, make sure pcs is deassert */
	do {
		pcs_local_status = hisi_pcie_subctrl_readl(pcie,
						PCIE_PCS_LOCAL_RESET_ST);
		pcs_local_status_checked = (pcs_local_status & (1 << port_id));
	} while ((pcs_local_status_checked) && (time_before(jiffies, timeout)));

	/* get a timeout error */
	if (pcs_local_status_checked)
		dev_err(pcie->pp.dev, "pcs deassert reset failed!\n");

	timeout = jiffies + HZ * 1;

	do {
		hilink_reset_status = hisi_pcie_subctrl_readl(pcie,
						PCIE_PCS_RESET_REG_ST);
		hilink_status_checked = (hilink_reset_status &
					 (0xff << (port_id << 3)));
	} while ((hilink_status_checked) && (time_before(jiffies, timeout)));

	if (hilink_status_checked)
		dev_err(pcie->pp.dev, "pcs deassert reset failed!\n");
}

static void hisi_pcie_assert_pcs_reset(struct hisi_pcie *pcie)
{
	u32 reg;
	u32 hilink_reset_status;
	u32 pcs_local_reset_status;
	u32 hilink_status_checked;
	u32 pcs_local_status_checked;
	unsigned long timeout;
	u32 port_id = pcie->port_id;

	reg = 1 << port_id;
	hisi_pcie_subctrl_writel(pcie, reg, PCIE_PCS_LOCAL_RESET_REQ);

	if (pcie->soc_ops->soc_type == PCIE_HOST_HIP06)
		hisi_pcie_subctrl_writel(pcie, reg, PCIE_PCS_APB_RESET_REQ);

	reg = 0xff << (port_id << 3);
	hisi_pcie_subctrl_writel(pcie, reg, PCIE_PCS_RESET_REQ_REG);

	timeout = jiffies + HZ * 1;
	/* read reset status, make sure pcs is reset */
	do {
		pcs_local_reset_status = hisi_pcie_subctrl_readl(pcie,
						PCIE_PCS_LOCAL_RESET_ST);
		pcs_local_status_checked =
		    ((pcs_local_reset_status & (1 << port_id)) !=
		     (1 << port_id));

	} while ((pcs_local_status_checked) && (time_before(jiffies, timeout)));

	if (pcs_local_status_checked)
		dev_err(pcie->pp.dev, "pcs local reset status read failed\n");

	timeout = jiffies + HZ * 1;
	do {
		hilink_reset_status = hisi_pcie_subctrl_readl(pcie,
						PCIE_PCS_RESET_REG_ST);
		hilink_status_checked =
		    ((hilink_reset_status & (0xff << (port_id << 3))) !=
		     (0xff << (port_id << 3)));
	} while ((hilink_status_checked) && (time_before(jiffies, timeout)));

	if (hilink_status_checked)
		dev_err(pcie->pp.dev, "error:pcs assert reset failed\n");
}

void pcie_equalization_common(struct hisi_pcie *pcie)
{
	hisi_pcie_apb_writel(pcie, 0xfd7, 0x894);

	hisi_pcie_apb_writel(pcie, 0x0, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xfc00, 0x898);
	hisi_pcie_apb_writel(pcie, 0x1, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xbd00, 0x898);
	hisi_pcie_apb_writel(pcie, 0x2, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xccc0, 0x898);
	hisi_pcie_apb_writel(pcie, 0x3, 0x89c);
	hisi_pcie_apb_writel(pcie, 0x8dc0, 0x898);
	hisi_pcie_apb_writel(pcie, 0x4, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xfc0, 0x898);
	hisi_pcie_apb_writel(pcie, 0x5, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xe46, 0x898);
	hisi_pcie_apb_writel(pcie, 0x6, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xdc8, 0x898);
	hisi_pcie_apb_writel(pcie, 0x7, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xcb46, 0x898);
	hisi_pcie_apb_writel(pcie, 0x8, 0x89c);
	hisi_pcie_apb_writel(pcie, 0x8c07, 0x898);
	hisi_pcie_apb_writel(pcie, 0x9, 0x89c);
	hisi_pcie_apb_writel(pcie, 0xd0b, 0x898);
	hisi_pcie_apb_writel(pcie, 0x103ff21, 0x8a8);
}

static void hisi_pcie_spd_set(struct hisi_pcie *pcie, u32 spd)
{
	u32 val;

	val = hisi_pcie_apb_readl(pcie, 0xa0);
	val &= ~(0xf);
	val |= spd;
	hisi_pcie_apb_writel(pcie, val, 0xa0);
}

static void hisi_pcie_spd_control(struct hisi_pcie *pcie)
{
	u32 val;

	/* set link width speed control register */
	val = hisi_pcie_apb_readl(pcie, PCIE_LINK_WIDTH_SPEED_CONTROL);
	/*
	 * set the Directed Speed Change field of the Link Width and Speed
	 * Change Control register
	 */
	val |= PORT_LOGIC_SPEED_CHANGE;
	hisi_pcie_apb_writel(pcie, val, PCIE_LINK_WIDTH_SPEED_CONTROL);
}

/* set smmu relax order mode to improve performance */
static void hisi_pcie_ro_set(struct hisi_pcie *pcie, u32 enable)
{
	u32 val;

	if (pcie->soc_ops->soc_type == PCIE_HOST_HIP06) {
		val = hisi_pcie_ctrl_readl(pcie, 0x110);
		val |= BIT(13);
		if (enable)
			val |= BIT(12);
		else
			val &= ~BIT(12);
		hisi_pcie_ctrl_writel(pcie, val, 0x110);
	}
}

void hisi_pcie_establish_link(struct hisi_pcie *pcie)
{
	u32 val = 0;
	int count = 0;

	if (dw_pcie_link_up(&pcie->pp)) {
		/* set smmu relax order mode to improve performance */
		hisi_pcie_ro_set(pcie, pcie->smmu_ro);

		dev_info(pcie->pp.dev, "already Link up\n");
		return;
	}
	/* assert reset signals */
	hisi_pcie_assert_core_reset(pcie);
	hisi_pcie_assert_pcs_reset(pcie);

	if (pcie->soc_ops->soc_type == PCIE_HOST_HIP05) {
		/* de-assert phy reset */
		hisi_pcie_deassert_pcs_reset(pcie);
		/* de-assert core reset */
		hisi_pcie_deassert_core_reset(pcie);
	} else if (pcie->soc_ops->soc_type == PCIE_HOST_HIP06) {
		/*
		 * When de-assert reset, there is several cycle
		 * between set the register and the de-assert be
		 * effective. APB access is forbindden in this
		 * window. Delay 10ms to protect the M3 from APB
		 * access(M3 should check the reset status before
		 * APB access).
		 */
		mdelay(10);

		/* de-assert core reset */
		hisi_pcie_deassert_core_reset(pcie);
		/* de-assert phy reset */
		hisi_pcie_deassert_pcs_reset(pcie);
	}

	/* enable clock */
	hisi_pcie_clock_ctrl(pcie, PCIE_CLOCK_ON);

	/*
	 * if the serdes is not locked, reading regs of pcie
	 * will die, so we check it firstly
	 */
	do {
		u32 pcs_serdes_status = pcie->soc_ops->pcs_serdes_status;

		val = hisi_pcie_pcs_readl(pcie, pcs_serdes_status);
		if (count == 10) {
			dev_info(pcie->pp.dev,
				 "PCIe Failed! PLL Locked: 0x%x\n", val);
			return;
		}
		count++;
		msleep(10);
	} while ((val & PCIE_PCS_LOCKED_MASK) == 0);
	count = 0;

	/* initialize phy */
	pcie->soc_ops->hisi_pcie_init_pcs(pcie);

	/* set controller to RC mode */
	pcie->soc_ops->hisi_pcie_mode_set(pcie);

	/* set target link speed */
	hisi_pcie_spd_set(pcie, 3);

	/* set pcie port num 0 for all controller, must be set 0 here.
	 *
	 * there is a bug about ITS. ITS uses request_id(BDF) + MSI_vector to
	 * establish ITS table for PCIe devices. However, PCIe host send
	 * port_id + request_id + MSI_vector to ITS TRANSLATION register
	 */
	if (pcie->soc_ops->hisi_pcie_portnum_set)
		pcie->soc_ops->hisi_pcie_portnum_set(pcie, 0);

	/* Enable PCIe lane revesal to fix potential broken linkup for
	 * some EP devices.
	 */
	if (pcie->soc_ops->hisi_pcie_lane_reversal_set)
		pcie->soc_ops->hisi_pcie_lane_reversal_set(pcie);

	/* set link speed control */
	hisi_pcie_spd_control(pcie);

	/* setup root complex */
	dw_pcie_setup_rc(&pcie->pp);

	pcie->soc_ops->pcie_equalization(pcie);

	/* set smmu relax order mode to improve performance */
	hisi_pcie_ro_set(pcie, pcie->smmu_ro);

	/* assert LTSSM enable */
	pcie->soc_ops->hisi_pcie_enable_ltssm(pcie, true);

	if (pcie->soc_ops->hisi_pcie_gen3_config)
		pcie->soc_ops->hisi_pcie_gen3_config(pcie);

	/* check if the link is up or not */
	while (!dw_pcie_link_up(&pcie->pp)) {
		u32 pcs_serdes_status = pcie->soc_ops->pcs_serdes_status;

		mdelay(100);
		count++;
		if (count == 10) {
			val = hisi_pcie_pcs_readl(pcie, pcs_serdes_status);

			dev_info(pcie->pp.dev,
				 "PCIe Link Failed! PLL Locked: 0x%x\n", val);
			return;
		}
	}

	dev_info(pcie->pp.dev, "Link up\n");

	/* dfe enable is just for 660 */
	if (pcie->soc_ops->hisi_pcie_gen3_dfe_enable)
		pcie->soc_ops->hisi_pcie_gen3_dfe_enable(pcie);
	/*
	 * add a 1s delay between linkup and enumeration, make sure
	 * the EP device's configuration registers are prepared well
	 */
	mdelay(1000);
}

static void hisi_pcie_set_db2_enable(struct hisi_pcie *pcie, u32 enable)
{
	u32 dbi_ctrl;

	hisi_pcie_change_apb_mode(pcie, PCIE_SLV_SYSCTRL_MODE);

	dbi_ctrl = hisi_pcie_apb_readl(pcie, PCIE_SYS_CTRL20_REG);
	if (enable)
		dbi_ctrl |= PCIE_DB2_ENABLE_SHIFT;
	else
		dbi_ctrl &= ~PCIE_DB2_ENABLE_SHIFT;
	hisi_pcie_apb_writel(pcie, dbi_ctrl, PCIE_SYS_CTRL20_REG);

	hisi_pcie_change_apb_mode(pcie, PCIE_SLV_DBI_MODE);
}

static void hisi_pcie_disabled_bar0(struct hisi_pcie *pcie)
{
	hisi_pcie_set_db2_enable(pcie, PCIE_DBI_CS2_ENABLE);
	hisi_pcie_apb_writel(pcie, 0, PCIE_CFG_BAR0BASE);
	hisi_pcie_set_db2_enable(pcie, PCIE_DBI_CS2_DISABLE);
}

static int hisi_pcie_msi_host_init(struct pcie_port *pp,
				   struct msi_controller *chip)
{
	struct device_node *msi_node;
	struct irq_domain *irq_domain;
	struct resource res;
	struct device_node *np = pp->dev->of_node;
	struct hisi_pcie *pcie = to_hisi_pcie(pp);

	msi_node = of_parse_phandle(np, "msi-parent", 0);
	if (!msi_node) {
		dev_err(pp->dev, "failed to find msi-parent\n");
		return -ENODEV;
	}

	if (of_address_to_resource(msi_node, 0, &res)) {
		dev_err(pp->dev, "failed to find resource!\n");
		return -ENODEV;
	}

	pcie->msi_addr = res.start + PCIE_GITS_TRANSLATER;

	irq_domain = irq_find_host(msi_node);
	if (!irq_domain) {
		dev_err(pp->dev, "failed to find irq domain\n");
		return -ENODEV;
	}

	pp->irq_domain = irq_domain;

	return 0;
}

static inline int hisi_rd_ecam_conf(struct pcie_port *pp, struct pci_bus *bus,
		unsigned int devfn, int where, int size, u32 *value)
{
	return pci_generic_config_read(bus, devfn, where, size, value);
}

static inline int hisi_wr_ecam_conf(struct pcie_port *pp, struct pci_bus *bus,
		unsigned int devfn, int where, int size, u32 value)
{
	return pci_generic_config_write(bus, devfn, where, size, value);
}

static int hisi_pcie_cfg_write(struct pcie_port *pp, int where, int size,
			       u32 val)
{
	u32 reg_val;
	u32 reg;
	struct hisi_pcie *pcie = to_hisi_pcie(pp);
	void *walker = &reg_val;

	walker += (where & 0x3);
	reg = where & ~0x3;
	if (size == 4)
		hisi_pcie_apb_writel(pcie, val, reg);
	else if (size == 2) {
		reg_val = hisi_pcie_apb_readl(pcie, reg);
		*(u16 __force *)walker = val;
		hisi_pcie_apb_writel(pcie, reg_val, reg);
	} else if (size == 1) {
		reg_val = hisi_pcie_apb_readl(pcie, reg);
		*(u8 __force *)walker = val;
		hisi_pcie_apb_writel(pcie, reg_val, reg);
	} else
		return PCIBIOS_BAD_REGISTER_NUMBER;
	/*
	 * This is a quirk. For some PCIe controller(eg, hip06), we need to
	 * operate some private regs to clear aer interrupt. The AER driver
	 * didn't provide interface to do this, we do this after the AER dirver
	 * clear aer interrupt.
	 */
	if (pcie->soc_ops->hisi_pcie_clr_extra_aer)
		pcie->soc_ops->hisi_pcie_clr_extra_aer(pcie, where, size);

	return PCIBIOS_SUCCESSFUL;
}

static void hisi_pcie_scan_bus(struct pcie_port *pp)
{
	struct hisi_pcie *hisi_pcie = to_hisi_pcie(pp);
	struct pci_bus *b = NULL;
	struct pci_dev *dev;

	while ((b = pci_find_next_bus(b)) != NULL)
		if (pp == b->sysdata)
			break;
	if (!b)
		return;

	dev = pci_get_slot(b, 0);
	if (!dev)
		return;

	hisi_pcie->soc_ops->aer_cap =
		pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	pci_dev_put(dev);
}

int hisi_pcie_link_up(struct pcie_port *pp)
{
	struct hisi_pcie *hisi_pcie = to_hisi_pcie(pp);

	return hisi_pcie->soc_ops->hisi_pcie_link_up(hisi_pcie);
}

static int hisi_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 *val)
{
	u32 reg;
	int ret;
	struct hisi_pcie *hisi_pcie = to_hisi_pcie(pp);

	ret = dw_pcie_rd_other_conf(pp, bus, devfn, where, size, val);

	if (ret)
		return ret;

	/*
	 * For the PCIe spec, in Configuration Request Retry Status(CRS),
	 * the synthetic Vendor_ID(0x1) is required. But in PCIe controller
	 * of hip06, the synthetic Vendor_ID is returned as 0x0
	 * or 0xffffffff(with Device_ID). This implementation makes the
	 * CRS response invalid.
	 * The synthetic Vendor_ID is modified to 0x1 to conform the PCIe
	 * spec.
	 */
	if (hisi_pcie->soc_ops->soc_type == PCIE_HOST_HIP06) {
		if ((where == PCI_VENDOR_ID) && (size == 4)) {
			reg = hisi_pcie_ctrl_readl(hisi_pcie, PCIE_SYS_STATE5);
			/* Bit[26:24] */
			reg = (reg >> 24) & 0x7;

			if (*val == 0) {
				/* check CRS state */
				if (reg == 0x2) {
					*val = 0x1;
					/* Clear read timeout status in CRS */
					hisi_pcie_ctrl_writel(hisi_pcie,
						BIT(24), PCIE_CTRL_11_REG);
				}
			}
		}
	}

	return ret;
}

static void hisi_pcie_host_init(struct pcie_port *pp)
{
	struct hisi_pcie *pcie = to_hisi_pcie(pp);

	hisi_pcie_establish_link(pcie);

	/*
	 * Some register configurations are moved from dw_pcie_host_init()
	 * to dw_pcie_setup_rc(). If the PCIe link is establised by BIOS,
	 * the dw_pcie_setup_rc() won't be called in hisi_pcie_establish_link(),
	 * add dw_pcie_setup_rc() to be called to ensure the registers are
	 * set as expected.
	 * The speed-change bit need not to be set after linkup.
	 */
	dw_pcie_setup_rc_lane_num(pp);
	dw_pcie_setup_rc_base(pp);

	if (!pp->ops->rd_other_conf)
		pp->ops->rd_other_conf = hisi_pcie_rd_other_conf;

	pcie->soc_ops->hisi_pcie_config_context(pcie);
	/*
	 * The default size of BAR0 in p660 host bridge is 0x10000000,
	 * which will bring problem when most resource has been allocated
	 * to BAR0 in host bridge.However, we need not use BAR0 in host bridge
	 * in RC mode. Here we just disable it
	 */
	hisi_pcie_disabled_bar0(pcie);

	/*
	 * We need to initialize some private regs to enable aer interrupt.
	 */
	if (pcie->soc_ops->hisi_pcie_enable_aer)
		pcie->soc_ops->hisi_pcie_enable_aer(pcie);
}

static void __iomem *hisi_pci_map_cfg_bus_cam(struct pci_bus *bus,
					      unsigned int devfn,
					      int where)
{
	void __iomem *addr;
	struct pcie_port *pp = bus->sysdata;

	addr = pp->va_cfg1_base - (pp->busn->start << 20) +
			((bus->number << 20) | (devfn << 12)) +
			where;

	return addr;
}

static int hisi_add_pcie_port(struct pcie_port *pp,
			      struct platform_device *pdev)
{
	int ret;
	u32 port_id;
	struct hisi_pcie *hisi_pcie = to_hisi_pcie(pp);

	if (of_property_read_u32(pdev->dev.of_node, "port-id", &port_id)) {
		dev_err(&pdev->dev, "failed to read port-id\n");
		return -EINVAL;
	}
	if (port_id > 3) {
		dev_err(&pdev->dev, "Invalid port-id: %d\n", port_id);
		return -EINVAL;
	}
	hisi_pcie->port_id = port_id;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize host\n");
		return ret;
	}

	if (hisi_pcie->soc_ops->populate_ctrl_sysfs)
		hisi_pcie->soc_ops->populate_ctrl_sysfs(hisi_pcie);

	if (hisi_pcie->soc_ops->create_err_thread)
		hisi_pcie->soc_ops->create_err_thread(hisi_pcie);

	return 0;
}

int hisi_pcie_probe(struct platform_device *pdev)
{
	struct hisi_pcie *hisi_pcie;
	struct pcie_port *pp;
	const struct of_device_id *match;
	struct resource *reg;
	struct resource *subctrl;
	struct resource *phy;
	struct device_driver *driver;
	int ret;

	hisi_pcie = devm_kzalloc(&pdev->dev, sizeof(*hisi_pcie), GFP_KERNEL);
	if (!hisi_pcie)
		return -ENOMEM;

	pp = &hisi_pcie->pp;
	pp->dev = &pdev->dev;
	driver = (pdev->dev).driver;

	match = of_match_device(driver->of_match_table, &pdev->dev);
	hisi_pcie->soc_ops = (struct pcie_soc_ops *)match->data;

	/*
	 * Change the ops functions from static definition to dynamic
	 * definition, because the rd_other_conf pointer should be
	 * defined after MEM type ATU be programmed, otherwise the ATU
	 * could not be programmed.
	 */
	pp->ops = devm_kzalloc(&pdev->dev, sizeof(struct pcie_host_ops),
						   GFP_KERNEL);
	if (!pp->ops)
		return -ENOMEM;

	pp->ops->host_init = hisi_pcie_host_init;
	pp->ops->link_up = hisi_pcie_link_up;
	pp->ops->msi_host_init = hisi_pcie_msi_host_init;
	pp->ops->wr_own_conf = hisi_pcie_cfg_write;
	pp->ops->scan_bus = hisi_pcie_scan_bus;

	subctrl = platform_get_resource_byname(pdev, IORESOURCE_MEM, "subctrl");
	if (!subctrl) {
		dev_err(pp->dev, "miss subctrl base\n");
		return -EINVAL;
	}
	hisi_pcie->subctrl_base = devm_ioremap_nocache(&pdev->dev,
						       subctrl->start,
						       resource_size(subctrl));
	if (!hisi_pcie->subctrl_base) {
		dev_err(pp->dev, "cannot get subctrl base\n");
		return -ENOMEM;
	}

	reg = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rc_dbi");
	hisi_pcie->reg_base = devm_ioremap_resource(&pdev->dev, reg);
	if (IS_ERR(hisi_pcie->reg_base)) {
		dev_err(pp->dev, "cannot get reg base\n");
		return PTR_ERR(hisi_pcie->reg_base);
	}

	hisi_pcie->pp.dbi_base = hisi_pcie->reg_base;

	reg = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ecam-cfg");
	if (reg) {
		/* ECAM driver version */
		hisi_pcie->pp.va_cfg0_base =
				devm_ioremap_resource(&pdev->dev, reg);
		if (IS_ERR(hisi_pcie->pp.va_cfg0_base)) {
			dev_err(pp->dev, "cannot get ecam-cfg\n");
			return PTR_ERR(hisi_pcie->pp.va_cfg0_base);
		}
		hisi_pcie->pp.va_cfg1_base = hisi_pcie->pp.va_cfg0_base;

		dw_pcie_ops.map_bus = hisi_pci_map_cfg_bus_cam;

		pp->ops->rd_other_conf = hisi_rd_ecam_conf;
		pp->ops->wr_other_conf = hisi_wr_ecam_conf;
	}

	phy = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcs");
	hisi_pcie->phy_base = devm_ioremap_resource(&pdev->dev, phy);
	if (IS_ERR(hisi_pcie->phy_base)) {
		dev_err(pp->dev, "cannot get phy base\n");
		return PTR_ERR(hisi_pcie->phy_base);
	}

	if (hisi_pcie->soc_ops->soc_type == PCIE_HOST_HIP05) {
		struct resource *serdes;

		serdes =
		    platform_get_resource_byname(pdev, IORESOURCE_MEM,
						 "serdes");
		hisi_pcie->serdes_base =
		    devm_ioremap_resource(&pdev->dev, serdes);
		if (IS_ERR(hisi_pcie->serdes_base)) {
			dev_err(pp->dev, "cannot get serdes base\n");
			return PTR_ERR(hisi_pcie->serdes_base);
		}
		hisi_pcie->ctrl_base = NULL;
	} else if (hisi_pcie->soc_ops->soc_type == PCIE_HOST_HIP06) {
		hisi_pcie->ctrl_base = hisi_pcie->reg_base + 0x1000;
		ret = of_property_read_u32(pdev->dev.of_node, "smmu_ro",
					   &hisi_pcie->smmu_ro);
		if (ret)
			hisi_pcie->smmu_ro = 0;
	} else
		dev_err(pp->dev, "unsopported soc type\n");

	ret = hisi_add_pcie_port(pp, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, hisi_pcie);

	return 0;
}
