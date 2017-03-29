/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "hns_basic.h"
#include "hns_dsaf_misc.h"
#include "hns_dsaf_mac.h"
#include "hns_dsaf_reg.h"
#include "hns_dsaf_ppe.h"

void hns_cpld_set_led(struct hns_mac_cb *mac_cb, int link_status,
		      u16 speed, int data)
{
	int speed_reg = 0;
	u8 value;

	if (!mac_cb) {
		pr_err("sfp_led_opt mac_dev is null!\n");
		return;
	}
	if (!mac_cb->cpld_vaddr) {
		dev_err(mac_cb->dev, "mac_id=%d, cpld_vaddr is null !\n",
			mac_cb->mac_id);
		return;
	}

	if (speed == MAC_SPEED_10000)
		speed_reg = 1;

	value = mac_cb->cpld_led_value;

	if (link_status) {
		dsaf_set_bit(value, DSAF_LED_LINK_B, link_status);
		dsaf_set_field(value, DSAF_LED_SPEED_M,
			       DSAF_LED_SPEED_S, speed_reg);
		dsaf_set_bit(value, DSAF_LED_DATA_B, data);

		if (value != mac_cb->cpld_led_value) {
			dsaf_write_b(mac_cb->cpld_vaddr, value);
			mac_cb->cpld_led_value = value;
		}
	} else {
		if (AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver)) {
			dsaf_write_b(mac_cb->cpld_vaddr,
				     CPLD_LED_DEFAULT_VALUE);
			mac_cb->cpld_led_value = CPLD_LED_DEFAULT_VALUE;
		} else {
			value = (mac_cb->cpld_led_value) &
				 (0x1 << DSAF_LED_ANCHOR_B);
			dsaf_write_b(mac_cb->cpld_vaddr, value);
			mac_cb->cpld_led_value = value;
		}
	}
}

void cpld_led_reset(struct hns_mac_cb *mac_cb)
{
	if (!mac_cb || !mac_cb->cpld_vaddr)
		return;

	dsaf_write_b(mac_cb->cpld_vaddr, CPLD_LED_DEFAULT_VALUE);
	mac_cb->cpld_led_value = CPLD_LED_DEFAULT_VALUE;
}

int cpld_set_led_id(struct hns_mac_cb *mac_cb,
		    enum hnae_led_state status)
{
	int ret = 0;
	switch (status) {
	case HNAE_LED_ACTIVE:
		if (AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver)) {
			mac_cb->cpld_led_value =
				dsaf_read_b(mac_cb->cpld_vaddr);
			dsaf_set_bit(mac_cb->cpld_led_value,
				     DSAF_LED_ANCHOR_B, CPLD_LED_ON_VALUE);
			dsaf_write_b(mac_cb->cpld_vaddr,
				     mac_cb->cpld_led_value);
			ret = 2;
		} else {
			dsaf_set_bit(mac_cb->cpld_led_value,
				     DSAF_LED_ANCHOR_B, CPLD_LED_ON_VALUE);
			dsaf_write_b(mac_cb->cpld_vaddr,
				     mac_cb->cpld_led_value);
		}
		break;
	case HNAE_LED_INACTIVE:
		dsaf_set_bit(mac_cb->cpld_led_value, DSAF_LED_ANCHOR_B,
			     CPLD_LED_DEFAULT_VALUE);
		dsaf_write_b(mac_cb->cpld_vaddr, mac_cb->cpld_led_value);
		break;
	default:
		dev_err(mac_cb->dev, "invalid led state: %d!", status);
		return -EINVAL;
	}

	return ret;
}

#define RESET_REQ_OR_DREQ 1
#define DSAF_RESET_REQ_VAL 0xfffff

