/* Copyright (c) 2015,2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mbi.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

/* Use the head file for Export some functions for
 * RTOS' Flow Control which is depends on Gmac
 * Driver.
 */
#include "hi1382_gmac.h"

#define DRV_NAME		"hi1382-gmac"
#define DRV_VERSION		"v1.0"

/* PPE use vfid 8 (pf), pool 31 and group 127 for Gmac */
#define PPE_GMAC_VFID		8
#define PPE_POOL_NUM		31
#define PPE_GROUP		127
/* Each PPE Tnl size is 0x1000 */
#define GMAC_PPE_TNL_SIZE	0x1000
#define GMAC_PPE_TNL_BASE	(GMAC_PPE_TNL_SIZE * port_id)

/* PPE_COMMON REG */
#define PPE_ACC_STREAMID_MODE_REG		(0x670)
#define PPE_ACC_PF_STREAMID_REG			(0x67C)
#define PPE_CFG_REQ_BMU_OUT_DEPTH_REG		(0x6E8)
#define PPE_CFG_BUS_CACHE_REG                   (0x6F8)
#define PPE_CFG_RX_BUFF_FIFO_SIZE_REG		(0x0C00 + PPE_POOL_NUM * 0x4)
#define PPE_CFG_RX_CFF_ADDR_REG			(0x0C80 + PPE_POOL_NUM * 0x4)
#define PPE_CFG_POOL_MAP_REG			(0x0700 + PPE_POOL_NUM * 0x4)
#define PPE_CFG_RX_BUFF_FIFO_RX_BUF_SIZE_REG	(0x20280 + PPE_POOL_NUM * 0x4)
#define PPE_CURR_BUF_CNT_REG			(0x20300 + PPE_POOL_NUM * 0x4)
#define PPE_CFG_VF_GRP_POOL_MAP_REG		(0x20400 + PPE_GROUP * 0x4)

/* PPE_TNL_0_9 REG */
#define PPE_ABNORMAL_INTEN_REG		(GMAC_PPE_TNL_BASE + 0x10000)
#define PPE_ABNORMAL_INTSTS_REG		(GMAC_PPE_TNL_BASE + 0x10004)
#define PPE_ABNORMAL_RINT_REG		(GMAC_PPE_TNL_BASE + 0x10008)
#define PPE_NORMAL_INTEN_REG		(GMAC_PPE_TNL_BASE + 0x10080)
#define PPE_NORMAL_INTSTS_REG		(GMAC_PPE_TNL_BASE + 0x10084)
#define PPE_NORMAL_RINT_REG		(GMAC_PPE_TNL_BASE + 0x10088)
#define PPE_CFG_RX_PKT_INT_REG		(GMAC_PPE_TNL_BASE + 0x100F0)
#define PPE_CFG_RX_CTRL_REG		(GMAC_PPE_TNL_BASE + 0x1011C)
#define PPE_CFG_BUS_CTRL_REG		(GMAC_PPE_TNL_BASE + 0x10118)
#define PPE_CFG_TX_FIFO_THRSLD_REG	(GMAC_PPE_TNL_BASE + 0x10120)
#define PPE_CFG_RX_FIFO_THRSLD_REG	(GMAC_PPE_TNL_BASE + 0x10124)
#define PPE_CFG_RX_FIFO_PAUSE_THRSLD	(GMAC_PPE_TNL_BASE + 0x10128)
#define PPE_CFG_MAX_FRAME_LEN_REG	(GMAC_PPE_TNL_BASE + 0x10180)
#define PPE_CFG_RX_PKT_MODE_REG		(GMAC_PPE_TNL_BASE + 0x10184)
#define PPE_CFG_QID_GRP_VFID_GEN_REG	(GMAC_PPE_TNL_BASE + 0x1019C)
#define PPE_CFG_CPU_ADD_ADDR_REG	(GMAC_PPE_TNL_BASE + 0x10400)
#define PPE_HIS_RX_WR_BD_OK_PKT_CNT_REG (GMAC_PPE_TNL_BASE + 0x10504)
#define PPE_HIS_RX_PKT_OK_CNT_REG	(GMAC_PPE_TNL_BASE + 0x10508)
#define PPE_HIS_RX_PKT_DROP_FUL_CNT_REG	(GMAC_PPE_TNL_BASE + 0x1050C)
#define PPE_CURR_CFF_DATA_NUM_REG	(GMAC_PPE_TNL_BASE + 0x105C8)

/* GE REG */
#define GE_DUPLEX_TYPE_REG				0x8
#define GE_FD_FC_TYPE_REG				0xC
#define GE_MAX_FRM_SIZE_REG				0x3C
#define GE_PORT_MODE_REG				0x40
#define GE_PORT_EN_REG					0x44
#define GE_PAUSE_EN_REG					0x48
#define GE_SHORT_RUNTS_THR_REG				0x50
#define GE_AN_NEG_STATE_REG				0x58
#define GE_TX_LOCAL_PAGE_REG				0x5c
#define GE_TRANSMIT_CONTROL_REG				0x60
#define GE_RX_OCTETS_TOTAL_OK_REG			0x80
#define GE_RX_OCTETS_BAD_REG				0x84
#define GE_RX_UC_PKTS_REG				0x88
#define GE_RX_MC_PKTS_REG				0x8C
#define GE_RX_BC_PKTS_REG				0x90
#define GE_RX_PKTS_64OCTETS_REG				0x94
#define GE_RX_PKTS_65TO127OCTETS_REG			0x98
#define GE_RX_PKTS_128TO255OCTETS_REG			0x9C
#define GE_RX_PKTS_255TO511OCTETS_REG			0xA0
#define GE_RX_PKTS_512TO1023OCTETS_REG			0xA4
#define GE_RX_PKTS_1024TO1518OCTETS_REG			0xA8
#define GE_RX_PKTS_1519TOMAXOCTETS_REG			0xAC
#define GE_RX_FCS_ERRORS_REG				0xB0
#define GE_RX_TAGGED_REG				0xB4
#define GE_RX_DATA_ERR_REG				0xB8
#define GE_RX_ALIGN_ERRORS_REG				0xBC
#define GE_RX_LONG_ERRORS_REG				0xC0
#define GE_RX_JABBER_ERRORS_REG				0xC4
#define GE_RX_PAUSE_MACCONTROL_FRAMCOUNTER_REG		0xC8
#define GE_RX_UNKNOWN_MACCONTROL_FRAMCOUNTER_REG	0xCC
#define GE_RX_VERY_LONG_ERR_CNT_REG			0xD0
#define GE_RX_RUNT_ERR_CNT_REG				0xD4
#define GE_RX_SHORT_ERR_CNT_REG				0xD8
#define GE_RX_FILT_PKT_CNT_REG				0xE8
#define GE_RX_OCTETS_TOTAL_FILT_REG			0xEC
#define GE_OCTETS_TRANSMITTED_OK_REG			0x100
#define GE_OCTETS_TRANSMITTED_BAD_REG			0x104
#define GE_TX_UC_PKTS_REG				0x108
#define GE_TX_MC_PKTS_REG				0x10C
#define GE_TX_BC_PKTS_REG				0x110
#define GE_TX_PKTS_64OCTETS_REG				0x114
#define GE_TX_PKTS_65TO127OCTETS_REG			0x118
#define GE_TX_PKTS_128TO255OCTETS_REG			0x11C
#define GE_TX_PKTS_255TO511OCTETS_REG			0x120
#define GE_TX_PKTS_512TO1023OCTETS_REG			0x124
#define GE_TX_PKTS_1024TO1518OCTETS_REG			0x128
#define GE_TX_PKTS_1519TOMAXOCTETS_REG			0x12C
#define GE_TX_EXCESSIVE_LENGTH_DROP_REG			0x14C
#define GE_TX_UNDERRUN_REG				0x150
#define GE_TX_TAGGED_REG				0x154
#define GE_TX_CRC_ERROR_REG				0x158
#define GE_TX_PAUSE_FRAMES_REG				0x15C
#define GE_LINE_LOOP_BACK_REG				0x1A8
#define GE_CF_CRC_STRIP_REG				0x1B0
#define GE_MODE_CHANGE_EN_REG				0x1B4
#define GE_SIXTEEN_BIT_CNTR_REG				0x1CC
#define GE_LD_LINK_COUNTER				0x1D0
#define GE_LOOP_REG					0x1DC
#define GE_RECV_CONTROL_REG				0x1E0
#define GE_RX_OVERRUN_CNT_REG				0x1EC
#define GE_STATION_ADDR_LOW_2_REG			0x210
#define GE_STATION_ADDR_HIGH_2_REG			0x214

