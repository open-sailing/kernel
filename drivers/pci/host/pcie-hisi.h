/*
 * PCIe host controller driver for Hisilicon Hip05 and Hip06 SoCs
 *
 * Copyright (C) 2015 Hisilicon Co., Ltd. http://www.hisilicon.com
 *
 * Authors: Zhou Wang <wangzhou1@hisilicon.com>
 *          Gabriele Paoloni <gabriele.paoloni@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _PCIE_HISI_H
#define _PCIE_HISI_H

#define PCIE_HOST_HIP05                                 0x660
#define PCIE_HOST_HIP06                                 0x1610

#define PCIE_SUBCTRL_MODE_REG                           0x2800
#define PCIE_SUBCTRL_SYS_STATE4_REG                     0x6818
#define PCIE_SLV_DBI_MODE                               0x0
#define PCIE_SLV_SYSCTRL_MODE                           0x1
#define PCIE_SLV_CONTENT_MODE                           0x2
#define PCIE_SLV_MSI_ASID                               0x10
#define PCIE_LTSSM_RCVRY_LOCK_STATE                     0x0d
#define PCIE_LTSSM_RCVRY_IDLE_STATE                     0x10
#define PCIE_LTSSM_RCVRY_EQ0_STATE                      0x20
#define PCIE_LTSSM_RCVRY_EQ3_STATE                      0x23
#define PCIE_LTSSM_LINKUP_STATE                         0x11
#define PCIE_LTSSM_STATE_MASK                           0x3F
#define PCIE_MSI_ASID_ENABLE                            (0x1 << 12)
#define PCIE_MSI_ASID_VALUE                             (0x1 << 16)
#define PCIE_MSI_TRANS_ENABLE                           (0x1 << 12)
#define PCIE_MSI_TRANS_REG                              0x1c8
#define PCIE_MSI_LOW_ADDRESS                            0x1b4
#define PCIE_MSI_HIGH_ADDRESS                           0x1c4
#define PCIE_GITS_TRANSLATER                            0x10040

#define PCIE_SYS_CTRL20_REG                             0x20
#define PCIE_RD_TAB_SEL                                 BIT(31)
#define PCIE_RD_TAB_EN                                  BIT(30)
#define PCIE_CFG_BAR0BASE                               0x10
#define PCIE_DB2_ENABLE_SHIFT                           BIT(0)
#define PCIE_DBI_CS2_ENABLE                             0x1
#define PCIE_DBI_CS2_DISABLE                            0x0

#define PCIE_CTRL_7_REG                                 0x114
#define PCIE_LTSSM_ENABLE_SHIFT                         BIT(11)
#define PCIE_SUBCTRL_RESET_REQ_REG                      0xA00
#define PCIE0_2_SUBCTRL_RESET_REQ_REG(port_id) \
	(PCIE_SUBCTRL_RESET_REQ_REG + (port_id << 3))
#define PCIE3_SUBCTRL_RESET_REQ_REG                     0xA68

#define PCIE_SUBCTRL_DRESET_REQ_REG                     0xA04
#define PCIE0_2_SUBCTRL_DRESET_REQ_REG(port_id) \
	(PCIE_SUBCTRL_DRESET_REQ_REG + (port_id << 3))
#define PCIE3_SUBCTRL_DRESET_REQ_REG                    0xA6C

#define PCIE_SUBCTRL_SC_PCIE0_CLK_DIS_REG               0x304
#define PCIE_SUBCTRL_SC_PCIE0_2_CLK_DIS_REG(port_id) \
	(PCIE_SUBCTRL_SC_PCIE0_CLK_DIS_REG + (port_id << 3))
#define PCIE_SUBCTRL_SC_PCIE3_CLK_DIS_REG               0x324

#define PCIE_SUBCTRL_SC_PCIE0_CLK_ST_REG                0x5300
#define PCIE_SUBCTRL_SC_PCIE0_2_CLK_ST_REG(port_id) \
	(PCIE_SUBCTRL_SC_PCIE0_CLK_ST_REG + (port_id << 2))
#define PCIE_SUBCTRL_SC_PCIE3_CLK_ST_REG                0x5310

#define PCIE_SUBCTRL_SC_PCIE0_CLK_EN_REG                0x300
#define PCIE_SUBCTRL_SC_PCIE0_2_CLK_EN_REG(port_id) \
	(PCIE_SUBCTRL_SC_PCIE0_CLK_EN_REG + (port_id << 3))
#define PCIE_SUBCTRL_SC_PCIE3_CLK_EN_REG                0x320

#define PCIE_ASSERT_RESET_ON                            true
#define PCIE_DEASSERT_RESET_ON                          false
#define PCIE_CLOCK_ON                                   true
#define PCIE_CLOCK_OFF                                  false

#define PCIE_PCS_LOCAL_RESET_REQ                        0xAC0
#define PCIE_PCS_LOCAL_DRESET_REQ                       0xAC4
#define PCIE_PCS_RESET_REQ_REG                          0xA60
#define PCIE_PCS_RESET_REG_ST                           0x5A30
#define PCIE_PCS_LOCAL_DRESET_ST                        0x5A60
#define PCIE_PCS_LOCAL_RESET_ST                         0x5A60
#define PCIE_PCS_DRESET_REQ_REG                         0xA64
#define PCIE_M_PCS_IN15_CFG                             0x74
#define PCIE_M_PCS_IN13_CFG                             0x34
#define PCIE_PCS_RXDETECTED                             0xE4
#define PCIE_CORE_MODE_REG                              0xF8
#define PCIE_LINK_WIDTH_SPEED_CONTROL                   0x80C
#define PORT_LOGIC_SPEED_CHANGE                         (0x1 << 17)

#define DS_API(lane) ((0x1FF6c + 8 * (15 - lane)) * 2)
#define PCIE_DFE_ENABLE_VAL                             0x3851

#define PCIE_RD_ERR_RESPONSE_EN                         BIT(31)
#define PCIE_WR_ERR_RESPONSE_EN                         BIT(30)
#define PCIE_PCS_APB_RESET_REQ                          0xAC8
#define PCIE_PCS_APB_DRESET_REQ                         0xACC
#define PCIE_CTRL_19_REG                                0x1c
#define PCIE_SYS_STATE4                                 0x31c

#define PCIE_PCS_LOCKED_MASK                            0x3

#define PCIE_CTRL_11_REG					0x124
#define PCIE_SYS_STATE5						0x30

/* default error polling interval is 1000 ms */
#define DEFAULT_ERR_POLL_INTERVAL				1000