void hns_dsaf_rst(struct dsaf_device *dsaf_dev, u32 val)
{
	u32 xbar_reg_addr;
	u32 nt_reg_addr;

	if (AE_IS_VER1(dsaf_dev->dsaf_ver)) {
		if (!val) {
			xbar_reg_addr = DSAF_SUB_SC_XBAR_RESET_REQ_REG;
			nt_reg_addr = DSAF_SUB_SC_NT_RESET_REQ_REG;
		} else {
			xbar_reg_addr = DSAF_SUB_SC_XBAR_RESET_DREQ_REG;
			nt_reg_addr = DSAF_SUB_SC_NT_RESET_DREQ_REG;
		}

		dsaf_write_reg(dsaf_dev->sc_base, xbar_reg_addr,
			       RESET_REQ_OR_DREQ);
		dsaf_write_reg(dsaf_dev->sc_base, nt_reg_addr,
			       RESET_REQ_OR_DREQ);
	} else {
		if (!val) {
			xbar_reg_addr = DSAF_SUB_SC_DSAF_RESET_REQ_REG;
			nt_reg_addr = DSAF_SUB_SC_DSAF_CLK_DIS_REG;
		} else {
			xbar_reg_addr = DSAF_SUB_SC_DSAF_RESET_DREQ_REG;
			nt_reg_addr = DSAF_SUB_SC_DSAF_CLK_EN_REG;
		}

		dsaf_write_reg(dsaf_dev->sc_base, xbar_reg_addr,
			       DSAF_RESET_REQ_VAL);
		mdelay(10);

		/*enable com_st and xbar_com bits for init register first*/
		if (!val)
			dsaf_write_reg(dsaf_dev->sc_base, nt_reg_addr,
				       DSAF_RESET_REQ_VAL);
		else
			dsaf_write_reg(dsaf_dev->sc_base, nt_reg_addr, 3 << 18);

	}
}

void hns_dsaf_clk_enable_all(struct dsaf_device *dsaf_dev)
{
	if (!AE_IS_VER1(dsaf_dev->dsaf_ver))
		dsaf_write_reg(dsaf_dev->sc_base, DSAF_SUB_SC_DSAF_CLK_EN_REG,
			       DSAF_RESET_REQ_VAL);
}

void hns_dsaf_xge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val)
{
	u32 reg_val = 0;
	u32 reg_addr;

	if (port >= DSAF_XGE_NUM)
		return;

	reg_val |= 0x2082082 << port;

	if (val == 0)
		reg_addr = DSAF_SUB_SC_XGE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_XGE_RESET_DREQ_REG;

	dsaf_write_reg(dsaf_dev->sc_base, reg_addr, reg_val);
}

/**
 * hns_dsaf_srst_chns - reset dsaf channels
 * @dsaf_dev: dsaf device struct pointer
 * @msk: xbar channels mask value:
 * bit0-5 for xge0-5
 * bit6-11 for ppe0-5
 * bit12-17 for roce0-5
 * bit18-19 for com/dfx
 * @val: 0 - request reset , 1 - drop reset
 */
void hns_dsaf_srst_chns(struct dsaf_device *dsaf_dev, u32 msk, u32 val)
{
	u32 reg_addr;

	if (val == 0)
		reg_addr = DSAF_SUB_SC_DSAF_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_DSAF_RESET_DREQ_REG;

	dsaf_write_reg(dsaf_dev->sc_base, reg_addr, msk);
}

void hns_dsaf_roce_srst(struct dsaf_device *dsaf_dev, u32 val)
{
	if (val == 0) {
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_ROCEE_RESET_REQ_REG, 1);
	} else {
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_ROCEE_CLK_DIS_REG, 1);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_ROCEE_RESET_DREQ_REG, 1);
		msleep(20);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_ROCEE_CLK_EN_REG, 1);
	}
}

void hns_dsaf_mac_cfg_srst(struct dsaf_device *dsaf_dev)
{
	if (AE_IS_VER1(dsaf_dev->dsaf_ver))
		return;

	dsaf_write_reg(dsaf_dev->sc_base,
		       DSAF_SUB_SC_GE_RESET_REQ0_REG, 0x01);
	dsaf_write_reg(dsaf_dev->sc_base,
		       DSAF_SUB_SC_XGE_RESET_REQ_REG, 0x01);
	mdelay(10);
	dsaf_write_reg(dsaf_dev->sc_base,
		       DSAF_SUB_SC_GE_RESET_DREQ0_REG, 0x01);
	dsaf_write_reg(dsaf_dev->sc_base,
		       DSAF_SUB_SC_XGE_RESET_DREQ_REG, 0x01);
	mdelay(10);
}