/* ETHSUB SysCtl REG */
#define SC_ITS_MBIGEN_ICG_EN_REG	0x300
#define SC_GE_ICG_EN_REG		0x500
#define SC_GE_ICG_DIS_REG		0x504
#define SC_PPE_ICG_EN_REG		0x510
#define SC_PPE_ICG_DIS_REG		0x514
#define SC_GE_PCS_ICG_EN_REG		0x518
#define SC_GE_PCS_ICG_DIS_REG		0x51C
#define SC_ITS_MBIGEN_RESET_REQ_REG	0xA00
#define SC_ITS_MBIGEN_RESET_DREQ_REG	0xA04
#define SC_GE_RESET_REQ_REG		0xC00
#define SC_GE_RESET_DREQ_REG		0xC04
#define	SC_MAC_CFG_RESET_REQ_REG	0xC10
#define SC_MAC_CFG_RESET_DREQ_REG	0xC14
#define SC_MAC_RESET_REQ_REG		0xC18
#define SC_MAC_RESET_DREQ_REG		0xC1C
#define SC_PPE_RESET_REQ_REG		0xC20
#define SC_PPE_RESET_DREQ_REG		0xC24
#define SC_ITS_MBIGEN_ICG_ST_REG	0x5300
#define SC_GE_ICG_ST_REG		0x5500
#define SC_PPE_ICG_ST_REG		0x5510
#define SC_ITS_MBIGEN_RESET_ST_REG	0x5A00
#define	SC_GE_RESET_ST_REG		0x5C00
#define SC_XGE_RESET_ST_REG		0x5C08
#define SC_MAC_CFG_RESET_ST_REG		0x5C10
#define SC_MAC_RESET_ST_REG		0x5C18
#define SC_PPE_RESET_ST_REG		0x5C20

/* GE's mode(speed and duplex) */
#define MAC_10M_SGMII		6
#define MAC_100M_SGMII		7
#define MAC_1000M_SGMII		8
#define MAC_FULL_DUPLEX		1
#define MAC_HALF_DUPLEX		0
#define SGMII_1000M		0x2
#define SGMII_100M		0x1
#define SGMII_10M		0x0

/* From 1791 is for Gmac Driver */
#define RX_RING_ADDR		1791
#define RX_RING_SIZE		255
#define TX_RING_SIZE		16

#define CACHE_LINE_SIZE		64
#define PACKET_HEAD_SIZE	CACHE_LINE_SIZE
#define PACKET_DATA_SIZE	0x600
#define RX_BUFFER_SIZE		(PACKET_HEAD_SIZE + PACKET_DATA_SIZE)

/* short < pkts len < max */
#define GMAC_PPE_RX_PKT_MAX_LEN	0x17F
#define GMAC_MAX_PKT_LEN	0x5EC
#define GMAC_SHORT_PKT_LEN	0x1F

/* receive frames flag */
#define RX_PKT_DROP		BIT(0)
#define RX_L2_ERR		BIT(1)
#define RX_PKT_ERR		(RX_PKT_DROP | RX_L2_ERR)

/* TX write back check */
#define GMAC_TX_IDLE		0x77777777
#define GMAC_TX_START		0x5a5a5a5a
#define GMAC_TX_END		0x00000000

#define TX_TIMEOUT		(6 * HZ)

#define PORT_BIT BIT(port_id)

/* PHY Support on FPGA */
#define ETH_PHY_MVL88E1543_ID		0x01410ea0
/* PHY Support on Board */
#define ETH_PHY_BCM5421x_ID		0x600d8590

struct rx_desc {
	u32 reserved1[3];
	u32 pkt_len;
	u32 reserved2[6];
	u32 pkt_err;
};

struct tx_desc {
	u32 reserved1[2];
	u32 curr_addr_l;
	u16 pkt_offset;
	u16 pkt_len;
	u32 reserved2[2];
	u32 curr_addr_h;
	u32 pool_num;
	u32 num_in_chain;
	u32 reserved3[2];
	u32 cfg;
	u32 wb_addr_l;
	u32 wb_addr_h;
} __aligned(64);

struct tx_buf {
	struct tx_buf	*next;
	u32		status;
	dma_addr_t	st_phys;
	struct sk_buff	*tx_skb;
	dma_addr_t	skb_phys;
	struct tx_desc  *desc;
	dma_addr_t	desc_phys;
};

/* Tx Buffer Ring */
struct tx_buf_ring {
	struct tx_buf	*cur;
	struct tx_buf	*free;
	struct tx_buf	buf[TX_RING_SIZE];
};

struct gmac_priv {
	/* base addr */
	void __iomem *ge_base;
	void __iomem *sysctrl_base;
	void __iomem *ppe_base;

	struct net_device *netdev;
	struct device *dev;

	/* GEX, X:0-8 */
	unsigned int port_id;

	/* TX */
	struct tx_desc *tx_desc;
	dma_addr_t tx_desc_dma;
	dma_addr_t tx_phys[TX_RING_SIZE];
	u32 tx_st[TX_RING_SIZE];
	dma_addr_t tx_st_phys[TX_RING_SIZE];
	struct sk_buff *tx_skb[TX_RING_SIZE];
	unsigned int tx_head;
	unsigned int tx_tail;
	struct tx_buf_ring tx_ring;

	/* RX */
	int rx_irq;
	int abnormal_irq;
	struct net_device_stats stats;
	struct napi_struct napi;
	struct sk_buff *rx_skb[RX_RING_SIZE];
	dma_addr_t rx_phys[RX_RING_SIZE];
	unsigned int rx_skb_remain;
	unsigned int rx_buf_size;
	unsigned int rx_skb_cur;
	unsigned int rx_skb_mask;

	/* PHY */
	struct device_node *phy_node;
	struct phy_device *phy;
	phy_interface_t phy_mode;
	unsigned int phy_id;
	unsigned int autoneg;
	unsigned int old_duplex;
	unsigned int old_speed;
	unsigned int duplex;
	unsigned int speed;

	/* rtos's flow ctrl */
	int flow_cfg;
	gmac_flow_ctrl_handler flow_ctrl_handler;

	int hw_reset;
	int thread_end;
	int no_buf_cnt;
	int rcv_len_zero;
	int oom_refill_fail;
	int oom_refill_ok;
	struct task_struct *aneg_task;
	struct task_struct *gmac_daemon;
};

/* Global variable */
static struct net_device *g_ndev;
static unsigned int port_id;
struct module *mdio_mod;

/**
 * Phy operation
 */
struct phy_device *get_phy(void)
{
	struct gmac_priv *priv;

	if (!g_ndev)
		return NULL;
	priv = netdev_priv(g_ndev);
	return priv->phy;
}

int get_bcm_phy_id(void)
{
	struct phy_device *phy = get_phy();
	int id1, id2;

	if (!phy)
		return -ENODEV;
	mutex_lock(&phy->lock);
	id1 = phy_read(phy, MII_PHYSID1);
	id2 = phy_read(phy, MII_PHYSID2);
	mutex_unlock(&phy->lock);
	return ((id1 & 0xffff) << 16) | (id2 & 0xffff);
}
EXPORT_SYMBOL(get_bcm_phy_id);

int get_phy_link_st(void)
{
	struct phy_device *phy = get_phy();

	if (!phy)
		return -ENODEV;
	return phy->link;
}
EXPORT_SYMBOL(get_phy_link_st);

int get_phy_ctrl_st(void)
{
	struct phy_device *phy = get_phy();
	int val;

	if (!phy)
		return -ENODEV;
	mutex_lock(&phy->lock);
	val = phy_read(phy, MII_BMCR);
	mutex_unlock(&phy->lock);
	return val;
}
EXPORT_SYMBOL(get_phy_ctrl_st);

int set_phy_loopback(bool enable)
{
	struct phy_device *phy = get_phy();

	if (!phy)
		return -ENODEV;
	mutex_lock(&phy->lock);
	if (enable)
		phy_write(phy, MII_BMCR, 0x7300);
	else
		phy_write(phy, MII_BMCR, 0x3300);
	mutex_unlock(&phy->lock);
	return 0;
}
EXPORT_SYMBOL(set_phy_loopback);

int set_phy_power_switch(bool enable)
{
	struct phy_device *phy = get_phy();
	int val;

	if (!phy)
		return -ENODEV;
	mutex_lock(&phy->lock);
	val = phy_read(phy, MII_BMCR);

	if (enable)
		val &= ~BIT(11);
	else
		val |= BIT(11);
	phy_write(phy, MII_BMCR, val);
	mutex_unlock(&phy->lock);
	return 0;
}
EXPORT_SYMBOL(set_phy_power_switch);

int phy_rdb_switch(struct phy_device *phy, bool enable)
{
	if (enable) {
		phy_write(phy, 0x17, 0x0f7e);
		phy_write(phy, 0x15, 0x0);
	} else {
		phy_write(phy, 0x1e, 0x0087);
		phy_write(phy, 0x1f, 0x8000);
	}
	return 0;
}

int phy_rdb_write(u32 reg, u32 val)
{
	struct phy_device *phy = get_phy();

	if (!phy)
		return -ENODEV;
	mutex_lock(&phy->lock);
	phy_rdb_switch(phy, 1);
	phy_write(phy, 0x1e, reg);
	phy_write(phy, 0x1f, val);
	phy_rdb_switch(phy, 0);
	mutex_unlock(&phy->lock);
	return 0;
}
EXPORT_SYMBOL(phy_rdb_write);

int phy_rdb_read(u32 reg)
{
	struct phy_device *phy = get_phy();
	int val;

	if (!phy)
		return -ENODEV;
	mutex_lock(&phy->lock);
	phy_rdb_switch(phy, 1);
	phy_write(phy, 0x1e, reg);
	val = phy_read(phy, 0x1f);
	phy_rdb_switch(phy, 0);
	mutex_unlock(&phy->lock);
	return val;
}
EXPORT_SYMBOL(phy_rdb_read);

/* inloop:
 * cpu -skbs-> GE -skbs-> CPU
 */