#define HISI_PCIE_PRINT_LIMIT_TIME			(10 * HZ)

enum pcie_mac_phy_rate_e {
	PCIE_GEN1 = 0,		/* PCIE 1.0 */
	PCIE_GEN2 = 1,		/* PCIE 2.0 */
	PCIE_GEN3 = 2		/* PCIE 3.0 */
};

#define to_hisi_pcie(x)	container_of(x, struct hisi_pcie, pp)

struct hisi_pcie;

/*
 * In order to improve the maintainability, we could change the
 * configurations and acquire the state of the PCIe controller
 * by sysfs.
 */
struct pcie_ctrl_attr {
	struct attribute attr;
	ssize_t (*show)(struct hisi_pcie *, char *);
	ssize_t (*store)(struct hisi_pcie *, const char *, size_t);
};

struct pcie_ctrl {
	struct hisi_pcie *pcie;
	struct kobject kobj;
};

#define to_pcie_ctrl_attr(x) container_of(x, struct pcie_ctrl_attr, attr)
#define to_pcie_ctrl(x) container_of(x, struct pcie_ctrl, kobj)

struct pcie_err_poll_info {
	struct task_struct *err_thread;
	u32 interval;
	u32 index;
};

struct pcie_soc_ops {
	int (*hisi_pcie_link_up)(struct hisi_pcie *pcie);
	void (*hisi_pcie_config_context)(struct hisi_pcie *pcie);
	void (*hisi_pcie_enable_ltssm)(struct hisi_pcie *pcie, bool on);
	void (*hisi_pcie_gen3_dfe_enable)(struct hisi_pcie *pcie);
	void (*hisi_pcie_init_pcs)(struct hisi_pcie *pcie);
	void (*pcie_equalization)(struct hisi_pcie *pcie);
	void (*hisi_pcie_mode_set)(struct hisi_pcie *pcie);
	void (*hisi_pcie_portnum_set)(struct hisi_pcie *pcie, u32 num);
	void (*hisi_pcie_lane_reversal_set)(struct hisi_pcie *pcie);
	void (*hisi_pcie_gen3_config)(struct hisi_pcie *pcie);
	void (*hisi_pcie_clr_extra_aer)(struct hisi_pcie *pcie, int where,
					int size);
	void (*hisi_pcie_enable_aer)(struct hisi_pcie *pcie);
	void (*populate_ctrl_sysfs)(struct hisi_pcie *pcie);
	void (*create_err_thread)(struct hisi_pcie *pcie);
	u32 aer_cap;
	u32 pcie_pcs_core_reset;
	u32 pcie_clk_ctrl;
	u32 pcs_serdes_status;
	u32 soc_type;
};