void hns_dsaf_ge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val)
{
	u32 reg_val_1;
	u32 reg_val_2;

	if (port >= DSAF_GE_NUM)
		return;

	if (port < DSAF_SERVICE_NW_NUM) {
		reg_val_1  = 0x1 << port;
		/* there is difference between V1 and V2 in register.*/
		if (AE_IS_VER1(dsaf_dev->dsaf_ver))
			reg_val_2  = 0x1041041 << port;
		else
			reg_val_2  = 0x2082082 << port;

		if (val == 0) {
			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_GE_RESET_REQ1_REG,
				       reg_val_1);

			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_GE_RESET_REQ0_REG,
				       reg_val_2);
		} else {
			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_GE_RESET_DREQ0_REG,
				       reg_val_2);

			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_GE_RESET_DREQ1_REG,
				       reg_val_1);
		}
	} else {
		reg_val_1 = 0x15540 << (port - 6);
		if (AE_IS_VER1(dsaf_dev->dsaf_ver))
			reg_val_2 = 0x100 << (port - 6);
		else
			reg_val_2 = 0x40 << (port - 6);

		if (val == 0) {
			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_GE_RESET_REQ1_REG,
				       reg_val_1);

			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_PPE_RESET_REQ_REG,
				       reg_val_2);
		} else {
			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_GE_RESET_DREQ1_REG,
				       reg_val_1);

			dsaf_write_reg(dsaf_dev->sc_base,
				       DSAF_SUB_SC_PPE_RESET_DREQ_REG,
				       reg_val_2);
		}
	}
}

void hns_ppe_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val)
{
	u32 reg_val = 0;
	u32 reg_addr;

	reg_val |= RESET_REQ_OR_DREQ << port;

	if (val == 0)
		reg_addr = DSAF_SUB_SC_PPE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_PPE_RESET_DREQ_REG;

	dsaf_write_reg(dsaf_dev->sc_base, reg_addr, reg_val);
}

void hns_ppe_com_srst(struct ppe_common_cb *ppe_common, u32 val)
{
	int comm_index = ppe_common->comm_index;
	struct dsaf_device *dsaf_dev = ppe_common->dsaf_dev;
	u32 reg_val;
	u32 reg_addr;

	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX) {
		reg_val = RESET_REQ_OR_DREQ;
		if (val == 0)
			reg_addr = DSAF_SUB_SC_RCB_PPE_COM_RESET_REQ_REG;
		else
			reg_addr = DSAF_SUB_SC_RCB_PPE_COM_RESET_DREQ_REG;

	} else {
		if (AE_IS_VER1(dsaf_dev->dsaf_ver))
			reg_val = 0x100 << (comm_index - 1);
		else
			reg_val = 0x40 << (comm_index - 1);

		if (val == 0)
			reg_addr = DSAF_SUB_SC_PPE_RESET_REQ_REG;
		else
			reg_addr = DSAF_SUB_SC_PPE_RESET_DREQ_REG;
	}

	dsaf_write_reg(dsaf_dev->sc_base, reg_addr, reg_val);
}

/**
 * hns_mac_get_sds_mode - get phy ifterface form serdes mode
 * @mac_cb: mac control block
 * retuen phy interface
 */
phy_interface_t hns_mac_get_phy_if(struct hns_mac_cb *mac_cb)
{
	u32 mode;
	u32 reg;
	u32 shift;
	bool is_ver1 = AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver);
	void __iomem *sys_ctl_vaddr = mac_cb->sys_ctl_vaddr;
	int mac_id = mac_cb->mac_id;
	phy_interface_t phy_if = PHY_INTERFACE_MODE_NA;

	if (is_ver1 && (mac_id >= 6 && mac_id <= 7)) {
		phy_if = PHY_INTERFACE_MODE_SGMII;
	} else if (mac_id >= 0 && mac_id <= 3) {
		reg = is_ver1 ? HNS_MAC_HILINK4_REG : HNS_MAC_HILINK4V2_REG;
		mode = dsaf_read_reg(sys_ctl_vaddr, reg);
		/* mac_id 0, 1, 2, 3 ---> hilink4 lane 0, 1, 2, 3 */
		shift = is_ver1 ? 0 : mac_id;
		if (dsaf_get_bit(mode, shift))
			phy_if = PHY_INTERFACE_MODE_XGMII;
		else
			phy_if = PHY_INTERFACE_MODE_SGMII;
	} else if (mac_id >= 4 && mac_id <= 7) {
		reg = is_ver1 ? HNS_MAC_HILINK3_REG : HNS_MAC_HILINK3V2_REG;
		mode = dsaf_read_reg(sys_ctl_vaddr, reg);
		/* mac_id 4, 5, 6, 7 ---> hilink3 lane 2, 3, 0, 1 */
		shift = is_ver1 ? 0 : mac_id <= 5 ? mac_id - 2 : mac_id - 6;
		if (dsaf_get_bit(mode, shift))
			phy_if = PHY_INTERFACE_MODE_XGMII;
		else
			phy_if = PHY_INTERFACE_MODE_SGMII;
	}
	return phy_if;
}