int gmac_enable_inloop(bool enable)
{
	struct gmac_priv *priv;
	u32 val;

	if (!g_ndev)
		return -ENODEV;

	priv = netdev_priv(g_ndev);
	if (enable) {
		/* check line_loop status */
		val = readl_relaxed(priv->ge_base + GE_LINE_LOOP_BACK_REG);
		if (val)
			return -EEXIST;
	}

	val = readl_relaxed(priv->ge_base + GE_LOOP_REG);
	if (enable)
		val |= BIT(2);
	else
		val &= ~BIT(2);
	writel_relaxed(val, priv->ge_base + GE_LOOP_REG);

	return 0;
}
EXPORT_SYMBOL(gmac_enable_inloop);

/* lineloop
 * other platform -frames-> GE -frames-> other platform.
 * We can test it with tesgine.
 */
int gmac_enable_lineloop(bool enable)
{
	struct gmac_priv *priv;
	u32 val;

	if (!g_ndev)
		return -ENODEV;

	priv = netdev_priv(g_ndev);
	if (enable) {
		/* check in_loop status */
		val = readl_relaxed(priv->ge_base + GE_LOOP_REG);
		if (val & BIT(2))
			return -EEXIST;

		val = readl_relaxed(priv->ge_base + GE_LOOP_REG);
		val &= ~BIT(1);
		writel_relaxed(val, priv->ge_base + GE_LOOP_REG);

		/* enable linke_loop_back */
		writel_relaxed(1, priv->ge_base + GE_LINE_LOOP_BACK_REG);
	} else {
		val = readl_relaxed(priv->ge_base + GE_LOOP_REG);
		val |= BIT(1);
		writel_relaxed(val, priv->ge_base + GE_LOOP_REG);

		/* disable line_loop_back disable */
		writel_relaxed(0, priv->ge_base + GE_LINE_LOOP_BACK_REG);
	}

	return 0;
}
EXPORT_SYMBOL(gmac_enable_lineloop);

int gmac_register_flow_ctrl_handler(gmac_flow_ctrl_handler flow_ctrl_handler)
{
	struct gmac_priv *priv;

	if (!g_ndev)
		return -ENODEV;

	priv = netdev_priv(g_ndev);
	priv->flow_ctrl_handler = flow_ctrl_handler;
	return 0;
}
EXPORT_SYMBOL(gmac_register_flow_ctrl_handler);

int gmac_unregister_flow_ctrl_handler(void)
{
	struct gmac_priv *priv;

	if (!g_ndev)
		return -ENODEV;

	priv = netdev_priv(g_ndev);
	priv->flow_ctrl_handler = NULL;
	return 0;
}
EXPORT_SYMBOL(gmac_unregister_flow_ctrl_handler);

int gmac_flow_ctrl_value_set(int value)
{
	struct gmac_priv *priv;

	if (!g_ndev)
		return -ENODEV;

	priv = netdev_priv(g_ndev);
	priv->flow_cfg = value;
	return 0;
}
EXPORT_SYMBOL(gmac_flow_ctrl_value_set);

/* it will send pause frames to peer when we set 1.
 * Also, we should close it after the flow control
 * finished.
 */
void gmac_send_txpause(bool value)
{
	struct gmac_priv *priv = netdev_priv(g_ndev);
	u32 val;

	val = readl_relaxed(priv->ppe_base + PPE_CFG_RX_FIFO_PAUSE_THRSLD);
	if (value)
		val &= ~(BIT(9));
	else
		val |= BIT(9);
	writel_relaxed(val, priv->ppe_base + PPE_CFG_RX_FIFO_PAUSE_THRSLD);
}
EXPORT_SYMBOL(gmac_send_txpause);

void gmac_enable_recv(bool value)
{
	struct gmac_priv *priv = netdev_priv(g_ndev);
	u32 val;

	/* Read the RX channel configuration register to get current status */
	val = readl_relaxed(priv->ge_base + GE_PORT_EN_REG);
	if (value)
		val |= BIT(1);
	else
		val &= ~BIT(1);
	writel_relaxed(val, priv->ge_base + GE_PORT_EN_REG);
}
EXPORT_SYMBOL(gmac_enable_recv);

void gmac_enable_xmit(bool value)
{
	struct gmac_priv *priv = netdev_priv(g_ndev);
	u32 val;

	/* read the RX channel configuration register to get current status */
	val = readl_relaxed(priv->ge_base + GE_PORT_EN_REG);
	if (value)
		val |= BIT(2);
	else
		val &= ~BIT(2);
	writel_relaxed(val, priv->ge_base + GE_PORT_EN_REG);
}
EXPORT_SYMBOL(gmac_enable_xmit);

const char *mode_name(u32 val)
{
	if (val == MAC_1000M_SGMII)
		return "1000M/s";
	else if (val == MAC_100M_SGMII)
		return "100M/s";
	else if (val == MAC_10M_SGMII)
		return "10M/s";
	else
		return "Unknown";
}

unsigned int reg_to_sgmii_spd(unsigned int speed)
{
	if (speed == MAC_1000M_SGMII)
		return SPEED_1000;
	else if (speed == MAC_100M_SGMII)
		return SPEED_100;
	else if (speed == MAC_10M_SGMII)
		return SPEED_10;

	return 0;
}

/* reset GE chip */
static void gmac_reset_ge(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	/* disable Ge clk */
	writel_relaxed(PORT_BIT, priv->sysctrl_base + SC_GE_ICG_DIS_REG);
	/* disable Ge PCS clk */
	val = PORT_BIT | (PORT_BIT << 12);
	writel_relaxed(val, priv->sysctrl_base + SC_GE_PCS_ICG_DIS_REG);
	/* reset request */
	val = PORT_BIT | PORT_BIT << 10 | PORT_BIT << 20;
	writel_relaxed(val, priv->sysctrl_base + SC_GE_RESET_REQ_REG);
	writel_relaxed(PORT_BIT, priv->sysctrl_base +
		       SC_MAC_CFG_RESET_REQ_REG);
	val = PORT_BIT | (PORT_BIT << 16);
	writel_relaxed(val, priv->sysctrl_base + SC_MAC_RESET_REQ_REG);
	udelay(50);

	/* enable clk */
	val = 0x7E000000 | PORT_BIT;
	writel_relaxed(val, priv->sysctrl_base + SC_GE_ICG_EN_REG);
	udelay(50);
	val = PORT_BIT | (PORT_BIT << 12);
	writel_relaxed(val, priv->sysctrl_base + SC_GE_PCS_ICG_EN_REG);
	udelay(50);
	/* reset drequest */
	val = PORT_BIT | PORT_BIT << 10 | PORT_BIT << 20 | BIT(29);
	writel_relaxed(val, priv->sysctrl_base + SC_GE_RESET_DREQ_REG);
	udelay(50);
	writel_relaxed(PORT_BIT, priv->sysctrl_base +
		       SC_MAC_CFG_RESET_DREQ_REG);
	udelay(50);
	val = PORT_BIT | (PORT_BIT << 16);
	writel_relaxed(val, priv->sysctrl_base + SC_MAC_RESET_DREQ_REG);
	udelay(50);
}

/* reset PPE chip */
static void gmac_reset_ppe(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* disable ppe clk */
	writel_relaxed(PORT_BIT, priv->sysctrl_base + SC_PPE_ICG_DIS_REG);
	/* only ppe tnl reset request */
	writel_relaxed(PORT_BIT, priv->sysctrl_base + SC_PPE_RESET_REQ_REG);
	udelay(50);
	/* enable clk */
	writel_relaxed(PORT_BIT | BIT(13),
		       priv->sysctrl_base + SC_PPE_ICG_EN_REG);
	udelay(50);
	/* reset drequest, we should make BIT 12-20 to dorequest if need */
	writel_relaxed(0xFFFFF000 | PORT_BIT,
		       priv->sysctrl_base + SC_PPE_RESET_DREQ_REG);
	udelay(50);
}

static unsigned int gmac_get_speed(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	return readl_relaxed(priv->ge_base + GE_PORT_MODE_REG) & 0xF;
}

static unsigned int gmac_get_duplex(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	return readl_relaxed(priv->ge_base + GE_DUPLEX_TYPE_REG) & 0x1;
}

/* It config the Ge port's speed duplex */
static void gmac_config_port(struct net_device *ndev, u32 speed, u32 duplex)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	priv->old_speed = speed;
	priv->speed = speed;
	priv->duplex = duplex;

	switch (priv->phy_mode) {
	case PHY_INTERFACE_MODE_SGMII:
		if (speed == SPEED_1000)
			val = MAC_1000M_SGMII;
		else if (speed == SPEED_100)
			val = MAC_100M_SGMII;
		else if (speed == SPEED_10)
			val = MAC_10M_SGMII;
		else
			val = MAC_100M_SGMII;
		break;
	default:
		netdev_warn(ndev, "not supported mode\n");
		val = MAC_1000M_SGMII;
		break;
	}

	netdev_info(ndev, "Set GE to Port Speed: %s, Duplex: %s\n",
		    mode_name(val),
		    duplex ? "FULL" : "HALF");
	writel_relaxed(val, priv->ge_base + GE_PORT_MODE_REG);
	val = duplex;
	writel_relaxed(val, priv->ge_base + GE_DUPLEX_TYPE_REG);
	val = BIT(0);
	writel_relaxed(val, priv->ge_base + GE_MODE_CHANGE_EN_REG);
}