struct hisi_pcie {
	void __iomem *subctrl_base;
	void __iomem *reg_base;
	void __iomem *phy_base;
	void __iomem *serdes_base;
	void __iomem *ctrl_base;
	u32 port_id;
	struct pcie_port pp;
	u64 msi_addr;
	struct pcie_soc_ops *soc_ops;
	u32 smmu_ro;
	struct pcie_err_poll_info err_poll;
};

static inline void hisi_pcie_subctrl_writel(struct hisi_pcie *pcie,
					    u32 val, u32 reg)
{
	writel(val, pcie->subctrl_base + reg);
}

static inline u32 hisi_pcie_subctrl_readl(struct hisi_pcie *pcie, u32 reg)
{
	return readl(pcie->subctrl_base + reg);
}

static inline void hisi_pcie_apb_writel(struct hisi_pcie *pcie,
					u32 val, u32 reg)
{
	writel(val, pcie->reg_base + reg);
}

static inline u32 hisi_pcie_apb_readl(struct hisi_pcie *pcie, u32 reg)
{
	return readl(pcie->reg_base + reg);
}

static inline void hisi_pcie_pcs_writel(struct hisi_pcie *pcie,
					u32 val, u32 reg)
{
	writel(val, pcie->phy_base + reg);
}

static inline u32 hisi_pcie_pcs_readl(struct hisi_pcie *pcie, u32 reg)
{
	return readl(pcie->phy_base + reg);
}

static inline void hisi_pcie_serdes_writel(struct hisi_pcie *pcie,
					   u32 val, u32 reg)
{
	writel(val, pcie->serdes_base + reg);
}

static inline void hisi_pcie_ctrl_writel(struct hisi_pcie *pcie, u32 val,
					 u32 reg)
{
	writel(val, pcie->ctrl_base + reg);
}

static inline u32 hisi_pcie_ctrl_readl(struct hisi_pcie *pcie, u32 reg)
{
	return readl(pcie->ctrl_base + reg);
}

static inline ssize_t pcie_ctrl_attr_show(struct kobject *kobj,
					  struct attribute *attr,
					  char *buf)
{
	struct pcie_ctrl_attr *ctrl_attr = to_pcie_ctrl_attr(attr);
	struct pcie_ctrl *ctrl = to_pcie_ctrl(kobj);
	struct hisi_pcie *pcie = ctrl->pcie;

	return ctrl_attr->show ? ctrl_attr->show(pcie, buf) : -EIO;
}

static inline ssize_t pcie_ctrl_attr_store(struct kobject *kobj,
					   struct attribute *attr,
					   const char *buf, size_t len)
{
	struct pcie_ctrl_attr *ctrl_attr = to_pcie_ctrl_attr(attr);
	struct pcie_ctrl *ctrl = to_pcie_ctrl(kobj);
	struct hisi_pcie *pcie = ctrl->pcie;

	return ctrl_attr->store ? ctrl_attr->store(pcie, buf, len) : -EIO;
}


void hisi_pcie_change_apb_mode(struct hisi_pcie *pcie, u32 mode);
void pcie_equalization_common(struct hisi_pcie *pcie);
int hisi_pcie_probe(struct platform_device *pdev);

#endif /* _PCIE_HISI_H */