/**
 * hns_mac_config_sds_loopback - set loop back for serdes
 * @mac_cb: mac control block
 * retuen 0 == success
 */
int hns_mac_config_sds_loopback(struct hns_mac_cb *mac_cb, u8 en)
{
	u8 *base_addr;

	const u8 lane_id[] = {
		0,	/* mac 0 -> lane 0 */
		1,	/* mac 1 -> lane 1 */
		2,	/* mac 2 -> lane 2 */
		3,	/* mac 3 -> lane 3 */
		2,	/* mac 4 -> lane 2 */
		3,	/* mac 5 -> lane 3 */
		0,	/* mac 6 -> lane 0 */
		1	/* mac 7 -> lane 1 */
	};
#define RX_CSR(lane, reg) ((0x4080 + (reg) * 0x0002 + (lane) * 0x0200) * 2)
	u64 reg_offset = RX_CSR(lane_id[mac_cb->mac_id], 0);

	int sfp_prsnt;
	int ret = hns_mac_get_sfp_prsnt(mac_cb, &sfp_prsnt);

	if (!mac_cb->phy_node) {
		if (ret)
			pr_info("please confirm sfp is present or not\n");
		else
			if (!sfp_prsnt)
				pr_info("no sfp in this eth\n");
	}

#define HILINK_ACCESS_SEL_CFG		0x40008
	if (!AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver)) {
		/* both hilink4 & hilink3 base addr are 0xc2200000 which is the
		 * same as xge training and xge u adaptor. and it is a hilink
		 * access sel cfg reg to distinguish between them
		 */
		base_addr = (u8 *)mac_cb->serdes_vaddr + 0x00200000;

		if (mac_cb->mac_id <= 3)
			dsaf_write_reg(base_addr, HILINK_ACCESS_SEL_CFG, 0x0);
		else
			dsaf_write_reg(base_addr, HILINK_ACCESS_SEL_CFG, 0x3);
	} else {
		/* port 0-3 hilink4 base is serdes_vaddr + 0x00280000
		 * port 4-7 hilink3 base is serdes_vaddr + 0x00200000
		 */
		base_addr = (u8 *)mac_cb->serdes_vaddr +
			(mac_cb->mac_id <= 3 ? 0x00280000 : 0x00200000);
	}

	dsaf_set_reg_field(base_addr, reg_offset, 1ull << 10, 10, !!en);

	return 0;
}

static int wait_for_dsaf_reg_read(u8 __iomem *base, u32 reg, u32 bit_map,
				  enum wait_for_reg_read_type type)
{
	int i;
	u32 reg_val;

	for (i = 0; i < MAX_REG_READ_NUM; i++) {
		reg_val = dsaf_read_reg(base, reg);
		if (((type == WAIT_FOR_REG_READ_1) &&
		     ((1UL << bit_map) & reg_val)) ||
		    ((type == WAIT_FOR_REG_READ_0) &&
		     !((1UL << bit_map) & reg_val)))
			break;

		mdelay(READ_REG_PERIOD);
	}

	if (i >= MAX_REG_READ_NUM) {
		pr_err("timeout: wait for expected reg_val over %dms\n",
		       READ_REG_PERIOD * MAX_REG_READ_NUM);
		return -EBUSY;
	} else {
		return 0;
	}
}

/**
 * hns_xbar_rst_hw - xbar channel reset
 * @dsaf_dev: the xbar blonged to
 * @channel: the channel number
 * make sure different xbar node don't reset at sametime.
 * Because they share with same registers in reset.
 */