static void gmac_adjust_link(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phy = priv->phy;

	if ((priv->speed != phy->speed) || (priv->duplex != phy->duplex) ||
	    (priv->old_speed != priv->speed)) {
		gmac_config_port(ndev, phy->speed, phy->duplex);
		phy_print_status(phy);
	}
}

static int gmac_adjust_link_i2c(void *data)
{
	struct net_device *ndev = (struct net_device *)data;
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 link_state = 0, neg_state = 1;
	u32 speed, duplex;
	u32 val;
	u32 an_neg_state;

	while (1) {
		if (priv->thread_end)
			break;
		an_neg_state = readl_relaxed(priv->ge_base + GE_AN_NEG_STATE_REG);
		/* check an_done */
		if (!(an_neg_state & BIT(21))) {
			neg_state = 0;
			goto out;
		}

		if (!neg_state) {
			netdev_warn(ndev, "Gmac adjust success\n");
			neg_state = 1;
		}

		/* check np_link_ok */
		if (an_neg_state & BIT(15)) {
			priv->old_speed = gmac_get_speed(ndev);
			priv->old_duplex = gmac_get_duplex(ndev);
			duplex = an_neg_state & BIT(12) ? MAC_FULL_DUPLEX : MAC_HALF_DUPLEX;
			/* speed is bit[10:11] */
			val = (an_neg_state & (BIT(10) | BIT(11))) >> 10;
			switch (val) {
			case SGMII_1000M:
				speed = MAC_1000M_SGMII;
				break;
			case SGMII_100M:
				speed = MAC_100M_SGMII;
				break;
			case SGMII_10M:
				speed = MAC_10M_SGMII;
				break;
			default:
				speed = MAC_1000M_SGMII;
				netdev_info(ndev, "Set 1000M as default\n");
				break;
			};

			priv->speed = speed;
			priv->duplex = duplex;

			if (!link_state || priv->old_speed != priv->speed || priv->old_duplex != priv->duplex) {
				netdev_info(ndev, "Link is Up\n");
				link_state = 1;

				netdev_info(ndev, "Set GE to Port Speed: %s, Duplex: %s\n",
					    mode_name(speed),
					    duplex ? "FULL" : "HALF");
				/* set speed to GE port mode reg */
				writel_relaxed(speed, priv->ge_base + GE_PORT_MODE_REG);
				writel_relaxed(duplex, priv->ge_base + GE_DUPLEX_TYPE_REG);
				writel_relaxed(0x1, priv->ge_base + GE_MODE_CHANGE_EN_REG);
			}
		} else {
			if (link_state) {
				link_state = 0;
				netdev_err(ndev, "Link is Down\n");
			}
		}
out:
		msleep_interruptible(500);
	}

	netdev_err(ndev, "Gmac adjust is done\n");
	return 0;
}

/* gamc driver doesn't use bmu, so close it */
void gmac_close_bmu(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ppe_base +
			    PPE_CFG_RX_BUFF_FIFO_RX_BUF_SIZE_REG);
	val &= ~BIT(18);
	writel_relaxed(val, priv->ppe_base +
		       PPE_CFG_RX_BUFF_FIFO_RX_BUF_SIZE_REG);
}

/* gamc driver doesn't use poe, so close it */
void gmac_close_poe(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ppe_base + PPE_CFG_BUS_CTRL_REG);
	val &= ~BIT(8);
	writel_relaxed(val, priv->ppe_base + PPE_CFG_BUS_CTRL_REG);
}

void gmac_port_mode_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	writel_relaxed(BIT(0), priv->ge_base + GE_TX_LOCAL_PAGE_REG);
}

void gmac_duplex_type_set(struct net_device *ndev, u32 duplex)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	writel_relaxed(duplex, priv->ge_base + GE_DUPLEX_TYPE_REG);
}

void gmac_pkt_store_format_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* set cf_node_code to 3 cf_rx_align_num to 0 */
	writel_relaxed(0x3, priv->ppe_base + PPE_CFG_RX_CTRL_REG);
}

void gmac_pkt_input_parse_mode_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ppe_base + PPE_CFG_RX_PKT_MODE_REG);
	/* set cf_parse_mode to 1 */
	val &= ~BIT(19);
	val |= BIT(18);
	writel_relaxed(val, priv->ppe_base + PPE_CFG_RX_PKT_MODE_REG);
}

void gmac_bus_ctrl_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ppe_base + PPE_CFG_BUS_CTRL_REG);
	/* set cf_bd_endian to 0 */
	val &= (~BIT(1) & ~BIT(0));
	writel_relaxed(val, priv->ppe_base + PPE_CFG_BUS_CTRL_REG);
}

void gmac_set_ppe_cf_buf_endian(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ppe_base + PPE_CFG_REQ_BMU_OUT_DEPTH_REG);
	/* make sure cf_buf_endian is little Endian */
	val &= (~BIT(3));
	writel_relaxed(val, priv->ppe_base + PPE_CFG_REQ_BMU_OUT_DEPTH_REG);
}

void gmac_ppe_max_frm_len_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* set max_frame_len GMAC_PPE_RX_PKT_MAX_LEN */
	writel_relaxed(GMAC_PPE_RX_PKT_MAX_LEN,
		       priv->ppe_base + PPE_CFG_MAX_FRAME_LEN_REG);
}

void gmac_max_frame_len_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* set max_frm_size to GMAC_MAX_PKT_LEN */
	writel_relaxed(GMAC_MAX_PKT_LEN, priv->ge_base + GE_MAX_FRM_SIZE_REG);
}

void gmac_short_frame_len_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	/* set short_runts_thr to GMAC_SHORT_PKT_LEN */
	writel_relaxed(GMAC_SHORT_PKT_LEN,
		       priv->ge_base + GE_SHORT_RUNTS_THR_REG);
}

void gmac_tx_add_crc_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	/* set crc_add to 1 */
	val = readl_relaxed(priv->ge_base + GE_TRANSMIT_CONTROL_REG);
	val |= BIT(6);
	writel_relaxed(val, priv->ge_base + GE_TRANSMIT_CONTROL_REG);
}

void gmac_rx_strip_crc_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	/* set cf_crc_strip to 1 */
	val = 0x1;
	writel_relaxed(val, priv->ge_base + GE_CF_CRC_STRIP_REG);
}

void gmac_rx_strip_pad_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	/* set strip_pad_en to 1 */
	val = readl_relaxed(priv->ge_base + GE_RECV_CONTROL_REG);
	val |= BIT(3);
	writel_relaxed(val, priv->ge_base + GE_RECV_CONTROL_REG);
}

void gmac_rx_short_frame_recv_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ge_base + GE_RECV_CONTROL_REG);
	val |= BIT(4);
	writel_relaxed(val, priv->ge_base + GE_RECV_CONTROL_REG);
}

void gmac_tx_pad_add_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	/* set pad_enable to 1 */
	val = readl_relaxed(priv->ge_base + GE_TRANSMIT_CONTROL_REG);
	val |= BIT(7);
	writel_relaxed(val, priv->ge_base + GE_TRANSMIT_CONTROL_REG);
}

void gmac_autoneg_set(struct net_device *ndev, u32 value)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = readl_relaxed(priv->ge_base + GE_TRANSMIT_CONTROL_REG);
	/* set an_enable to 0 */
	if (value == 0)
		val &= ~BIT(5);
	else
		val |= BIT(5);
	writel_relaxed(val, priv->ge_base + GE_TRANSMIT_CONTROL_REG);
}

void gmac_interrupt_mask_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	/* Enable rx_irq */
	writel_relaxed(0x1, priv->ppe_base + PPE_NORMAL_INTEN_REG);

	/* set the Interrupt trigger
	 * cf_intrpt_pkt 1
	 * cf_intrpt_time 1
	 */
	val = (1 << 6) | 1;
	writel_relaxed(val, priv->ppe_base + PPE_CFG_RX_PKT_INT_REG);
}

void gmac_rx_fifo_set(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;

	val = (RX_RING_SIZE << 16) | BIT(11) | RX_RING_ADDR;
	writel_relaxed(val, priv->ppe_base + PPE_CFG_RX_BUFF_FIFO_SIZE_REG);
}

void gmac_cfg_qid_grp_vfid_gen(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val = 0;

	/* grp_vfid_mode 1
	 * qid_mode 1
	 * def_grp_en 1
	 * def_grp 127
	 * def_vfid 8
	 * def_qid_en 1
	 * def_qid 0
	 */
	val = BIT(18) | BIT(17) | BIT(16) |
	      (PPE_GROUP << 8) | (PPE_GMAC_VFID << 4) | BIT(3);
	writel_relaxed(val, priv->ppe_base + PPE_CFG_QID_GRP_VFID_GEN_REG);
}

void gmac_cfg_pool_map(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val = 0;

	/* bmu_pool_en 1
	 * bmu_vfid 8
	 * bmu_vf_pool_num 31
	 */
	val = BIT(9) | (8 << 5) | 31;
	writel_relaxed(val, priv->ppe_base + PPE_CFG_POOL_MAP_REG);
}

void gmac_cfg_vf_grp_pool_map(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	writel_relaxed(0xffffff, priv->ppe_base + PPE_CFG_VF_GRP_POOL_MAP_REG);
}

static void gmac_set_smmu_bypass(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	writel_relaxed(0xFFFF, priv->ppe_base + PPE_ACC_PF_STREAMID_REG);
}

static int gmac_hw_init(struct net_device *ndev)
{
	gmac_set_smmu_bypass(ndev);
	gmac_cfg_qid_grp_vfid_gen(ndev);
	gmac_cfg_pool_map(ndev);
	gmac_cfg_vf_grp_pool_map(ndev);
	gmac_pkt_store_format_set(ndev);
	gmac_duplex_type_set(ndev, MAC_FULL_DUPLEX);
	gmac_pkt_input_parse_mode_set(ndev);
	gmac_bus_ctrl_set(ndev);
	gmac_rx_fifo_set(ndev);
	gmac_set_ppe_cf_buf_endian(ndev);
	gmac_ppe_max_frm_len_set(ndev);
	gmac_max_frame_len_set(ndev);
	gmac_short_frame_len_set(ndev);
	gmac_tx_add_crc_set(ndev);
	gmac_rx_strip_crc_set(ndev);
	gmac_rx_strip_pad_set(ndev);
	gmac_rx_short_frame_recv_set(ndev);
	gmac_autoneg_set(ndev, 0);
	gmac_port_mode_set(ndev);
	gmac_autoneg_set(ndev, 1);
	gmac_tx_pad_add_set(ndev);
	gmac_close_bmu(ndev);
	gmac_close_poe(ndev);

	return 0;
}

static void gmac_clean_buffers(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	unsigned int i;
	u32 val = readl_relaxed(priv->ppe_base + PPE_CURR_BUF_CNT_REG);

	for (i = 0; i < RX_RING_SIZE; i++) {
		if (priv->rx_phys[i]) {
			dma_unmap_single(priv->dev, priv->rx_phys[i],
					 priv->rx_buf_size, DMA_FROM_DEVICE);
			priv->rx_phys[i] = 0;
		}
		if (priv->rx_skb[i])
			dev_kfree_skb_any(priv->rx_skb[i]);
	}

	while (val) {
		readl_relaxed(priv->ppe_base + PPE_CFG_RX_CFF_ADDR_REG);
		val = readl_relaxed((priv->ppe_base + PPE_CURR_BUF_CNT_REG));
	}
}

static int gmac_rx_fill_buffers(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	unsigned long buf_addr;
	unsigned int i;
	unsigned int err;
	u32 val;

	val = RX_BUFFER_SIZE / 64;
	writel_relaxed(val, priv->ppe_base +
		       PPE_CFG_RX_BUFF_FIFO_RX_BUF_SIZE_REG);

	for (i = 0; i < RX_RING_SIZE; i++) {
		dma_addr_t phys;

		phys = dma_map_single(priv->dev, priv->rx_skb[i]->data,
				      priv->rx_buf_size, DMA_FROM_DEVICE);
		err = dma_mapping_error(priv->dev, phys);
		if (err)
			return -EIO;

		memset(priv->rx_skb[i]->data, 0x0, RX_BUFFER_SIZE);
		priv->rx_phys[i] = phys;
		buf_addr = phys / CACHE_LINE_SIZE;
		writel_relaxed((u32)buf_addr,
			       priv->ppe_base + PPE_CFG_RX_CFF_ADDR_REG);
	}
	return 0;
}

static int gmac_rx_refill_one_buffer(struct net_device *ndev, unsigned int i)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	dma_addr_t phys;
	unsigned long buf_addr;

	priv->rx_skb[i] = NULL;
	priv->rx_phys[i] = 0;

	skb = netdev_alloc_skb(ndev, priv->rx_buf_size);
	if (!skb)
		return -ENOMEM;
	memset(skb->data, 0x0, RX_BUFFER_SIZE);
	phys = dma_map_single(priv->dev, skb->data,
			      priv->rx_buf_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(priv->dev, phys)) {
		dev_kfree_skb_any(skb);
		return -EIO;
	}
	priv->rx_skb[i] = skb;
	priv->rx_phys[i] = phys;
	buf_addr = phys / CACHE_LINE_SIZE;
	writel_relaxed(buf_addr, priv->ppe_base + PPE_CFG_RX_CFF_ADDR_REG);

	return 0;
}

static int gmac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	unsigned int count = 0;
	struct tx_desc *tx_desc;
	dma_addr_t phys, tx_st_phys;
	struct tx_buf_ring *tx_ring = &priv->tx_ring;
	struct tx_buf *cur = tx_ring->cur;
	unsigned int bytes_compl = 0, pkts_compl = 0;

	/* before read data use smp_rmb */
	smp_rmb();

	tx_st_phys = dma_map_single(priv->dev, &cur->status,
				    sizeof(u32), DMA_TO_DEVICE);
	if (dma_mapping_error(priv->dev, tx_st_phys)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	cur->st_phys = tx_st_phys;

	phys = dma_map_single(priv->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(priv->dev, phys)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	cur->skb_phys = phys;
	cur->tx_skb = skb;
	tx_desc = cur->desc;

	tx_desc->pool_num = cpu_to_le32(31 << 24);
	tx_desc->pkt_len = cpu_to_le16(skb->len);
	tx_desc->pkt_offset = cpu_to_le16(phys % CACHE_LINE_SIZE);
	tx_desc->num_in_chain = cpu_to_le32(1 << 24);
	tx_desc->cfg = cpu_to_le32(BIT(7));
	tx_desc->curr_addr_l = cpu_to_le32((u32)phys);

	tx_desc->curr_addr_h = cpu_to_le32((phys >> 32) << 8);

	cur->status = GMAC_TX_START;
	tx_desc->wb_addr_l = cpu_to_le32((u32)tx_st_phys);
	tx_desc->wb_addr_h = cpu_to_le32(tx_st_phys >> 32);

	phys = cur->desc_phys;
	phys = (phys >> 6) | BIT(31);
	writel_relaxed(phys, priv->ppe_base + PPE_CFG_CPU_ADD_ADDR_REG);

	/* check write back value */
	while (cur->status != GMAC_TX_END) {
		udelay(10);
		count++;
		if (count >= 2000) {
			if (printk_ratelimit())
				netdev_err(ndev, "Xmit Timeout\n");
			break;
		}
	}

	ndev->trans_start = jiffies;
	netdev_sent_queue(ndev, skb->len);
	ndev->stats.tx_bytes += skb->len;
	ndev->stats.tx_packets++;

	/* Free the buff as soon as possible */
	if (cur->skb_phys) {
		dma_unmap_single(priv->dev, cur->skb_phys,
				 cur->tx_skb->len,
				 DMA_TO_DEVICE);
		cur->skb_phys = 0;
	}
	if (cur->st_phys) {
		dma_unmap_single(priv->dev, cur->st_phys,
				 sizeof(u32),
				 DMA_TO_DEVICE);
		cur->st_phys = 0;
	}
	pkts_compl++;
	bytes_compl += cur->tx_skb->len;
	dev_kfree_skb_any(cur->tx_skb);
	cur->tx_skb = NULL;
	cur->status = GMAC_TX_IDLE;

	netdev_completed_queue(ndev, pkts_compl, bytes_compl);

	cur = cur->next;
	tx_ring->cur = cur;

	/* use smp_wmb */
	smp_wmb();

	return NETDEV_TX_OK;
}

static void gmac_timeout(struct net_device *ndev)
{
	ndev->stats.tx_errors++;
	netif_wake_queue(ndev);
}

static u32 gmac_recv_cnt(struct gmac_priv *priv)
{
	return readl_relaxed(priv->ppe_base + PPE_HIS_RX_WR_BD_OK_PKT_CNT_REG);
}

static int hi1382_rx_poll(struct napi_struct *napi, int budget)
{
	struct gmac_priv *priv = container_of(napi, struct gmac_priv, napi);
	struct net_device *ndev = priv->netdev;
	struct sk_buff *skb = NULL;
	unsigned int rx_skb_cur;
	unsigned int len, err;
	struct rx_desc *rx_desc = NULL;
	int rx = 0;
	unsigned int cnt = gmac_recv_cnt(priv) + priv->rx_skb_remain;
	int ret;

	priv->rx_skb_remain = 0;
	while (cnt) {
		rx_skb_cur = priv->rx_skb_cur;
		skb = priv->rx_skb[rx_skb_cur];
		if (unlikely(!skb)) {
			ret = gmac_rx_refill_one_buffer(ndev, rx_skb_cur);
			if (ret && printk_ratelimit())
				netdev_err(ndev, "Refill Failed again %d\n", ret);
			priv->rx_skb_cur++;
			if (priv->rx_skb_cur == RX_RING_SIZE)
				priv->rx_skb_cur = 0;
			rx = budget;
			priv->rx_skb_remain = cnt;
			goto done;
		}
		dma_unmap_single(priv->dev, priv->rx_phys[rx_skb_cur],
				 priv->rx_buf_size, DMA_FROM_DEVICE);
		priv->rx_phys[rx_skb_cur] = 0;
		rx_desc = (struct rx_desc *)skb->data;
		len = le32_to_cpu(rx_desc->pkt_len) >> 16;
		err = le32_to_cpu(rx_desc->pkt_err);

		if (0 == len) {
			priv->rcv_len_zero++;
			ndev->stats.rx_length_errors++;
			ndev->stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			goto refill;
		}
		if (err & RX_PKT_ERR) {
			ndev->stats.rx_errors++;
			ndev->stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			goto refill;
		}
		if (len >= PACKET_DATA_SIZE) {
			ndev->stats.rx_length_errors++;
			ndev->stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			goto refill;
		}

		if (priv->flow_ctrl_handler)
			priv->flow_ctrl_handler();

		skb_reserve(skb, PACKET_HEAD_SIZE);
		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, ndev);

		netif_receive_skb(skb);

		ndev->stats.rx_bytes += len;
		ndev->stats.rx_packets++;
		ndev->last_rx = jiffies;
refill:
		rx++;
		/* TODO: check refill */
		ret = gmac_rx_refill_one_buffer(ndev, rx_skb_cur);
		if (ret && printk_ratelimit())
			netdev_err(ndev, "Memory alloc failed, Refill Failed %d\n", ret);

		rx_skb_cur++;
		if (RX_RING_SIZE == rx_skb_cur)
			rx_skb_cur = 0;
		priv->rx_skb_cur = rx_skb_cur;

		if (rx >= budget) {
			priv->rx_skb_remain = cnt - 1;
			goto done;
		}
		if (--cnt == 0)
			cnt = gmac_recv_cnt(priv);
	}

	napi_complete(napi);

	/* clean rx interrupt */
	writel_relaxed(0x1, priv->ppe_base + PPE_NORMAL_RINT_REG);
	/* enable rx interrupt */
	writel_relaxed(0x1, priv->ppe_base + PPE_NORMAL_INTEN_REG);
done:

	return rx;
}