int hns_xbar_rst_hw(struct dsaf_device *dsaf_dev, unsigned int channel)
{
	u32 reg_val;
	int ret = 0;

	assert(channel >= MAX_XBAR_NUM);

	/* request to reset xbar channel */
	reg_val = dsaf_read_dev(dsaf_dev, DSAF_SAFE_RST_REG);
	if ((1UL << channel) & reg_val) {
		dev_dbg(dsaf_dev->dev,
			"dsaf still in xbar channel %u reset.\n", channel);
		goto out;
	}

	dsaf_set_bit(reg_val, channel, 1);
	dsaf_write_dev(dsaf_dev, DSAF_SAFE_RST_REG, reg_val);

	/* Is safe reset ok? */
	ret = wait_for_dsaf_reg_read(dsaf_dev->io_base, DSAF_SAFE_RST_OK_REG,
				     channel, WAIT_FOR_REG_READ_1);
	if (ret) {
		dev_info(dsaf_dev->dev,
			 "dsaf xbar channel %u can not be safely reset after %d ms.\n",
			 channel, (MAX_REG_READ_NUM * READ_REG_PERIOD));
		/* It was doomed to failed for some hardware errors.
		 * We can try fast reset flow.
		 */
		dsaf_set_bit(reg_val, channel, 0);
		dsaf_write_dev(dsaf_dev, DSAF_SAFE_RST_REG, reg_val);
		goto fast_rst;
	}

fast_rst:
	mb();	/* memory barrier for different dispatch access */

	/* reset xbar channel */
	reg_val = dsaf_read_reg(dsaf_dev->sc_base, SC_DSAF_RESET_REQ);
	dsaf_set_bit(reg_val, channel, 1);
	dsaf_write_reg(dsaf_dev->sc_base, SC_DSAF_RESET_REQ, reg_val);

	/* Is xbar channel reset ok? */
	ret = wait_for_dsaf_reg_read(dsaf_dev->sc_base, SC_DSAF_RESET_ST_REQ,
				     channel, WAIT_FOR_REG_READ_1);
	if (ret) {
		dev_err(dsaf_dev->dev,
			"xbar channel %u can not be successfully reset after %d ms.\n",
			channel, (MAX_REG_READ_NUM * READ_REG_PERIOD));
		ret = -EPERM;
		goto out;
	}

	/* disable clock */
	reg_val = dsaf_read_reg(dsaf_dev->sc_base, DSAF_SUB_SC_DSAF_CLK_ST_REG);
	reg_val = (~reg_val) & 0xfffff;
	dsaf_set_bit(reg_val, channel, 1);
	dsaf_write_reg(dsaf_dev->sc_base,
		       DSAF_SUB_SC_DSAF_CLK_DIS_REG, reg_val);
	if (channel < DSAF_SERVICE_NW_NUM) {
		reg_val = dsaf_read_reg(dsaf_dev->sc_base,
					DSAF_SUB_SC_GE_CLK_ST_REG);
		reg_val = (~reg_val) & 0x7ffffff;
		dsaf_set_bit(reg_val, channel, 1);
		dsaf_set_bit(reg_val, channel + 8, 1);
		dsaf_set_bit(reg_val, channel + 16, 1);
		dsaf_set_bit(reg_val, channel + 24, 1);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_GE_CLK_DIS_REG, reg_val);
	} else if (channel < (DSAF_SERVICE_NW_NUM << 1)) {
		reg_val = dsaf_read_reg(dsaf_dev->sc_base,
					DSAF_SUB_SC_PPE_CLK_ST_REG);
		reg_val = (~reg_val) & 0xff;
		dsaf_set_bit(reg_val, channel - DSAF_SERVICE_NW_NUM, 1);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_PPE_CLK_DIS_REG, reg_val);
	} else {
		reg_val = dsaf_read_reg(dsaf_dev->sc_base,
					DSAF_SUB_SC_ROCEE_CLK_ST_REG);
		reg_val = (~reg_val) & 0x1;
		dsaf_set_bit(reg_val, 0, 1UL);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_ROCEE_CLK_DIS_REG, reg_val);
	}

	/* xbar dreset */
	reg_val = dsaf_read_reg(dsaf_dev->sc_base, SC_DSAF_RESET_DREQ);
	dsaf_set_bit(reg_val, channel, 1);
	dsaf_write_reg(dsaf_dev->sc_base, SC_DSAF_RESET_DREQ, reg_val);

	ret = wait_for_dsaf_reg_read(dsaf_dev->sc_base, SC_DSAF_RESET_ST_REQ,
				     channel, WAIT_FOR_REG_READ_0);
	if (ret) {
		dev_err(dsaf_dev->dev,
			"dsaf xbar channel %u can not be dreset after %d ms.\n",
			channel, (MAX_REG_READ_NUM * READ_REG_PERIOD));
		ret = -EPERM;
		goto dreset_err;
	}