static irqreturn_t gmac_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct gmac_priv *priv = netdev_priv(ndev);
	irqreturn_t ret = IRQ_HANDLED;
	u32 val;

	/* interrupt state */
	val = readl_relaxed(priv->ppe_base + PPE_NORMAL_INTSTS_REG);
	if (val == 1 && napi_schedule_prep(&priv->napi)) {
		/* disable interrupt */
		writel_relaxed(0x0, priv->ppe_base + PPE_NORMAL_INTEN_REG);
		__napi_schedule(&priv->napi);
	}

	return ret;
}

static int gmac_set_mac_address(struct net_device *ndev, void *p)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	struct sockaddr *addr = p;
	u32 mac_addr_l, mac_addr_h;

	if (!is_valid_ether_addr(addr->sa_data)) {
		netdev_err(ndev, "Invalid ether addr\n");
		return -EADDRNOTAVAIL;
	}

	memcpy(ndev->dev_addr, addr->sa_data, ndev->addr_len);
	mac_addr_l = (ndev->dev_addr[0] << 8) | (ndev->dev_addr[1]);
	mac_addr_h = (ndev->dev_addr[2] << 24) |
		     (ndev->dev_addr[3] << 16) |
		     (ndev->dev_addr[4] << 8) |
		     (ndev->dev_addr[5]);

	writel_relaxed(mac_addr_l, priv->ge_base + GE_STATION_ADDR_LOW_2_REG);
	writel_relaxed(mac_addr_h, priv->ge_base + GE_STATION_ADDR_HIGH_2_REG);

	return 0;
}

static void gmac_free_buffers(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	unsigned int  i;

	for (i = 0; i < TX_RING_SIZE; i++) {
		if (priv->tx_phys[i]) {
			dma_unmap_single(priv->dev, priv->tx_phys[i],
					 priv->tx_skb[i]->len,
					 DMA_TO_DEVICE);
			priv->tx_phys[i] = 0;
		}
		if (priv->tx_skb[i])
			dev_kfree_skb_any(priv->tx_skb[i]);
	}

	dma_free_coherent(priv->dev, TX_RING_SIZE * PACKET_HEAD_SIZE,
			  priv->tx_desc, priv->tx_desc_dma);
}

static int gmac_rx_ring_init(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	int i;

	priv->rx_buf_size = RX_BUFFER_SIZE;

	for (i = 0; i < RX_RING_SIZE; i++) {
		priv->rx_skb[i] = netdev_alloc_skb(ndev, priv->rx_buf_size);
		memset(priv->rx_skb[i]->data, 0, priv->rx_buf_size);
		if (!priv->rx_skb[i])
			return -ENOMEM;
	}
	priv->rx_skb_cur = 0;

	return 0;
}

static int gmac_tx_ring_init(struct net_device *ndev)
{
	struct gmac_priv *priv  = netdev_priv(ndev);
	struct tx_buf_ring *tx_ring = &priv->tx_ring;
	struct tx_buf *buf = tx_ring->buf;
	int i;

	priv->tx_desc = dma_alloc_coherent(priv->dev,
					   TX_RING_SIZE * PACKET_HEAD_SIZE,
					   &priv->tx_desc_dma, GFP_ATOMIC);
	if (!priv->tx_desc)
		return -ENOMEM;

	for (i = 0; i < TX_RING_SIZE; i++) {
		buf[i].desc_phys = priv->tx_desc_dma + i * PACKET_HEAD_SIZE;
		buf[i].desc = &priv->tx_desc[i];

		buf[i].tx_skb = NULL;
		buf[i].status = GMAC_TX_IDLE;
		buf[i].next = &buf[(i + 1) % TX_RING_SIZE];
	}

	tx_ring->cur = &buf[0];
	tx_ring->free = &buf[0];

	return 0;
}

static int gmac_daemon_task(void *data)
{
	struct net_device *ndev = (struct net_device *)data;
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 val;
	int i, j;

	while (!kthread_should_stop()) {
		msleep_interruptible(5000);

		if (!netif_running(ndev) || priv->hw_reset)
			continue;
		val = readl_relaxed(priv->ppe_base + PPE_CURR_BUF_CNT_REG);
		if (likely(val))
			continue;
		priv->no_buf_cnt++;
		if (priv->no_buf_cnt < 2)
			continue;
		priv->no_buf_cnt = 0;

		for (i = 0; i < RX_RING_SIZE; i++) {
			priv->rx_skb[i] = netdev_alloc_skb(ndev, priv->rx_buf_size);
			if (!priv->rx_skb[i])
				break;
			memset(priv->rx_skb[i]->data, 0, priv->rx_buf_size);
		}
		if (i != RX_RING_SIZE) {
			for (j = 0; j < i; j++)
				dev_kfree_skb_any(priv->rx_skb[j]);
			priv->oom_refill_fail++;
			continue;
		}
		priv->oom_refill_ok++;
		gmac_enable_xmit(0);
		gmac_enable_recv(0);
		disable_irq(priv->rx_irq);
		writel_relaxed(0, priv->ppe_base + PPE_NORMAL_INTEN_REG);
		napi_disable(&priv->napi);
		netif_stop_queue(ndev);

		gmac_recv_cnt(priv);
		priv->rx_skb_remain = 0;
		priv->rx_skb_cur = 0;

		gmac_rx_fill_buffers(ndev);

		netdev_reset_queue(ndev);
		netif_start_queue(ndev);
		napi_enable(&priv->napi);
		enable_irq(priv->rx_irq);
		gmac_interrupt_mask_set(ndev);
		gmac_enable_xmit(1);
		gmac_enable_recv(1);
	}
	return 0;
}

static int gmac_open(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	int count = 0;

	while (priv->old_speed != priv->speed) {
		if (count >= 200) {
			netdev_warn(ndev, "Autoneg Timeout\n");
			break;
		}
		count++;
		msleep(10);
	}

	/* we should do it for make sure there is
	 * no pkts cnt in the stats reg
	 */
	gmac_recv_cnt(priv);
	priv->rx_skb_remain = 0;
	priv->rx_skb_cur = 0;
	gmac_rx_ring_init(ndev);
	gmac_rx_fill_buffers(ndev);
	netdev_reset_queue(ndev);
	netif_start_queue(ndev);
	napi_enable(&priv->napi);
	enable_irq(priv->rx_irq);
	gmac_interrupt_mask_set(ndev);
	gmac_enable_xmit(1);
	gmac_enable_recv(1);

	return 0;
}

static int gmac_close(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	gmac_enable_xmit(0);
	gmac_enable_recv(0);
	disable_irq(priv->rx_irq);
	writel_relaxed(0, priv->ppe_base + PPE_NORMAL_INTEN_REG);
	napi_disable(&priv->napi);
	netif_stop_queue(ndev);
	gmac_clean_buffers(ndev);

	return 0;
}

static const struct net_device_ops gmac_netdev_ops = {
	.ndo_open               = gmac_open,
	.ndo_stop               = gmac_close,
	.ndo_start_xmit         = gmac_start_xmit,
	.ndo_change_mtu         = eth_change_mtu,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_tx_timeout         = gmac_timeout,
	.ndo_set_mac_address    = gmac_set_mac_address,
};

static int gmac_init(struct net_device *ndev)
{
	int ret = 0;

	gmac_hw_init(ndev);

	ret = gmac_tx_ring_init(ndev);
	if (ret)
		netdev_err(ndev, "Tx Ring Init Failed\n");

	return ret;
}

static const char gmac_gstrings_test[][ETH_GSTRING_LEN] = {
	"PHY Loopback   (on/offline)"
};
#define GMAC_TEST_LEN (sizeof(gmac_gstrings_test) / ETH_GSTRING_LEN)

static void gmac_self_test(struct net_device *ndev,
			   struct ethtool_test *test, u64 *data)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	memset(data, 0, sizeof(u64) * GMAC_TEST_LEN);
	if (priv->phy_id == 0) {
		test->flags = ETH_TEST_FL_FAILED;
		data[0] = 1;
		netdev_err(ndev, "Ubppe Board does't support this feature.\n");
		return;
	}

	if (!netif_running(ndev)) {
		netdev_err(ndev, "The Iface can't self test at down state.\n");
		return;
	}

	if (!test->flags) {
		if (!netif_carrier_ok(ndev)) {
			netif_carrier_on(ndev);
			netdev_info(ndev, "PHY Loopback Test Starting.\n");
		 } else {
			netdev_info(ndev, "PHY Loopback Bit is no Setted Or Carrier is OK\n");
		}
	}
}

static int gmac_get_settings(struct net_device *ndev,
			     struct ethtool_cmd *cmd)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	if (priv->phy_id == 0) {
		cmd->supported		= 0;
		cmd->advertising	= 0;
		cmd->duplex		= priv->duplex;
		cmd->port		= PORT_MII;
		cmd->phy_address	= 0;
		cmd->transceiver	= XCVR_INTERNAL;
		cmd->autoneg		= AUTONEG_ENABLE;
		cmd->maxtxpkt		= 0;
		cmd->maxrxpkt		= 0;
		ethtool_cmd_speed_set(cmd, reg_to_sgmii_spd(priv->speed));
		return 0;
	}

	if (!priv->phy)
		return -ENODEV;

	return phy_ethtool_gset(priv->phy, cmd);
}

static int gmac_set_settings(struct net_device *ndev,
			     struct ethtool_cmd *cmd)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	u32 speed = ethtool_cmd_speed(cmd);

	/* set for UBPPE board */
	if (priv->phy_id == 0)
		return -EOPNOTSUPP;

	if (!priv->phy)
		return -ENODEV;

	if (speed == SPEED_1000 && cmd->autoneg == AUTONEG_DISABLE)
		return -EINVAL;

	return phy_ethtool_sset(priv->phy, cmd);
}

static int gmac_nway_reset(struct net_device *ndev)
{
	struct gmac_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		int count = 0, i;

		if (printk_ratelimit())
			netdev_info(ndev, "Reset the GMAC chip\n");
		netif_carrier_off(ndev);
		priv->hw_reset = 1;
		gmac_enable_xmit(0);
		gmac_enable_recv(0);
		disable_irq(priv->rx_irq);
		writel_relaxed(0, priv->ppe_base + PPE_NORMAL_INTEN_REG);
		napi_disable(&priv->napi);
		netif_stop_queue(ndev);
		gmac_clean_buffers(ndev);
		for (i = 0; i < TX_RING_SIZE; i++) {
			if (priv->tx_phys[i]) {
				dma_unmap_single(priv->dev, priv->tx_phys[i],
						 priv->tx_skb[i]->len,
						 DMA_TO_DEVICE);
				priv->tx_phys[i] = 0;
			}
			if (priv->tx_skb[i])
				dev_kfree_skb_any(priv->tx_skb[i]);
		}

		udelay(50);

		gmac_reset_ppe(ndev);
		gmac_reset_ge(ndev);
		udelay(50);

		gmac_hw_init(ndev);

		udelay(50);

		/* after hw reset we should do config the port */
		if (priv->phy_id) {
			if (priv->phy)
				gmac_config_port(ndev, priv->phy->speed, priv->phy->duplex);
		}

		/* check the speed is ok */
		while (priv->old_speed != priv->speed) {
			if (count >= 200) {
				netdev_warn(ndev, "Adjust Link Timeout while HW reset!\n");
				break;
			}
			count++;
			msleep(10);
		}

		gmac_recv_cnt(priv);
		priv->rx_skb_remain = 0;
		priv->rx_skb_cur = 0;
		gmac_rx_ring_init(ndev);
		gmac_rx_fill_buffers(ndev);
		netdev_reset_queue(ndev);
		netif_start_queue(ndev);
		napi_enable(&priv->napi);
		enable_irq(priv->rx_irq);
		gmac_interrupt_mask_set(ndev);
		gmac_enable_xmit(1);
		gmac_enable_recv(1);
		priv->hw_reset = 0;
		if (!netif_carrier_ok(ndev))
			netif_carrier_on(ndev);
	}
	return 0;
}

static struct {
	const char str[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	/* interrupt status */
	{ "intrpt_status" },
	/* ge/ppe dfx */
	{ "rx_unicast" },
	{ "rx_multicast" },
	{ "rx_broadcast" },
	{ "rx_pkts_64" },
	{ "rx_pkts_65_to_127" },
	{ "rx_pkts_128_to_255" },
	{ "rx_pkts_256_to_511" },
	{ "rx_pkts_512_to_1023" },
	{ "rx_pkts_1024_to_1518" },
	{ "rx_pkts_1519_to_max" },
	{ "rx_fcs_errors" },
	{ "rx_data_err" },
	{ "rx_align_errors" },
	{ "rx_long_errors" },
	{ "rx_very_long_err_cnt" },
	{ "rx_runt_err_cnt" },
	{ "rx_short_err_cnt" },
	{ "rx_overfun_cnt" },
	{ "rx_over_flow_cnt" },
	{ "tx_unicast" },
	{ "tx_multicast" },
	{ "tx_broadcast" },
	{ "tx_pkts_64" },
	{ "tx_pkts_65_to_127" },
	{ "tx_pkts_128_to_255" },
	{ "tx_pkts_256_to_511" },
	{ "tx_pkts_512_to_1023" },
	{ "tx_pkts_1024_to_1518" },
	{ "tx_pkts_1519_to_max" },
	{ "tx_excessivle_length_drop" },
	{ "tx_underrun" },
	{ "tx_crc_erros" },
	{ "tx_pause_frames" },
	{ "port_mode" },
	{ "duplex_type" },
	{ "port_en" },
	{ "transmit_control" },
	{ "loop_reg" },
	{ "ppe_curr_data_num" },
	/* net dev stats */
	{ "rx_bytes" },
	{ "tx_bytes" },
	{ "rx_packets" },
	{ "tx_packets" },
	{ "rx_erros" },
	{ "rx_length_errors" },
	{ "tx_erros" },
	{ "rx_dropped" },
	{ "tx_dropped" },
	{ "tx_fifo_erros" },
	{ "rcv_len_zero" },
	{ "oom_refill_fail" },
	{ "oom_refill_ok" },
	/* ppe common regs */
	{ "ppe_streamid_mode" },
	{ "ppe_pf_streamid" },
	{ "ppe_endian" },
	{ "ppe_bus_cache" },
	{ "ppe_rx_buff_fifo_size" },
	{ "ppe_curr_buf_cnt" },
	{ "ppe_vf_grp_pool_map" },
	/* flow ctrl */
	{ "flow_ctrl_cnt" },

};

static void gmac_get_strings(struct net_device *ndev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(buf, *gmac_gstrings_test, GMAC_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		memcpy(buf, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		break;
	default:
		BUG();
		break;
	}
}

static int gmac_get_sset_count(struct net_device *ndev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return GMAC_TEST_LEN;
	case ETH_SS_STATS:
		return ARRAY_SIZE(ethtool_stats_keys);
	default:
		return -EOPNOTSUPP;
	}
}

static void gmac_get_ethtool_stats(struct net_device *ndev,
				   struct ethtool_stats *estats,
				   u64 *tmp_stats)
{
	struct gmac_priv *priv = netdev_priv(ndev);
	int i = 0;

	tmp_stats[i++] = readl_relaxed(priv->ppe_base +
				       PPE_NORMAL_INTSTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_UC_PKTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_MC_PKTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_BC_PKTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_64OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_65TO127OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_128TO255OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_255TO511OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_512TO1023OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_1024TO1518OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_PKTS_1519TOMAXOCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_FCS_ERRORS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_DATA_ERR_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_ALIGN_ERRORS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_LONG_ERRORS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_VERY_LONG_ERR_CNT_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_RUNT_ERR_CNT_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_RX_SHORT_ERR_CNT_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_RX_OVERRUN_CNT_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base +
				       PPE_HIS_RX_PKT_DROP_FUL_CNT_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_TX_UC_PKTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_TX_MC_PKTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_TX_BC_PKTS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_64OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_65TO127OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_128TO255OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_255TO511OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_512TO1023OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_1024TO1518OCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PKTS_1519TOMAXOCTETS_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_EXCESSIVE_LENGTH_DROP_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_TX_UNDERRUN_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_TX_CRC_ERROR_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TX_PAUSE_FRAMES_REG);

	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_PORT_MODE_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_DUPLEX_TYPE_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_PORT_EN_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base +
				       GE_TRANSMIT_CONTROL_REG);
	tmp_stats[i++] = readl_relaxed(priv->ge_base + GE_LOOP_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base +
				       PPE_CURR_CFF_DATA_NUM_REG);

	tmp_stats[i++] = ndev->stats.rx_bytes;
	tmp_stats[i++] = ndev->stats.tx_bytes;
	tmp_stats[i++] = ndev->stats.rx_packets;
	tmp_stats[i++] = ndev->stats.tx_packets;
	tmp_stats[i++] = ndev->stats.rx_errors;
	tmp_stats[i++] = ndev->stats.rx_length_errors;
	tmp_stats[i++] = ndev->stats.tx_errors;
	tmp_stats[i++] = ndev->stats.rx_dropped;
	tmp_stats[i++] = ndev->stats.tx_dropped;
	tmp_stats[i++] = ndev->stats.tx_fifo_errors;
	tmp_stats[i++] = priv->rcv_len_zero;
	tmp_stats[i++] = priv->oom_refill_fail;
	tmp_stats[i++] = priv->oom_refill_ok;