dreset_err:
	/* enable clock */
	reg_val = dsaf_read_reg(dsaf_dev->sc_base, DSAF_SUB_SC_DSAF_CLK_ST_REG);
	dsaf_set_bit(reg_val, channel, 1);
	dsaf_write_reg(dsaf_dev->sc_base, DSAF_SUB_SC_DSAF_CLK_EN_REG, reg_val);
	if (channel < DSAF_SERVICE_NW_NUM) {
		reg_val = dsaf_read_reg(dsaf_dev->sc_base,
					DSAF_SUB_SC_GE_CLK_ST_REG);
		dsaf_set_bit(reg_val, channel, 1);
		dsaf_set_bit(reg_val, channel + 8, 1);
		dsaf_set_bit(reg_val, channel + 16, 1);
		dsaf_set_bit(reg_val, channel + 24, 1);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_GE_CLK_EN_REG, reg_val);
	} else if (channel < DSAF_SERVICE_NW_NUM << 1) {
		reg_val = dsaf_read_reg(dsaf_dev->sc_base,
					DSAF_SUB_SC_PPE_CLK_ST_REG);
		dsaf_set_bit(reg_val, channel - DSAF_SERVICE_NW_NUM, 1);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_PPE_CLK_EN_REG, reg_val);
	} else {
		reg_val = dsaf_read_reg(dsaf_dev->sc_base,
					DSAF_SUB_SC_ROCEE_CLK_ST_REG);
		dsaf_set_bit(reg_val, 0, 1UL);
		dsaf_write_reg(dsaf_dev->sc_base,
			       DSAF_SUB_SC_ROCEE_CLK_EN_REG, reg_val);
	}

out:
	wmb();   /* memory barrier for different dispatch access */
	return ret;
}

int hns_ppe_rcb_rst_hw(struct dsaf_device *dsaf_dev, int port_id)
{
	struct ppe_common_cb *ppe_common;
	struct rcb_common_cb *rcb_common;
	struct hns_ppe_cb *ppe_cb;
	u32 try_cnt = 0;
	int ret = 0;
	u32 val;

#define PPE_RST_MAX_TRY_NUM (20)

	if (!(port_id >= 0 && port_id < DSAF_SERVICE_PORT_NUM_PER_DSAF)) {
		dev_err(dsaf_dev->dev, "unsupported port: %d!\n", port_id);
		return -EINVAL;
	}

	/* Only the first ppe common can be rst. */
	ppe_common = dsaf_dev->ppe_common[0];
	ppe_cb = &ppe_common->ppe_cb[port_id];

	rcb_common = dsaf_dev->rcb_common[0];

	while (try_cnt < PPE_RST_MAX_TRY_NUM) {
		val = dsaf_read_dev(ppe_cb, PPE_CURR_RX_ST_REG);
		if (val)
			break;
		mdelay(20);
		try_cnt++;
	}
	if (try_cnt >= PPE_RST_MAX_TRY_NUM) {
		dev_err(dsaf_dev->dev,
			"dsaf port %u can not be safely reset for PPE_CURR_RX_ST.",
			port_id);
		ret = -EPERM;
		goto out;
	}

	/* Stop new send task. */
	dsaf_write_dev(rcb_common, RCB_COM_CFG_PPE_TNL_CLKEN_REG,
		       ~(1UL << port_id) & 0x3f);

	/* Start reset. */
	dsaf_write_dev(ppe_cb, PPE_CFG_TNL_TO_BE_RST_REG, 1);

	/* Check allow reset or not. */
	try_cnt = 0;
	while (try_cnt < PPE_RST_MAX_TRY_NUM) {
		val = dsaf_read_dev(ppe_cb, PPE_CURR_TNL_CAN_RST_REG);
		if (val)
			break;
		mdelay(10);
		try_cnt++;
	}

	if (try_cnt >= PPE_RST_MAX_TRY_NUM) {
		dev_err(dsaf_dev->dev,
			"dsaf port %u can not be safely reset for PPE_CURR_TNL_CAN_RST.",
			port_id);
		ret = -EPERM;
		goto be_rst_err;
	}

	hns_ppe_init_hw(ppe_cb);

be_rst_err:
	dsaf_write_dev(rcb_common, RCB_COM_CFG_PPE_TNL_CLKEN_REG, 0x3f);
	mdelay(10);

out:
	return ret;
}