	/* ppe regs */
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_ACC_STREAMID_MODE_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_ACC_PF_STREAMID_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_CFG_REQ_BMU_OUT_DEPTH_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_CFG_BUS_CACHE_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_CFG_RX_BUFF_FIFO_SIZE_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_CURR_BUF_CNT_REG);
	tmp_stats[i++] = readl_relaxed(priv->ppe_base + PPE_CFG_VF_GRP_POOL_MAP_REG);

	/* flow cfg */
	tmp_stats[i++] = priv->flow_cfg;
}

static void gmac_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	drvinfo->testinfo_len = GMAC_TEST_LEN;
}

static struct ethtool_ops gmac_ethtool_ops = {
	.nway_reset		= gmac_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_settings		= gmac_get_settings,
	.set_settings		= gmac_set_settings,
	.get_strings		= gmac_get_strings,
	.get_sset_count		= gmac_get_sset_count,
	.get_ethtool_stats	= gmac_get_ethtool_stats,
	.get_drvinfo		= gmac_get_drvinfo,
	.self_test		= gmac_self_test,

};

static int gmac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct gmac_priv *priv;
	struct net_device *ndev;
	struct resource *res;
	const char *mac_addr;
	u32 phy_id;
	int ret = 0;

	/* Init network device */
	ndev = alloc_netdev(sizeof(struct gmac_priv),
			    "HiFE%d",
			    NET_NAME_UNKNOWN,
			    ether_setup);

	if (!ndev) {
		netdev_err(ndev, "alloc_netdev failed\n");
		return -ENOMEM;
	}

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)))
		netdev_warn(ndev, "could not set mask and coherent 64 bit\n");

	g_ndev = ndev;
	ndev->features = 0;
	ndev->hw_features = 0;
	SET_NETDEV_DEV(ndev, dev);

	priv = netdev_priv(ndev);
	memset(priv, 0, sizeof(*priv));
	priv->netdev = ndev;
	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->sysctrl_base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(priv->sysctrl_base)) {
		netdev_err(ndev, "Sysctrl_base is NULL, so check it.\n");
		ret = PTR_ERR((void *)priv->sysctrl_base);
		goto err_free_netdev;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->ppe_base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(priv->ppe_base)) {
		netdev_err(ndev, "PPE_base is NULL, so check it.\n");
		ret = PTR_ERR((void *)priv->ppe_base);
		goto err_free_netdev;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	priv->ge_base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(priv->ge_base)) {
		netdev_err(ndev, "GE_base is NULL, so check it.\n");
		ret = PTR_ERR((void *)priv->ge_base);
		goto err_free_netdev;
	}

	ndev->base_addr = (unsigned long)priv->ge_base;

	ret = of_property_read_u32(node, "port-id", &port_id);
	if (ret  < 0 || port_id < 0 || port_id > 128) {
		dev_warn(dev, "no port-id, set to default 0\n");
		port_id = 0;
	}
	priv->port_id = port_id;

	ret = of_property_read_u32(node, "phy-id", &phy_id);
	if (ret  < 0) {
		dev_warn(dev, "no phy-id, set to default 0\n");
		phy_id = 0;
	}
	priv->phy_id = phy_id;
	if (phy_id > 0) {
		priv->phy_mode = of_get_phy_mode(node);
		if (priv->phy_mode < 0) {
			dev_err(dev, "not find phy-mode\n");
			ret = -EINVAL;
			goto err_free_netdev;
		}
	}
	/* The GMAC-specific entries in the device structure. */
	ndev->netdev_ops = &gmac_netdev_ops;
	ndev->ethtool_ops = &gmac_ethtool_ops;
	ndev->watchdog_timeo = TX_TIMEOUT;

	mac_addr = of_get_mac_address(node);
	if (mac_addr)
		ether_addr_copy(ndev->dev_addr, mac_addr);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* clean fifo */
	gmac_clean_buffers(ndev);

	/* hw reset */
	gmac_reset_ge(ndev);
	gmac_reset_ppe(ndev);

	gmac_init(ndev);

	/* Request IRQ */
	priv->rx_irq = platform_get_irq(pdev, 12 + priv->port_id);
	if (priv->rx_irq <= 0) {
		dev_err(dev, "No rx irq resource\n");
		ret = -EINVAL;
		goto err_free_buff;
	}

	ret = request_irq(priv->rx_irq, gmac_interrupt,
			  IRQF_SHARED, pdev->name, ndev);
	if (ret) {
		dev_err(dev, "request_irq rx failed\n");
		goto err_free_buff;
	}
	disable_irq(priv->rx_irq);

	if (phy_id > 0) {
		priv->phy_node = of_parse_phandle(node, "phy-handle", 0);
		if (!priv->phy_node) {
			dev_err(dev, "could not find phy-handle\n");
			ret = -EINVAL;
			goto err_free_irq;
		}
		priv->old_speed = reg_to_sgmii_spd(gmac_get_speed(ndev));
		/* get phydev */
		priv->phy = of_phy_connect(ndev, priv->phy_node,
					   &gmac_adjust_link, 0,
					   priv->phy_mode);
		if (!priv->phy) {
			netdev_err(ndev, "phy connect Failed\n");
			ret = -EINVAL;
			goto err_put_node;
		}
		priv->phy->advertising = priv->phy->supported;
		phy_start(priv->phy);
	} else {
		priv->old_speed = gmac_get_speed(ndev);
		priv->aneg_task = kthread_run(gmac_adjust_link_i2c, ndev, "gmac_i2c_monitor");
		if (!priv->aneg_task) {
			ret = PTR_ERR(priv->aneg_task);
			netdev_err(ndev, "Gmac kthread_run Failed\n");
			goto err_free_irq;
		}

	}

	priv->gmac_daemon = kthread_run(gmac_daemon_task, ndev, "gmac_daemon");
	if (!priv->gmac_daemon) {
		netdev_err(ndev, "gmac_daemon ktrhead run Failed\n");
		ret = PTR_ERR(priv->gmac_daemon);
		goto err_free_aneg;
	}

	priv->speed = 0;
	priv->duplex = gmac_get_duplex(ndev);

	netif_napi_add(ndev, &priv->napi, hi1382_rx_poll, NAPI_POLL_WEIGHT);
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(dev, "register_netdev failed\n");
		goto err_del_napi;
	}

	platform_set_drvdata(pdev, ndev);

	return ret;

err_del_napi:
	netif_napi_del(&priv->napi);
	kthread_stop(priv->gmac_daemon);
err_free_aneg:
	if (priv->aneg_task) {
		priv->thread_end = 1;
		kthread_stop(priv->aneg_task);
		priv->aneg_task = NULL;
	}
	if (priv->phy) {
		phy_stop(priv->phy);
		phy_disconnect(priv->phy);
		priv->phy = NULL;
	}
err_put_node:
	if (phy_id > 0)
		of_node_put(priv->phy_node);
err_free_buff:
	gmac_free_buffers(ndev);
err_free_irq:
	free_irq(priv->rx_irq, ndev);
err_free_netdev:
	free_netdev(ndev);

	return ret;
}

static int gmac_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct gmac_priv *priv = netdev_priv(ndev);

	if (priv->gmac_daemon) {
		kthread_stop(priv->gmac_daemon);
		priv->gmac_daemon = NULL;
	}

	if (priv->aneg_task) {
		priv->thread_end = 1;
		kthread_stop(priv->aneg_task);
		priv->aneg_task = NULL;
	}

	if (priv->phy) {
		phy_stop(priv->phy);
		phy_disconnect(priv->phy);
		priv->phy = NULL;
	}

	unregister_netdev(ndev);
	netif_napi_del(&priv->napi);
	if (priv->phy_id > 0)
		of_node_put(priv->phy_node);
	gmac_free_buffers(ndev);
	free_irq(priv->rx_irq, ndev);
	free_netdev(ndev);

	return 0;
}

static const struct of_device_id hi1382_of_match[] = {
	{ .compatible = "hisilicon,hi1382-gmac" },
	{ }
};

static struct platform_driver hi1382_ge_driver = {
	.probe = gmac_probe,
	.remove = gmac_drv_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = hi1382_of_match,
	},
};

static int __init hi1382_gmac_init(void)
{
	/*
	 * when CONFIG_HNS_MDIO is 'y', find_module will return NULL,
	 * so add IS_MODULE to check it. While according to the build
	 * order, hns_mdio would load before hi1382_gmac.
	 */
	if (IS_MODULE(CONFIG_HNS_MDIO)) {
		mdio_mod = find_module("hns_mdio");
		if (!mdio_mod) {
			pr_err("Should insmod hns_mido before gmac\n");
			return -EINVAL;
		}

		if (!try_module_get(mdio_mod)) {
			pr_err("Get Hns_mdio Module Failed\n");
			return -EINVAL;
		}
	}

	return platform_driver_register(&hi1382_ge_driver);
}

static void __exit hi1382_gmac_exit(void)
{
	if (mdio_mod)
		module_put(mdio_mod);
	platform_driver_unregister(&hi1382_ge_driver);
}

module_init(hi1382_gmac_init);
module_exit(hi1382_gmac_exit);
MODULE_DESCRIPTION("Hisilicon intercon GMAC driver");
MODULE_AUTHOR("Huawei");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hi1382-gmac");
