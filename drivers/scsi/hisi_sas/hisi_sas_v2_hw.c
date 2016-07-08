/*
 * Copyright (c) 2016 Linaro Ltd.
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v2_hw"

/* global registers need init*/
#define DLVRY_QUEUE_ENABLE		0x0
#define QUEUE_ARB_THRES_VALUE   0x4
#define IOST_BASE_ADDR_LO		0x8
#define IOST_BASE_ADDR_HI		0xc
#define ITCT_BASE_ADDR_LO		0x10
#define ITCT_BASE_ADDR_HI		0x14
#define IO_BROKEN_MSG_ADDR_LO		0x18
#define IO_BROKEN_MSG_ADDR_HI		0x1c
#define PHY_CONTEXT			0x20
#define PHY_STATE			0x24
#define PHY_PORT_NUM_MA			0x28
#define PORT_STATE			0x2c
#define PORT_STATE_PHY8_PORT_NUM_OFF	16
#define PORT_STATE_PHY8_PORT_NUM_MSK	(0xf << PORT_STATE_PHY8_PORT_NUM_OFF)
#define PORT_STATE_PHY8_CONN_RATE_OFF	20
#define PORT_STATE_PHY8_CONN_RATE_MSK	(0xf << PORT_STATE_PHY8_CONN_RATE_OFF)
#define PHY_CONN_RATE			0x30
#define HGC_CON_TIME_LIMIT      0x34
#define HGC_TRANS_TASK_CNT_LIMIT	0x38
#define AXI_AHB_CLK_CFG			0x3c
#define ITCT_CLR			0x44
#define ITCT_CLR_EN_OFF			16
#define ITCT_CLR_EN_MSK			(0x1 << ITCT_CLR_EN_OFF)
#define ITCT_DEV_OFF			0
#define ITCT_DEV_MSK			(0x7ff << ITCT_DEV_OFF)
#define AXI_USER1			0x48
#define AXI_USER2			0x4c
#define IO_SATA_BROKEN_MSG_ADDR_LO	0x58
#define IO_SATA_BROKEN_MSG_ADDR_HI	0x5c
#define SATA_INITI_D2H_STORE_ADDR_LO	0x60
#define SATA_INITI_D2H_STORE_ADDR_HI	0x64
#define SAS_HGC_STP_UPD_HDER_CFG        0x78
#define SAS_HGC_SEND_DONE_CFG           0x7c
#define SAS_HGC_OPEN_RTY_CNT_CFG        0x80
#define HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL	0x84
#define HGC_SAS_TXFAIL_RETRY_CTRL	0x88
#define HGC_GET_ITV_TIME		0x90
#define DEVICE_MSG_WORK_MODE		0x94
#define OPENA_WT_CONTI_TIME		0x9c
#define I_T_NEXUS_LOSS_TIME		0xa0
#define MAX_CON_TIME_LIMIT_TIME		0xa4
#define BUS_INACTIVE_LIMIT_TIME		0xa8
#define REJECT_TO_OPEN_LIMIT_TIME	0xac
#define CFG_AGING_TIME			0xbc
#define HGC_DFX_CFG2			0xc0
#define HGC_IOMB_PROC1_STATUS	0x104
#define CFG_1US_TIMER_TRSH		0xcc
#define HGC_INVLD_DQE_INFO		0x148
#define HGC_INVLD_DQE_INFO_FB_CH0_OFF	9
#define HGC_INVLD_DQE_INFO_FB_CH0_MSK	(0x1 << HGC_INVLD_DQE_INFO_FB_CH0_OFF)
#define HGC_INVLD_DQE_INFO_FB_CH3_OFF	18
#define INT_COAL_EN			0x19c
#define OQ_INT_COAL_TIME		0x1a0
#define OQ_INT_COAL_CNT			0x1a4
#define ENT_INT_COAL_TIME		0x1a8
#define ENT_INT_COAL_CNT		0x1ac
#define OQ_INT_SRC			0x1b0
#define OQ_INT_SRC_MSK			0x1b4
#define ENT_INT_SRC1			0x1b8
#define ENT_INT_SRC1_D2H_FIS_CH0_OFF	0
#define ENT_INT_SRC1_D2H_FIS_CH0_MSK	(0x1 << ENT_INT_SRC1_D2H_FIS_CH0_OFF)
#define ENT_INT_SRC1_D2H_FIS_CH1_OFF	8
#define ENT_INT_SRC1_D2H_FIS_CH1_MSK	(0x1 << ENT_INT_SRC1_D2H_FIS_CH1_OFF)
#define ENT_INT_SRC2			0x1bc
#define ENT_INT_SRC3			0x1c0
#define ENT_INT_SRC3_ITC_INT_OFF	15
#define ENT_INT_SRC3_ITC_INT_MSK	(0x1 << ENT_INT_SRC3_ITC_INT_OFF)
#define ENT_INT_SRC_MSK1		0x1c4
#define ENT_INT_SRC_MSK2		0x1c8
#define ENT_INT_SRC_MSK3		0x1cc
#define ENT_INT_SRC_MSK3_ENT95_MSK_OFF	31
#define ENT_INT_SRC_MSK3_ENT95_MSK_MSK	(0x1 << ENT_INT_SRC_MSK3_ENT95_MSK_OFF)
#define SAS_ECC_INTR_MSK		0x1ec
#define HGC_ERR_STAT_EN			0x238
#define DLVRY_Q_0_BASE_ADDR_LO		0x260
#define DLVRY_Q_0_BASE_ADDR_HI		0x264
#define DLVRY_Q_0_DEPTH			0x268
#define DLVRY_Q_0_WR_PTR		0x26c
#define DLVRY_Q_0_RD_PTR		0x270
#define HYPER_STREAM_ID_EN_CFG		0xc80
#define OQ0_INT_SRC_MSK			0xc90
#define COMPL_Q_0_BASE_ADDR_LO		0x4e0
#define COMPL_Q_0_BASE_ADDR_HI		0x4e4
#define COMPL_Q_0_DEPTH			0x4e8
#define COMPL_Q_0_WR_PTR		0x4ec
#define COMPL_Q_0_RD_PTR		0x4f0

/* phy registers need init */
#define PORT_BASE			(0x2000)

#define PHY_CFG				(PORT_BASE + 0x0)
#define HARD_PHY_LINKRATE		(PORT_BASE + 0x4)
#define PHY_CFG_ENA_OFF			0
#define PHY_CFG_ENA_MSK			(0x1 << PHY_CFG_ENA_OFF)
#define PHY_CFG_DC_OPT_OFF		2
#define PHY_CFG_DC_OPT_MSK		(0x1 << PHY_CFG_DC_OPT_OFF)
#define PROG_PHY_LINK_RATE		(PORT_BASE + 0x8)
#define PROG_PHY_LINK_RATE_MAX_OFF	0
#define PROG_PHY_LINK_RATE_MAX_MSK	(0xff << PROG_PHY_LINK_RATE_MAX_OFF)
#define PHY_CTRL			(PORT_BASE + 0x14)
#define PHY_CTRL_RESET_OFF		0
#define PHY_CTRL_RESET_MSK		(0x1 << PHY_CTRL_RESET_OFF)
#define SAS_PHY_CTRL			(PORT_BASE + 0x20)
#define SL_CFG				(PORT_BASE + 0x84)
#define PHY_PCN				(PORT_BASE + 0x44)
#define SL_TOUT_CFG			(PORT_BASE + 0x8c)
#define SL_CONTROL			(PORT_BASE + 0x94)
#define RX_PRIMS_STATUS     (PORT_BASE + 0x98)
#define SL_CONTROL_NOTIFY_EN_OFF	0
#define SL_CONTROL_NOTIFY_EN_MSK	(0x1 << SL_CONTROL_NOTIFY_EN_OFF)
#define SL_CTA_OFF			17
#define SL_CTA_MSK			(0x1 << SL_CTA_OFF)
#define TX_ID_DWORD0			(PORT_BASE + 0x9c)
#define TX_ID_DWORD1			(PORT_BASE + 0xa0)
#define TX_ID_DWORD2			(PORT_BASE + 0xa4)
#define TX_ID_DWORD3			(PORT_BASE + 0xa8)
#define TX_ID_DWORD4			(PORT_BASE + 0xaC)
#define TX_ID_DWORD5			(PORT_BASE + 0xb0)
#define TX_ID_DWORD6			(PORT_BASE + 0xb4)
#define TXID_AUTO				(PORT_BASE + 0xb8)
#define CT3_OFF			1
#define CT3_MSK			(0x1 << CT3_OFF)
#define TX_HARDRST_OFF	2
#define TX_HARDRST_MSK  (0x1 << TX_HARDRST_OFF)
#define RX_IDAF_DWORD0			(PORT_BASE + 0xc4)
#define RX_IDAF_DWORD1			(PORT_BASE + 0xc8)
#define RX_IDAF_DWORD2			(PORT_BASE + 0xcc)
#define RX_IDAF_DWORD3			(PORT_BASE + 0xd0)
#define RX_IDAF_DWORD4			(PORT_BASE + 0xd4)
#define RX_IDAF_DWORD5			(PORT_BASE + 0xd8)
#define RX_IDAF_DWORD6			(PORT_BASE + 0xdc)
#define RXOP_CHECK_CFG_H		(PORT_BASE + 0xfc)
#define STP_CON_CLOSE_REG       (PORT_BASE + 0x10c)
#define PRIM_TOUT_CFG           (PORT_BASE + 0x110)
#define CON_CONTROL				(PORT_BASE + 0x118)
#define DONE_RECEIVED_TIME		(PORT_BASE + 0x11c)
#define CHL_INT0			(PORT_BASE + 0x1b4)
#define CHL_INT0_HOTPLUG_TOUT_OFF	0
#define CHL_INT0_HOTPLUG_TOUT_MSK	(0x1 << CHL_INT0_HOTPLUG_TOUT_OFF)
#define CHL_INT0_SL_RX_BCST_ACK_OFF	1
#define CHL_INT0_SL_RX_BCST_ACK_MSK	(0x1 << CHL_INT0_SL_RX_BCST_ACK_OFF)
#define CHL_INT0_SL_PHY_ENABLE_OFF	2
#define CHL_INT0_SL_PHY_ENABLE_MSK	(0x1 << CHL_INT0_SL_PHY_ENABLE_OFF)
#define CHL_INT0_NOT_RDY_OFF		4
#define CHL_INT0_NOT_RDY_MSK		(0x1 << CHL_INT0_NOT_RDY_OFF)
#define CHL_INT0_PHY_RDY_OFF		5
#define CHL_INT0_PHY_RDY_MSK		(0x1 << CHL_INT0_PHY_RDY_OFF)
#define CHL_INT1			(PORT_BASE + 0x1b8)
#define CHL_INT1_DMAC_TX_ECC_ERR_OFF	15
#define CHL_INT1_DMAC_TX_ECC_ERR_MSK	(0x1 << CHL_INT1_DMAC_TX_ECC_ERR_OFF)
#define CHL_INT1_DMAC_RX_ECC_ERR_OFF	17
#define CHL_INT1_DMAC_RX_ECC_ERR_MSK	(0x1 << CHL_INT1_DMAC_RX_ECC_ERR_OFF)
#define CHL_INT2			(PORT_BASE + 0x1bc)
#define CHL_INT0_MSK			(PORT_BASE + 0x1c0)
#define CHL_INT1_MSK			(PORT_BASE + 0x1c4)
#define CHL_INT2_MSK			(PORT_BASE + 0x1c8)
#define CHL_INT_COAL_EN			(PORT_BASE + 0x1d0)
#define DMA_RX_DFX0             (PORT_BASE + 0x220) /* DMA rx DFX0 */
#define DMA_RX_DFX1             (PORT_BASE + 0x224) /* DMA rx DFX1 */
#define SAS_TX_TRAIN_TIMER0     (PORT_BASE + 0x29c)
#define SAS_TX_TRAIN_TIMER1		(PORT_BASE + 0x2a0)
#define PHY_CTRL_RDY_MSK		(PORT_BASE + 0x2b0)
#define PHYCTRL_NOT_RDY_MSK		(PORT_BASE + 0x2b4)
#define PHYCTRL_DWS_RESET_MSK	(PORT_BASE + 0x2b8)
#define PHYCTRL_PHY_ENA_MSK		(PORT_BASE + 0x2bc)
#define SL_RX_BCAST_CHK_MSK		(PORT_BASE + 0x2c0)
#define PHYCTRL_OOB_RESTART_MSK		(PORT_BASE + 0x2c4)
#define DMA_TX_STATUS			(PORT_BASE + 0x2d0)
#define DMA_TX_STATUS_BUSY_OFF		0
#define DMA_TX_STATUS_BUSY_MSK		(0x1 << DMA_TX_STATUS_BUSY_OFF)
#define DMA_RX_STATUS			(PORT_BASE + 0x2e8)
#define DMA_RX_STATUS_BUSY_OFF		0
#define DMA_RX_STATUS_BUSY_MSK		(0x1 << DMA_RX_STATUS_BUSY_OFF)

#define AXI_CFG				(0x5100)
#define AM_CFG_MAX_TRANS		(0x5010)
#define AM_CFG_SINGLE_PORT_MAX_TRANS	(0x5014)

#define AXI_MASTER_CFG_BASE		(0x5000)
#define AM_CTRL_GLOBAL			(0x0)
#define AM_CURR_TRANS_RETURN	(0x150)

/* HW dma structures */
/* Delivery queue header */
/* dw0 */
#define CMD_HDR_ABORT_FLAG_OFF		0
#define CMD_HDR_ABORT_FLAG_MSK		(0x3 << CMD_HDR_ABORT_FLAG_OFF)
#define CMD_HDR_ABORT_DEVICE_TYPE_OFF	2
#define CMD_HDR_ABORT_DEVICE_TYPE_MSK	(0x1 << CMD_HDR_ABORT_DEVICE_TYPE_OFF)
#define CMD_HDR_RESP_REPORT_OFF		5
#define CMD_HDR_RESP_REPORT_MSK		(1 << CMD_HDR_RESP_REPORT_OFF)
#define CMD_HDR_TLR_CTRL_OFF		6
#define CMD_HDR_TLR_CTRL_MSK		(3 << CMD_HDR_TLR_CTRL_OFF)
#define CMD_HDR_PORT_OFF		18
#define CMD_HDR_PORT_MSK		(0xf << CMD_HDR_PORT_OFF)
#define CMD_HDR_PRIORITY_OFF		27
#define CMD_HDR_PRIORITY_MSK		(0x1 << CMD_HDR_PRIORITY_OFF)
#define CMD_HDR_CMD_OFF			29
#define CMD_HDR_CMD_MSK			(0x7 << CMD_HDR_CMD_OFF)
/* dw1 */
#define CMD_HDR_DIR_OFF			5
#define CMD_HDR_DIR_MSK			(0x3 << CMD_HDR_DIR_OFF)
#define CMD_HDR_RESET_OFF		7
#define CMD_HDR_RESET_MSK		(0x1 << CMD_HDR_RESET_OFF)
#define CMD_HDR_PIR_OFF		8
#define CMD_HDR_PIR_MSK		(0x1 << CMD_HDR_PIR_OFF)
#define CMD_HDR_VDTL_OFF		10
#define CMD_HDR_VDTL_MSK		(0x1 << CMD_HDR_VDTL_OFF)
#define CMD_HDR_FRAME_TYPE_OFF		11
#define CMD_HDR_FRAME_TYPE_MSK		(0x1f << CMD_HDR_FRAME_TYPE_OFF)
#define CMD_HDR_DEV_ID_OFF		16
#define CMD_HDR_DEV_ID_MSK		(0xffff << CMD_HDR_DEV_ID_OFF)
/* dw2 */
#define CMD_HDR_CFL_OFF			0
#define CMD_HDR_CFL_MSK			(0x1ff << CMD_HDR_CFL_OFF)
#define CMD_HDR_NCQ_TAG_OFF		10
#define CMD_HDR_NCQ_TAG_MSK		(0x1f << CMD_HDR_NCQ_TAG_OFF)
#define CMD_HDR_MRFL_OFF		15
#define CMD_HDR_MRFL_MSK		(0x1ff << CMD_HDR_MRFL_OFF)
#define CMD_HDR_SG_MOD_OFF		24
#define CMD_HDR_SG_MOD_MSK		(0x3 << CMD_HDR_SG_MOD_OFF)
/* dw3 */
#define CMD_HDR_IPTT_OFF		0
#define CMD_HDR_IPTT_MSK		(0xffff << CMD_HDR_IPTT_OFF)
/* dw6 */
#define CMD_HDR_DIF_SGL_LEN_OFF		0
#define CMD_HDR_DIF_SGL_LEN_MSK		(0xffff << CMD_HDR_DIF_SGL_LEN_OFF)
#define CMD_HDR_DATA_SGL_LEN_OFF	16
#define CMD_HDR_DATA_SGL_LEN_MSK	(0xffff << CMD_HDR_DATA_SGL_LEN_OFF)
/* dw7 */
#define CMD_HDR_ADDR_MODE_SEL_OFF		15
#define CMD_HDR_ADDR_MODE_SEL_MSK		(1 << CMD_HDR_ADDR_MODE_SEL_OFF)
#define CMD_HDR_ABORT_IPTT_OFF		16
#define CMD_HDR_ABORT_IPTT_MSK		(0xffff << CMD_HDR_ABORT_IPTT_OFF)

/* Completion header */
/* dw0 */
#define CMPLT_HDR_CMPLT_OFF		0
#define CMPLT_HDR_CMPLT_MSK		(0x3 << CMPLT_HDR_CMPLT_OFF)
#define CMPLT_HDR_ERROR_PHASE_OFF   2
#define CPLIT_HDR_ERROR_PHASE_MSK   (0xff << 2)
#define CMPLT_HDR_RSPNS_XFRD_OFF	10
#define CMPLT_HDR_RSPNS_XFRD_MSK	(0x1 << CMPLT_HDR_RSPNS_XFRD_OFF)
#define CMPLT_HDR_RSPNS_GOOD_OFF	11
#define CMPLT_HDR_RSPNS_GOOD_MSK	(0x1 << CMPLT_HDR_RSPNS_GOOD_OFF)
#define CMPLT_HDR_ERX_OFF		12
#define CMPLT_HDR_ERX_MSK		(0x1 << CMPLT_HDR_ERX_OFF)
#define CMPLT_HDR_ABORT_STAT_OFF	13
#define CMPLT_HDR_ABORT_STAT_MSK	(0x7 << CMPLT_HDR_ABORT_STAT_OFF)

/* abort_stat */
#define STAT_IO_NOT_VALID		0x1
#define STAT_IO_NO_DEVICE		0x2
#define STAT_IO_COMPLETE		0x3
#define STAT_IO_ABORTED			0x4
/* dw1 */
#define CMPLT_HDR_IPTT_OFF		0
#define CMPLT_HDR_IPTT_MSK		(0xffff << CMPLT_HDR_IPTT_OFF)
#define CMPLT_HDR_DEV_ID_OFF		16
#define CMPLT_HDR_DEV_ID_MSK		(0xffff << CMPLT_HDR_DEV_ID_OFF)

/* ITCT header */
/* qw0 */
#define ITCT_HDR_DEV_TYPE_OFF		0
#define ITCT_HDR_DEV_TYPE_MSK		(0x3 << ITCT_HDR_DEV_TYPE_OFF)
#define ITCT_HDR_VALID_OFF		2
#define ITCT_HDR_VALID_MSK		(0x1 << ITCT_HDR_VALID_OFF)
#define ITCT_HDR_MCR_OFF		5
#define ITCT_HDR_MCR_MSK		(0xf << ITCT_HDR_MCR_OFF)
#define ITCT_HDR_VLN_OFF		9
#define ITCT_HDR_VLN_MSK		(0xf << ITCT_HDR_VLN_OFF)
#define ITCT_HDR_SMP_TIMEOUT_OFF 16
#define ITCT_HDR_AWT_CONTINUE_OFF 25
#define ITCT_HDR_PORT_ID_OFF		28
#define ITCT_HDR_PORT_ID_MSK		(0xf << ITCT_HDR_PORT_ID_OFF)
/* qw2 */
#define ITCT_HDR_INLT_OFF		0
#define ITCT_HDR_INLT_MSK		(0xffffULL << ITCT_HDR_INLT_OFF)
#define ITCT_HDR_BITLT_OFF		16
#define ITCT_HDR_BITLT_MSK		(0xffffULL << ITCT_HDR_BITLT_OFF)
#define ITCT_HDR_MCTLT_OFF		32
#define ITCT_HDR_MCTLT_MSK		(0xffffULL << ITCT_HDR_MCTLT_OFF)
#define ITCT_HDR_RTOLT_OFF		48
#define ITCT_HDR_RTOLT_MSK		(0xffffULL << ITCT_HDR_RTOLT_OFF)
#define SAS_IO_MAY_REMAIN_IN_DISK   0xFFFF
#define SAS_DISABLE_STP_LINK    1
#define SAS_ENABLE_STP_LINK     2

/*serdes*/
#define DJTAG_MSTR_ADDR			0xd810
#define DJTAG_MSTR_DATA			0xd814
#define DJTAG_MSTR_CFG			0xd818
#define DJTAG_MSTR_START_EN		0xd81c
#define DJTAG_RD_DATA0_REG		0xe800
#define DJTAG_OP_ST_REG			0xe828

#define CRYSTAL_OSCILLATOR_50G  0x101
#define SC_DIE_ID4_OFFSET       0xe014

#define HILINK_DJTAG_SEL		0x30
#define RX_CSR(lane, reg)		(0x4080 + reg * 0x0002 + lane * 0x200)

struct regmap *alg_hccs_subctrl_base = NULL;
spinlock_t serdes_reg_rw_lock;

#define CHAIN_UNIT_CFG_EN_OFF	0
#define CHAIN_UNIT_CFG_EN_MSK	(0xff << CHAIN_UNIT_CFG_EN_OFF)
#define DEBUG_MODULE_SEL_OFF	16
#define DEBUG_MODULE_SEL_MSK	(0x3f << DEBUG_MODULE_SEL_OFF)
#define DJTAG_MSTR_WR_OFF		29
#define DJTAG_NOR_CFG_EN_OFF	30
#define DJTAG_MSTR_DISABLE_OFF	31

#define DJTAG_OP_DONE_OFF		8

#define SAS_CORE_0				0
#define SAS_CORE_1				1
#define SAS_CORE_2				2

#define HILINK2					2
#define HILINK5					5
#define HILINK6					6

#ifdef SAS_DIF
#define PIRF_UDBS_OFF			30
#define PIRF_T10_CHK_EN_OFF		26
#define PIRF_INCR_LBAT_OFF		23
#define PIRF_T10_CHK_MSK_OFF	15
#define PIRF_LBAT_CHK_VAL_OFF	0

struct protect_iu_v2_hw {
	u32 dw0;
	u32 lbrtcv;
	u32 dw2;
	u32 _r_b;
};
#endif

struct hisi_sas_complete_v2_hdr {
	__le32 dw0;
	__le32 dw1;
	__le32 act;
	__le32 dw3;
};

struct hisi_sas_err_record_v2 {
	/* dw0 */
	__le32 trans_tx_fail_type;

	/* dw1 */
	__le32 trans_rx_fail_type;

	/* dw2 */
	__le16 dma_tx_err_type;
	__le16 sipc_rx_err_type;

	/* dw3 */
	__le32 dma_rx_err_type;
};

enum {
	HISI_SAS_PHY_PHY_UPDOWN,
	HISI_SAS_PHY_CHNL_INT,
	HISI_SAS_PHY_INT_NR
};

#define ERR_BIT_OFFSET_V2_HW(n) (((u32)1UL) << ((u32)n))

enum err_dw0 {
	DW0_IO_OPEN_FAIL_I_T_NEXUS_LOSS = ERR_BIT_OFFSET_V2_HW(0),
	DW0_IO_PHY_NOT_ENABLE_ERR = ERR_BIT_OFFSET_V2_HW(1),
	DW0_IO_OPEN_CNX_WRONG_DEST_ERR = ERR_BIT_OFFSET_V2_HW(2),
	DW0_IO_OPEN_CNX_ZONE_VIOLATION_ERR = ERR_BIT_OFFSET_V2_HW(3),
	DW0_IO_OPEN_CNX_BY_OTHERS_ERR = ERR_BIT_OFFSET_V2_HW(4),
	/* BIT5 RESERVED */
	DW0_IO_OPEN_CNX_AIP_TIMEOUT_ERR = ERR_BIT_OFFSET_V2_HW(6),
	DW0_IO_OPEN_CNX_STP_RSC_BUSY_ERR = ERR_BIT_OFFSET_V2_HW(7),
	DW0_IO_OPEN_CNX_PROTO_NOT_SUPPORT_ERR = ERR_BIT_OFFSET_V2_HW(8),
	DW0_IO_OPEN_CNX_CONN_RATE_NOT_SUPPORT_ERR = ERR_BIT_OFFSET_V2_HW(9),
	DW0_IO_OPEN_CNX_BAD_DEST_ERR = ERR_BIT_OFFSET_V2_HW(10),
	DW0_IO_OPEN_CNX_BREAK_RCVD_ERR = ERR_BIT_OFFSET_V2_HW(11),
	DW0_IO_OPEN_CNX_LOW_PHY_POWER_ERR = ERR_BIT_OFFSET_V2_HW(12),
	DW0_IO_OPEN_CNX_PATHWAY_BLOCKED_ERR = ERR_BIT_OFFSET_V2_HW(13),
	DW0_IO_OPEN_CNX_OPEN_TIMEOUT_ERR = ERR_BIT_OFFSET_V2_HW(14),
	DW0_IO_OPEN_CNX_NO_DEST_ERR = ERR_BIT_OFFSET_V2_HW(15),
	DW0_IO_OPEN_RETRY_THRESHOLD_REACHED_ERR = ERR_BIT_OFFSET_V2_HW(16),
	DW0_IO_TX_ERR_FRAME_TXED = ERR_BIT_OFFSET_V2_HW(17),
	DW0_IO_TX_ERR_BREAK_TIMEOUT = ERR_BIT_OFFSET_V2_HW(18),
	DW0_IO_TX_ERR_BREAK_REQ = ERR_BIT_OFFSET_V2_HW(19),
	DW0_IO_TX_ERR_BREAK_RCVD = ERR_BIT_OFFSET_V2_HW(20),
	DW0_IO_TX_ERR_CLOSE_TIMEOUT = ERR_BIT_OFFSET_V2_HW(21),
	DW0_IO_TX_ERR_CLOSE_NORMAL = ERR_BIT_OFFSET_V2_HW(22),
	DW0_IO_TX_ERR_CLOSE_PHY_DISABLE = ERR_BIT_OFFSET_V2_HW(23),
	DW0_IO_TX_ERR_CLOSE_DWS_TIMEOUT = ERR_BIT_OFFSET_V2_HW(24),
	DW0_IO_TX_ERR_CLOSE_COMINIT_RCVD = ERR_BIT_OFFSET_V2_HW(25),
	DW0_IO_TX_ERR_NAK_RCVD = ERR_BIT_OFFSET_V2_HW(26),
	DW0_IO_TX_ERR_ACK_NAK_TIMEOUT = ERR_BIT_OFFSET_V2_HW(27),
	DW0_IO_TX_ERR_R_ERR_RCVD = ERR_BIT_OFFSET_V2_HW(27),
	DW0_IO_TX_ERR_CREDIT_TIMEOUT = ERR_BIT_OFFSET_V2_HW(28),
	DW0_IO_TX_ERR_SATA_DEV_LOST = ERR_BIT_OFFSET_V2_HW(28),
	DW0_IO_IPTT_CONFLICT_ERR = ERR_BIT_OFFSET_V2_HW(29),

	DW0_IO_TX_ERR_OPEN_BY_DEST_OR_OTHERS = ERR_BIT_OFFSET_V2_HW(30),
	DW0_IO_TX_ERR_SYNC_RCVD = ERR_BIT_OFFSET_V2_HW(30),
	DW0_IO_TX_ERR_WAIT_RCVD_TIMEOUT = ERR_BIT_OFFSET_V2_HW(31),

	/*Reserved */
	DW0_IO_ERR_INFO_MAX
};
enum err_dw1 {
	DW1_IO_RX_FRAME_CRC_ERR = ERR_BIT_OFFSET_V2_HW(0),
	DW1_IO_RX_FIS_8B10B_DISP_ERR = ERR_BIT_OFFSET_V2_HW(1),
	DW1_IO_RX_FRAME_ERR_PRIM_ERR = ERR_BIT_OFFSET_V2_HW(2),
	DW1_IO_RX_FIS_8B10B_CODE_ERR = ERR_BIT_OFFSET_V2_HW(2),

	DW1_IO_RX_FIS_DECODE_ERR = ERR_BIT_OFFSET_V2_HW(3),
	DW1_IO_RX_FIS_CRC_ERR = ERR_BIT_OFFSET_V2_HW(4),

	DW1_IO_RX_FRAME_LEN_OVERRUN_ERR = ERR_BIT_OFFSET_V2_HW(5),
	DW1_IO_RX_FIS_TX_SYNCP_ERR = ERR_BIT_OFFSET_V2_HW(5),

	DW1_IO_RX_FIS_RX_SYNCP_ERR = ERR_BIT_OFFSET_V2_HW(6),
	DW1_IO_RX_LINK_BUFF_OVERRUN_ERR = ERR_BIT_OFFSET_V2_HW(7),
	DW1_IO_RX_ERR_BREAK_TIMEOUT = ERR_BIT_OFFSET_V2_HW(8),
	DW1_IO_RX_ERR_BREAK_REQ = ERR_BIT_OFFSET_V2_HW(9),
	DW1_IO_RX_ERR_BREAK_RCVD = ERR_BIT_OFFSET_V2_HW(10),
	/* BIT11 RESERVED */
	DW1_IO_RX_ERR_CLOSE_NORMAL = ERR_BIT_OFFSET_V2_HW(12),
	DW1_IO_RX_ERR_CLOSE_PHY_DISABLE = ERR_BIT_OFFSET_V2_HW(13),
	DW1_IO_RX_ERR_CLOSE_DWS_TIMEOUT = ERR_BIT_OFFSET_V2_HW(14),
	DW1_IO_RX_ERR_CLOSE_COMINIT = ERR_BIT_OFFSET_V2_HW(15),
	DW1_IO_RX_ERR_DATA_LEN_ZERO = ERR_BIT_OFFSET_V2_HW(16),
	DW1_IO_RX_ERR_BAD_HASH = ERR_BIT_OFFSET_V2_HW(17),
	DW1_IO_RX_ERR_FIS_LEN_TOO_SHORT = ERR_BIT_OFFSET_V2_HW(17),

	DW1_IO_RX_ERR_XRDY_WLEN_ZERO = ERR_BIT_OFFSET_V2_HW(18),
	DW1_IO_RX_ERR_FIS_LEN_TOO_LONG = ERR_BIT_OFFSET_V2_HW(18),
	DW1_IO_RX_ERR_SSP_FRAME_LEN_ERR = ERR_BIT_OFFSET_V2_HW(19),
	DW1_IO_RX_ERR_SATA_DEV_LOST = ERR_BIT_OFFSET_V2_HW(19),
	/* BIT20~BIT23 RESERVED */
	DW1_IO_RX_ERR_FRAME_TYPE_ERR = ERR_BIT_OFFSET_V2_HW(24),
	DW1_IO_RX_ERR_SMP_FRAME_LEN_ERR = ERR_BIT_OFFSET_V2_HW(25),
	DW1_IO_RX_ERR_WAIT_SMP_RESP_TIMEOUT = ERR_BIT_OFFSET_V2_HW(26),
	/* BIT27~BIT30 RESERVED */

	DW1_IO_RX_ERR_R_ERR_PRIM_TXD = ERR_BIT_OFFSET_V2_HW(31),
	DW1_IO_ERR_INFO_MAX
};

enum err_dw2 {
	DW2_IO_TX_DIF_CRC_ERR = ERR_BIT_OFFSET_V2_HW(0),
	DW2_IO_TX_DIF_APP_ERR = ERR_BIT_OFFSET_V2_HW(1),
	DW2_IO_TX_DIF_RPP_ERR = ERR_BIT_OFFSET_V2_HW(2),
	DW2_IO_TX_DATA_SGL_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(3),
	DW2_IO_TX_DIF_SGL_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(4),
	DW2_IO_UNEXP_XFER_ERR = ERR_BIT_OFFSET_V2_HW(5),
	DW2_IO_UNEXP_RETRANS_ERR = ERR_BIT_OFFSET_V2_HW(6),
	DW2_IO_TX_XFER_LEN_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(7),
	DW2_IO_TX_XFER_OFFSET_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(8),
	DW2_IO_TX_RAM_ECC_ERR = ERR_BIT_OFFSET_V2_HW(9),
	DW2_IO_TX_DIF_LEN_ALIGN_ERR = ERR_BIT_OFFSET_V2_HW(10),
	/* BIT11~15 RESERVED */

	DW2_IO_RX_FIS_STATUS_ERR_BIT_VLD = ERR_BIT_OFFSET_V2_HW(16),
	DW2_IO_RX_PIO_WRSETUP_STATUS_DRQ_ERR = ERR_BIT_OFFSET_V2_HW(17),
	DW2_IO_RX_FIS_STATUS_BSY_ERR = ERR_BIT_OFFSET_V2_HW(18),
	DW2_IO_RX_WRSETUP_LEN_ODD_ERR = ERR_BIT_OFFSET_V2_HW(19),
	DW2_IO_RX_WRSETUP_LEN_ZERO_ERR = ERR_BIT_OFFSET_V2_HW(20),

	DW2_IO_RX_WRDATA_LEN_NOT_MATCH_ERR = ERR_BIT_OFFSET_V2_HW(21),
	DW2_IO_RX_NCQ_WRSETUP_OFFSET_ERR = ERR_BIT_OFFSET_V2_HW(22),
	DW2_IO_RX_NCQ_WRSETUP_AUTO_ACTIVE_ERR = ERR_BIT_OFFSET_V2_HW(23),

	DW2_RX_SATA_UNEXP_FIS_ERR = ERR_BIT_OFFSET_V2_HW(24),
	DW2_IO_RX_WRSETUP_ESTATUS_ERR = ERR_BIT_OFFSET_V2_HW(25),
	DW2_IO_DATA_UNDERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(26),
	/* BIT27~31 RESERVED */
	DW2_IO_ERR_INFO_MAX
};

enum err_dw3 {
	DW3_IO_RX_DIF_CRC_ERR = ERR_BIT_OFFSET_V2_HW(0),
	DW3_IO_RX_DIF_APP_ERR = ERR_BIT_OFFSET_V2_HW(1),
	DW3_IO_RX_DIF_RPP_ERR = ERR_BIT_OFFSET_V2_HW(2),
	DW3_IO_RX_DATA_SGL_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(3),
	DW3_IO_RX_DIF_SGL_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(4),
	DW3_IO_RX_DATA_LEN_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(5),
	DW3_IO_RX_DATA_LEN_UNDERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(6),
	DW3_IO_RX_DATA_OFFSET_ERR = ERR_BIT_OFFSET_V2_HW(7),
	/* BIT8 RESERVED */
	DW3_IO_RX_SATA_FRAME_TYPE_ERR = ERR_BIT_OFFSET_V2_HW(9),
	DW3_IO_RX_RESP_BUFF_OVERFLOW_ERR = ERR_BIT_OFFSET_V2_HW(10),
	DW3_IO_RX_UNEXP_RETRANS_RESP_ERR = ERR_BIT_OFFSET_V2_HW(11),
	DW3_IO_RX_UNEXP_NORMAL_RESP_ERR = ERR_BIT_OFFSET_V2_HW(12),
	DW3_IO_RX_UNEXP_RDFRAME_ERR = ERR_BIT_OFFSET_V2_HW(13),
	DW3_IO_RX_PIO_DATA_LEN_ERR = ERR_BIT_OFFSET_V2_HW(14),
	DW3_IO_RX_RDSETUP_STATUS_ERR = ERR_BIT_OFFSET_V2_HW(15),
	DW3_IO_RX_RDSETUP_STATUS_DRQ_ERR = ERR_BIT_OFFSET_V2_HW(16),
	DW3_IO_RX_RDSETUP_STATUS_BSY_ERR = ERR_BIT_OFFSET_V2_HW(17),
	DW3_IO_RX_RDSETUP_LEN_ODD_ERR = ERR_BIT_OFFSET_V2_HW(18),
	DW3_IO_RX_RDSETUP_LEN_ZERO_ERR = ERR_BIT_OFFSET_V2_HW(19),
	DW3_IO_RX_RDSETUP_LEN_OVER_ERR = ERR_BIT_OFFSET_V2_HW(20),
	DW3_IO_RX_RDSETUP_OFFSET_ERR = ERR_BIT_OFFSET_V2_HW(21),
	DW3_IO_RX_RDSETUP_ACTIVE_ERR = ERR_BIT_OFFSET_V2_HW(22),
	DW3_IO_RX_RDSETUP_ESTATUS_ERR = ERR_BIT_OFFSET_V2_HW(23),
	DW3_IO_RX_RAM_ECC_ERR = ERR_BIT_OFFSET_V2_HW(24),
	DW3_IO_RX_UNKNOWN_FRAME_ERR = ERR_BIT_OFFSET_V2_HW(25),

	DW3_IO_ERR_INFO_MAX
};

#define ERR_ENCODE_V2_HW(dw, bit_pos) (((dw)<<6)|(bit_pos))
/* hisilicon 1610 io error code */
enum io_error_code_v2_hw {
	/* error from DW0 */
	H_DW0_IO_OPEN_FAIL_I_T_NEXUS_LOSS = ERR_ENCODE_V2_HW(0, 0),
	H_DW0_IO_PHY_NOT_ENABLE_ERR = ERR_ENCODE_V2_HW(0, 1),
	H_DW0_IO_OPEN_CNX_WRONG_DEST_ERR = ERR_ENCODE_V2_HW(0, 2),
	H_DW0_IO_OPEN_CNX_ZONE_VIOLATION_ERR = ERR_ENCODE_V2_HW(0, 3),
	H_DW0_IO_OPEN_CNX_BY_OTHERS_ERR = ERR_ENCODE_V2_HW(0, 4),
	/* BIT5 RESERVED */
	H_DW0_IO_OPEN_CNX_AIP_TIMEOUT_ERR = ERR_ENCODE_V2_HW(0, 6),
	H_DW0_IO_OPEN_CNX_STP_RSC_BUSY_ERR = ERR_ENCODE_V2_HW(0, 7),
	H_DW0_IO_OPEN_CNX_PROTO_NOT_SUPPORT_ERR = ERR_ENCODE_V2_HW(0, 8),
	H_DW0_IO_OPEN_CNX_CONN_RATE_NOT_SUPPORT_ERR =
	    ERR_ENCODE_V2_HW(0, 9),
	H_DW0_IO_OPEN_CNX_BAD_DEST_ERR = ERR_ENCODE_V2_HW(0, 10),
	H_DW0_IO_OPEN_CNX_BREAK_RCVD_ERR = ERR_ENCODE_V2_HW(0, 11),
	H_DW0_IO_OPEN_CNX_LOW_PHY_POWER_ERR = ERR_ENCODE_V2_HW(0, 12),
	H_DW0_IO_OPEN_CNX_PATHWAY_BLOCKED_ERR = ERR_ENCODE_V2_HW(0, 13),
	H_DW0_IO_OPEN_CNX_OPEN_TIMEOUT_ERR = ERR_ENCODE_V2_HW(0, 14),
	H_DW0_IO_OPEN_CNX_NO_DEST_ERR = ERR_ENCODE_V2_HW(0, 15),
	H_DW0_IO_OPEN_RETRY_THRESHOLD_REACHED_ERR = ERR_ENCODE_V2_HW(0, 16),
	H_DW0_IO_TX_ERR_FRAME_TXED = ERR_ENCODE_V2_HW(0, 17),
	H_DW0_IO_TX_ERR_BREAK_TIMEOUT = ERR_ENCODE_V2_HW(0, 18),
	H_DW0_IO_TX_ERR_BREAK_REQ = ERR_ENCODE_V2_HW(0, 19),
	H_DW0_IO_TX_ERR_BREAK_RCVD = ERR_ENCODE_V2_HW(0, 20),
	H_DW0_IO_TX_ERR_CLOSE_TIMEOUT = ERR_ENCODE_V2_HW(0, 21),
	H_DW0_IO_TX_ERR_CLOSE_NORMAL = ERR_ENCODE_V2_HW(0, 22),
	H_DW0_IO_TX_ERR_CLOSE_PHY_DISABLE = ERR_ENCODE_V2_HW(0, 23),
	H_DW0_IO_TX_ERR_CLOSE_DWS_TIMEOUT = ERR_ENCODE_V2_HW(0, 24),
	H_DW0_IO_TX_ERR_CLOSE_COMINIT_RCVD = ERR_ENCODE_V2_HW(0, 25),
	H_DW0_IO_TX_ERR_NAK_RCVD = ERR_ENCODE_V2_HW(0, 26),
	H_DW0_IO_TX_ERR_ACK_NAK_TIMEOUT = ERR_ENCODE_V2_HW(0, 27),
	H_DW0_IO_TX_ERR_R_ERR_RCVD = ERR_ENCODE_V2_HW(0, 27),
	H_DW0_IO_TX_ERR_CREDIT_TIMEOUT = ERR_ENCODE_V2_HW(0, 28),
	H_DW0_IO_TX_ERR_SATA_DEV_LOST = ERR_ENCODE_V2_HW(0, 28),
	H_DW0_IO_IPTT_CONFLICT_ERR = ERR_ENCODE_V2_HW(0, 29),

	H_DW0_IO_TX_ERR_OPEN_BY_DEST_OR_OTHERS = ERR_ENCODE_V2_HW(0, 30),
	H_DW0_IO_TX_ERR_SYNC_RCVD = ERR_ENCODE_V2_HW(0, 30),
	H_DW0_IO_TX_ERR_WAIT_RCVD_TIMEOUT = ERR_ENCODE_V2_HW(0, 31),

	/* error from DW1 */
	H_DW1_IO_RX_FRAME_CRC_ERR = ERR_ENCODE_V2_HW(1, 0),
	H_DW1_IO_RX_FIS_8B10B_DISP_ERR = ERR_ENCODE_V2_HW(1, 1),
	H_DW1_IO_RX_FRAME_ERR_PRIM_ERR = ERR_ENCODE_V2_HW(1, 2),
	H_DW1_IO_RX_FIS_8B10B_CODE_ERR = ERR_ENCODE_V2_HW(1, 2),

	H_DW1_IO_RX_FIS_DECODE_ERR = ERR_ENCODE_V2_HW(1, 3),
	H_DW1_IO_RX_FIS_CRC_ERR = ERR_ENCODE_V2_HW(1, 4),

	H_DW1_IO_RX_FRAME_LEN_OVERRUN_ERR = ERR_ENCODE_V2_HW(1, 5),
	H_DW1_IO_RX_FIS_TX_SYNCP_ERR = ERR_ENCODE_V2_HW(1, 5),

	H_DW1_IO_RX_FIS_RX_SYNCP_ERR = ERR_ENCODE_V2_HW(1, 6),
	H_DW1_IO_RX_LINK_BUFF_OVERRUN_ERR = ERR_ENCODE_V2_HW(1, 7),
	H_DW1_IO_RX_ERR_BREAK_TIMEOUT = ERR_ENCODE_V2_HW(1, 8),
	H_DW1_IO_RX_ERR_BREAK_REQ = ERR_ENCODE_V2_HW(1, 9),
	H_DW1_IO_RX_ERR_BREAK_RCVD = ERR_ENCODE_V2_HW(1, 10),
	/* BIT11 RESERVED */
	H_DW1_IO_RX_ERR_CLOSE_NORMAL = ERR_ENCODE_V2_HW(1, 12),
	H_DW1_IO_RX_ERR_CLOSE_PHY_DISABLE = ERR_ENCODE_V2_HW(1, 13),
	H_DW1_IO_RX_ERR_CLOSE_DWS_TIMEOUT = ERR_ENCODE_V2_HW(1, 14),
	H_DW1_IO_RX_ERR_CLOSE_COMINIT = ERR_ENCODE_V2_HW(1, 15),
	H_DW1_IO_RX_ERR_DATA_LEN_ZERO = ERR_ENCODE_V2_HW(1, 16),
	H_DW1_IO_RX_ERR_BAD_HASH = ERR_ENCODE_V2_HW(1, 17),
	H_DW1_IO_RX_ERR_FIS_LEN_TOO_SHORT = ERR_ENCODE_V2_HW(1, 17),

	H_DW1_IO_RX_ERR_XRDY_WLEN_ZERO = ERR_ENCODE_V2_HW(1, 18),
	H_DW1_IO_RX_ERR_FIS_LEN_TOO_LONG = ERR_ENCODE_V2_HW(1, 18),
	H_DW1_IO_RX_ERR_SSP_FRAME_LEN_ERR = ERR_ENCODE_V2_HW(1, 19),
	H_DW1_IO_RX_ERR_SATA_DEV_LOST = ERR_ENCODE_V2_HW(1, 19),
	/* BIT20~BIT23 RESERVED */
	H_DW1_IO_RX_ERR_FRAME_TYPE_ERR = ERR_ENCODE_V2_HW(1, 24),
	H_DW1_IO_RX_ERR_SMP_FRAME_LEN_ERR = ERR_ENCODE_V2_HW(1, 25),
	H_DW1_IO_RX_ERR_WAIT_SMP_RESP_TIMEOUT = ERR_ENCODE_V2_HW(1, 26),
	/* BIT27~BIT30 RESERVED */

	H_DW1_IO_RX_ERR_R_ERR_PRIM_TXD = ERR_ENCODE_V2_HW(1, 31),

	/* error from DW2 */
	H_DW2_IO_TX_DIF_CRC_ERR = ERR_ENCODE_V2_HW(2, 0),
	H_DW2_IO_TX_DIF_APP_ERR = ERR_ENCODE_V2_HW(2, 1),
	H_DW2_IO_TX_DIF_RPP_ERR = ERR_ENCODE_V2_HW(2, 2),
	H_DW2_IO_TX_DATA_SGL_OVERFLOW_ERR = ERR_ENCODE_V2_HW(2, 3),
	H_DW2_IO_TX_DIF_SGL_OVERFLOW_ERR = ERR_ENCODE_V2_HW(2, 4),
	H_DW2_IO_UNEXP_XFER_ERR = ERR_ENCODE_V2_HW(2, 5),
	H_DW2_IO_UNEXP_RETRANS_ERR = ERR_ENCODE_V2_HW(2, 6),
	H_DW2_IO_TX_XFER_LEN_OVERFLOW_ERR = ERR_ENCODE_V2_HW(2, 7),
	H_DW2_IO_TX_XFER_OFFSET_OVERFLOW_ERR = ERR_ENCODE_V2_HW(2, 8),
	H_DW2_IO_TX_RAM_ECC_ERR = ERR_ENCODE_V2_HW(2, 9),
	H_DW2_IO_TX_DIF_LEN_ALIGN_ERR = ERR_ENCODE_V2_HW(2, 10),
	/* BIT11~15 RESERVED */

	H_DW2_IO_RX_FIS_STATUS_ERR_BIT_VLD = ERR_ENCODE_V2_HW(2, 16),
	H_DW2_IO_RX_PIO_WRSETUP_STATUS_DRQ_ERR = ERR_ENCODE_V2_HW(2, 17),
	H_DW2_IO_RX_FIS_STATUS_BSY_ERR = ERR_ENCODE_V2_HW(2, 18),
	H_DW2_IO_RX_WRSETUP_LEN_ODD_ERR = ERR_ENCODE_V2_HW(2, 19),
	H_DW2_IO_RX_WRSETUP_LEN_ZERO_ERR = ERR_ENCODE_V2_HW(2, 20),

	H_DW2_IO_RX_WRDATA_LEN_NOT_MATCH_ERR = ERR_ENCODE_V2_HW(2, 21),
	H_DW2_IO_RX_NCQ_WRSETUP_OFFSET_ERR = ERR_ENCODE_V2_HW(2, 22),
	H_DW2_IO_RX_NCQ_WRSETUP_AUTO_ACTIVE_ERR = ERR_ENCODE_V2_HW(2, 23),

	H_DW2_RX_SATA_UNEXP_FIS_ERR = ERR_ENCODE_V2_HW(2, 24),
	H_DW2_IO_RX_WRSETUP_ESTATUS_ERR = ERR_ENCODE_V2_HW(2, 25),
	/* FOR SATA */
	H_DW2_IO_DATA_UNDERFLOW_ERR = ERR_ENCODE_V2_HW(2, 26),

	/* error from DW3 */
	H_DW3_IO_RX_DIF_CRC_ERR = ERR_ENCODE_V2_HW(3, 0),
	H_DW3_IO_RX_DIF_APP_ERR = ERR_ENCODE_V2_HW(3, 1),
	H_DW3_IO_RX_DIF_RPP_ERR = ERR_ENCODE_V2_HW(3, 2),
	H_DW3_IO_RX_DATA_SGL_OVERFLOW_ERR = ERR_ENCODE_V2_HW(3, 3),
	H_DW3_IO_RX_DIF_SGL_OVERFLOW_ERR = ERR_ENCODE_V2_HW(3, 4),
	H_DW3_IO_RX_DATA_LEN_OVERFLOW_ERR = ERR_ENCODE_V2_HW(3, 5),
	H_DW3_IO_RX_DATA_LEN_UNDERFLOW_ERR = ERR_ENCODE_V2_HW(3, 6),
	H_DW3_IO_RX_DATA_OFFSET_ERR = ERR_ENCODE_V2_HW(3, 7),
	/* BIT8 RESERVED */
	H_DW3_IO_RX_SATA_FRAME_TYPE_ERR = ERR_ENCODE_V2_HW(3, 9),
	H_DW3_IO_RX_RESP_BUFF_OVERFLOW_ERR = ERR_ENCODE_V2_HW(3, 10),
	H_DW3_IO_RX_UNEXP_RETRANS_RESP_ERR = ERR_ENCODE_V2_HW(3, 11),
	H_DW3_IO_RX_UNEXP_NORMAL_RESP_ERR = ERR_ENCODE_V2_HW(3, 12),
	H_DW3_IO_RX_UNEXP_RDFRAME_ERR = ERR_ENCODE_V2_HW(3, 13),
	H_DW3_IO_RX_PIO_DATA_LEN_ERR = ERR_ENCODE_V2_HW(3, 14),
	H_DW3_IO_RX_RDSETUP_STATUS_ERR = ERR_ENCODE_V2_HW(3, 15),
	H_DW3_IO_RX_RDSETUP_STATUS_DRQ_ERR = ERR_ENCODE_V2_HW(3, 16),
	H_DW3_IO_RX_RDSETUP_STATUS_BSY_ERR = ERR_ENCODE_V2_HW(3, 17),
	H_DW3_IO_RX_RDSETUP_LEN_ODD_ERR = ERR_ENCODE_V2_HW(3, 18),
	H_DW3_IO_RX_RDSETUP_LEN_ZERO_ERR = ERR_ENCODE_V2_HW(3, 19),
	H_DW3_IO_RX_RDSETUP_LEN_OVER_ERR = ERR_ENCODE_V2_HW(3, 20),
	H_DW3_IO_RX_RDSETUP_OFFSET_ERR = ERR_ENCODE_V2_HW(3, 21),
	H_DW3_IO_RX_RDSETUP_ACTIVE_ERR = ERR_ENCODE_V2_HW(3, 22),
	H_DW3_IO_RX_RDSETUP_ESTATUS_ERR = ERR_ENCODE_V2_HW(3, 23),
	H_DW3_IO_RX_RAM_ECC_ERR = ERR_ENCODE_V2_HW(3, 24),
	H_DW3_IO_RX_UNKNOWN_FRAME_ERR = ERR_ENCODE_V2_HW(3, 25),
	H_IO_ERROR_MAX,
};

#define HISI_SAS_COMMAND_ENTRIES_V2_HW 4096

#define DIR_NO_DATA 0
#define DIR_TO_INI 1
#define DIR_TO_DEVICE 2
#define DIR_RESERVED 3

static int interrupt_clear_v2_hw(struct hisi_hba *hisi_hba);

static u32 hisi_sas_read32(struct hisi_hba *hisi_hba, u32 off)
{
	void __iomem *regs = hisi_hba->regs + off;

	return readl(regs);
}

static u32 hisi_sas_read32_relaxed(struct hisi_hba *hisi_hba, u32 off)
{
	void __iomem *regs = hisi_hba->regs + off;

	return readl_relaxed(regs);
}

static void hisi_sas_write32(struct hisi_hba *hisi_hba, u32 off, u32 val)
{
	void __iomem *regs = hisi_hba->regs + off;

	writel(val, regs);
}

static void hisi_sas_phy_write32(struct hisi_hba *hisi_hba, int phy_no,
				 u32 off, u32 val)
{
	void __iomem *regs = hisi_hba->regs + (0x400 * phy_no) + off;

	writel(val, regs);
}

static u32 hisi_sas_phy_read32(struct hisi_hba *hisi_hba,
				      int phy_no, u32 off)
{
	void __iomem *regs = hisi_hba->regs + (0x400 * phy_no) + off;

	return readl(regs);
}

static int get_macro_lane(u32 core_id, u32 phy_no, u32 *macro, u32 *lane)
{
	int ret = -1;

	switch	(core_id) {
	case SAS_CORE_0:
		*macro = HILINK2;
		*lane = phy_no;
		ret = 0;
		break;
	case SAS_CORE_1:
		if (phy_no > 3 && phy_no < 8) {
			*macro = HILINK6;
			*lane = phy_no - 4;
			ret = 0;
		} else if (phy_no >= 0 && phy_no <= 3) {
			*macro = HILINK5;
			*lane = phy_no;
			ret = 0;
		} else {
			ret = -1;
		}
		break;
	case SAS_CORE_2:
		*macro = HILINK6;
		*lane = phy_no;
		ret = 0;
		break;
	default:
		ret = -1;
	}
	return ret;
}

static int hilink_djtag_rw(struct regmap *subctrl_base, u32 module_sel,
					u32 reg, u32 *value, bool rw)
{
	u32 timeout = 0;
	u32 status = 0;
	unsigned long flag = 0;
	u32 djtag_cfg_value, op_stat_value;

	spin_lock_irqsave(&serdes_reg_rw_lock, flag);
	regmap_write(subctrl_base, DJTAG_MSTR_ADDR, reg);
	regmap_read(subctrl_base, DJTAG_MSTR_CFG, &djtag_cfg_value);

	if (rw)
		djtag_cfg_value |= (1 << DJTAG_MSTR_WR_OFF);
	else
		djtag_cfg_value &= ~(1 << DJTAG_MSTR_WR_OFF);

	djtag_cfg_value &= ~(DEBUG_MODULE_SEL_MSK);
	djtag_cfg_value |= (module_sel << DEBUG_MODULE_SEL_OFF) &
		(DEBUG_MODULE_SEL_MSK);

	djtag_cfg_value &= ~(1 << DJTAG_MSTR_DISABLE_OFF);
	djtag_cfg_value &= ~(1 << DJTAG_NOR_CFG_EN_OFF);

	djtag_cfg_value &= ~(CHAIN_UNIT_CFG_EN_MSK);
	djtag_cfg_value |= (1 << CHAIN_UNIT_CFG_EN_OFF) &
		(CHAIN_UNIT_CFG_EN_MSK);

	regmap_write(subctrl_base, DJTAG_MSTR_CFG, djtag_cfg_value);
	if (rw)
		regmap_write(subctrl_base, DJTAG_MSTR_DATA, *value);

	regmap_write(subctrl_base, DJTAG_MSTR_START_EN, 1);

	timeout = 100000;
	do {
		regmap_read(subctrl_base, DJTAG_MSTR_START_EN, &status);
		udelay(1);
		timeout--;
		if (!timeout) {
			pr_info("rw = %d(write=1,read=0) timeout\n", rw);
			spin_unlock_irqrestore(&serdes_reg_rw_lock, flag);
			return -ETIMEDOUT;
		}
	} while	(status);

	timeout = 10000;
	do {
		udelay(10);
		regmap_read(subctrl_base, DJTAG_OP_ST_REG, &op_stat_value);
		timeout--;
		if (!timeout) {
			pr_info("rw = %d DJTAG_OP_ST_REG timeout\n", rw);
			spin_unlock_irqrestore(&serdes_reg_rw_lock, flag);
			return -ETIMEDOUT;
		}
	} while (!(op_stat_value & (1 << DJTAG_OP_DONE_OFF)));

	if (!rw) {
		if (value)
			regmap_read(subctrl_base, DJTAG_RD_DATA0_REG, value);
	}

	spin_unlock_irqrestore(&serdes_reg_rw_lock, flag);
	return 0;
}

void serdes_regbits_write(u32 macro, u32 lane, u32 reg,
			u32 bit, u32 val)
{
	int ret = -1;
	u32 origin_value = 0;
	u32 final_value = 0;

	/*check serdes param*/
	if (macro == HILINK2 || macro == HILINK5 || macro == HILINK6) {
		if ((macro == HILINK2 && lane > 7)
			|| (macro == HILINK5 && lane > 3)
			|| (macro == HILINK6 && lane > 3)) {
			pr_info("macro: %u, lane: %u is invalid\n",
				macro, lane);
			return;
		}
	} else {
		pr_info("macro is invalid: %u\n", macro);
		return;
	}

	ret = hilink_djtag_rw(alg_hccs_subctrl_base,
		HILINK_DJTAG_SEL + macro,
		RX_CSR(lane, reg), &origin_value, 0);
	pr_err("%s %d: origin_value =0x%x\n", __func__, __LINE__, origin_value);
	if (ret) {
		pr_info("Hilink %u, reg %x, read failed!\n", macro, lane);
		return;
	}

	if (val)
		final_value = origin_value | (1 << bit);
	else
		final_value = origin_value & (~(1 << bit));

	ret = hilink_djtag_rw(alg_hccs_subctrl_base,
		HILINK_DJTAG_SEL + macro,
		RX_CSR(lane, reg), &final_value, 1);
	if (ret) {
		pr_info("Hilink %u, reg %x, read failed!\n", macro, lane);
		return;
	}
	ret = hilink_djtag_rw(alg_hccs_subctrl_base,
		HILINK_DJTAG_SEL + macro,
		RX_CSR(lane, reg), &origin_value, 0);
	pr_err("%s %d: write value =0x%x\n", __func__, __LINE__, origin_value);
}

static int hisi_sas_serdes_init(struct platform_device *pdev)
{
	spin_lock_init(&serdes_reg_rw_lock);
	if (pdev->dev.of_node)
		alg_hccs_subctrl_base =
			syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
				"subctrl,sas-syscon");

	if (!alg_hccs_subctrl_base) {
		pr_warn("Alg subctrl base addr remap failed\n");
		return -ENOMEM;
	}
	return 0;
}

static int hisi_sas_serdes_exit(void)
{
	if (alg_hccs_subctrl_base)
		alg_hccs_subctrl_base = NULL;

	return 0;
}

/* This function needs to be protected from pre-emption. */
static int
slot_index_alloc_v2_hw(struct hisi_hba *hisi_hba, int *slot_idx,
		       struct domain_device *device)
{
	unsigned int index = 0;
	void *bitmap = hisi_hba->slot_index_tags;
	int sata_dev = dev_is_sata(device);

	while (1) {
		index = find_next_zero_bit(bitmap, hisi_hba->slot_index_count,
					   index);
		if (index >= hisi_hba->slot_index_count)
			return -SAS_QUEUE_FULL;
		if (sata_dev ^ (index & 1))
			break;
		index++;
	}

	set_bit(index, bitmap);
	*slot_idx = index;
	return 0;
}

static struct hisi_sas_device *alloc_dev_v2_hw(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = device->port->ha->lldd_ha;
	struct hisi_sas_device *sas_dev = NULL;
	int i, sata_dev = dev_is_sata(device);
	unsigned long flags;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		if (sata_dev && (i & 1))
			continue;
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
			pr_err("%s sata_dev=%d i=%d\n", __func__, sata_dev, i);
			hisi_hba->devices[i].device_id = i;
			sas_dev = &hisi_hba->devices[i];
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
			sas_dev->dev_type = device->dev_type;
			sas_dev->hisi_hba = hisi_hba;
			sas_dev->sas_device = device;
			break;
		}
	}
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
	pr_err("%s sas_dev=%p\n", __func__, sas_dev);
	return sas_dev;
}

static void config_phy_opt_mode_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg &= ~PHY_CFG_DC_OPT_MSK;
	cfg |= 1 << PHY_CFG_DC_OPT_OFF;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void config_id_frame_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct sas_identify_frame identify_frame;
	u32 *identify_buffer;

	memset(&identify_frame, 0, sizeof(identify_frame));
	identify_frame.dev_type = SAS_END_DEVICE;
	identify_frame.frame_type = 0;
	identify_frame._un1 = 1;
	identify_frame.initiator_bits = SAS_PROTOCOL_ALL;
	identify_frame.target_bits = SAS_PROTOCOL_NONE;
	memcpy(&identify_frame._un4_11[0], hisi_hba->sas_addr, SAS_ADDR_SIZE);
	memcpy(&identify_frame.sas_addr[0], hisi_hba->sas_addr,	SAS_ADDR_SIZE);
	identify_frame.phy_id = phy_no;
	identify_buffer = (u32 *)(&identify_frame);

	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD0,
			__swab32(identify_buffer[0]));
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD1,
			identify_buffer[2]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD2,
			identify_buffer[1]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD3,
			identify_buffer[4]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD4,
			identify_buffer[3]);
	hisi_sas_phy_write32(hisi_hba, phy_no, TX_ID_DWORD5,
			__swab32(identify_buffer[5]));
}

static void init_id_frame_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		config_id_frame_v2_hw(hisi_hba, i);
}

static void setup_itct_v2_hw(struct hisi_hba *hisi_hba,
			     struct hisi_sas_device *sas_dev)
{
	struct domain_device *device = sas_dev->sas_device;
	struct device *dev = &hisi_hba->pdev->dev;
	u64 qw0, device_id = sas_dev->device_id;
	struct hisi_sas_itct *itct = &hisi_hba->itct[device_id];
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_port *port = device->port->lldd_port;

	memset(itct, 0, sizeof(*itct));

	/* qw0 */
	qw0 = 0;
	switch (sas_dev->dev_type) {
	case SAS_END_DEVICE:
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		qw0 = HISI_SAS_DEV_TYPE_SSP << ITCT_HDR_DEV_TYPE_OFF;
		break;
	case SAS_SATA_DEV:
	case SAS_SATA_PM:
	case SAS_SATA_PM_PORT:
	case SAS_SATA_PENDING:
		if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type))
			qw0 = HISI_SAS_DEV_TYPE_STP << ITCT_HDR_DEV_TYPE_OFF;
		else
			qw0 = HISI_SAS_DEV_TYPE_SATA << ITCT_HDR_DEV_TYPE_OFF;
		break;
	default:
		dev_warn(dev, "setup itct: unsupported dev type (%d)\n",
			 sas_dev->dev_type);
	}

	qw0 |= ((1 << ITCT_HDR_VALID_OFF) |
		(device->linkrate << ITCT_HDR_MCR_OFF) |
		(1 << ITCT_HDR_VLN_OFF) |
		(0Xfa << ITCT_HDR_SMP_TIMEOUT_OFF) |
		(1 << ITCT_HDR_AWT_CONTINUE_OFF) |
		(port->id << ITCT_HDR_PORT_ID_OFF));
	itct->qw0 = cpu_to_le64(qw0);

	/* qw1 */
	memcpy(&itct->sas_addr, device->sas_addr, SAS_ADDR_SIZE);
	itct->sas_addr = __swab64(itct->sas_addr);

	if (!dev_is_sata(device))
		itct->qw2 = cpu_to_le64((5000ULL << ITCT_HDR_INLT_OFF) |
					(0x1ULL << ITCT_HDR_BITLT_OFF) |
					(0x32ULL << ITCT_HDR_MCTLT_OFF) |
					(0x1ULL << ITCT_HDR_RTOLT_OFF));
}

static void free_device_v2_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_device *sas_dev)
{
	u64 dev_id = sas_dev->device_id;
	struct device *dev = &hisi_hba->pdev->dev;
	struct hisi_sas_itct *itct = &hisi_hba->itct[dev_id];
	u32 reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
	int i;

	/* clear the itct interrupt state */
	if (ENT_INT_SRC3_ITC_INT_MSK & reg_val)
		hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
				 ENT_INT_SRC3_ITC_INT_MSK);

	/* clear the itct int*/
	for (i = 0; i < 2; i++) {
		/* clear the itct table*/
		reg_val = hisi_sas_read32(hisi_hba, ITCT_CLR);
		reg_val |= ITCT_CLR_EN_MSK | (dev_id & ITCT_DEV_MSK);
		hisi_sas_write32(hisi_hba, ITCT_CLR, reg_val);

		udelay(10);
		reg_val = hisi_sas_read32(hisi_hba, ENT_INT_SRC3);
		if (ENT_INT_SRC3_ITC_INT_MSK & reg_val) {
			dev_dbg(dev, "got clear ITCT done interrupt\n");

			/* invalid the itct state*/
			memset(itct, 0, sizeof(struct hisi_sas_itct));
			hisi_sas_write32(hisi_hba, ENT_INT_SRC3,
					 ENT_INT_SRC3_ITC_INT_MSK);
			hisi_hba->devices[dev_id].dev_type = SAS_PHY_UNUSED;
			hisi_hba->devices[dev_id].dev_status = HISI_SAS_DEV_NORMAL;

			/* clear the itct */
			hisi_sas_write32(hisi_hba, ITCT_CLR, 0);
			dev_dbg(dev, "clear ITCT ok\n");
			break;
		}
	}
}

static int reset_hw_v2_hw(struct hisi_hba *hisi_hba)
{
	int i, reset_val;
	u32 val;
	unsigned long end_time;
	struct device *dev = &hisi_hba->pdev->dev;

	if (hisi_hba->n_phy == 9)
		reset_val = 0x1fffff;
	else
		reset_val = 0x7ffff;

	/* Disable all of the DQ */
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, 0);

	/* Disable all of the PHYs */
	for (i = 0; i < hisi_hba->n_phy; i++) {
		u32 phy_cfg = hisi_sas_phy_read32(hisi_hba, i, PHY_CFG);

		phy_cfg &= ~PHY_CTRL_RESET_MSK;
		hisi_sas_phy_write32(hisi_hba, i, PHY_CFG, phy_cfg);
	}
	udelay(50);

	/* Ensure DMA tx & rx idle */
	for (i = 0; i < hisi_hba->n_phy; i++) {
		u32 dma_tx_status, dma_rx_status;

		end_time = jiffies + msecs_to_jiffies(1000);

		while (1) {
			dma_tx_status = hisi_sas_phy_read32(hisi_hba, i,
							    DMA_TX_STATUS);
			dma_rx_status = hisi_sas_phy_read32(hisi_hba, i,
							    DMA_RX_STATUS);

			if (!(dma_tx_status & DMA_TX_STATUS_BUSY_MSK) &&
				!(dma_rx_status & DMA_RX_STATUS_BUSY_MSK))
				break;

			msleep(20);
			if (time_after(jiffies, end_time))
				return -EIO;
		}
	}

	/* Ensure axi bus idle */
	end_time = jiffies + msecs_to_jiffies(1000);
	while (1) {
		u32 axi_status =
			hisi_sas_read32(hisi_hba, AXI_CFG);

		if (axi_status == 0)
			break;

		msleep(20);
		if (time_after(jiffies, end_time))
			return -EIO;
	}

	/* reset and disable clock*/
	regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_reset_reg,
			reset_val);
	regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_clock_ena_reg + 4,
			reset_val);
	msleep(1);
	regmap_read(hisi_hba->ctrl, hisi_hba->ctrl_reset_sts_reg, &val);
	if (reset_val != (val & reset_val)) {
		dev_err(dev, "SAS reset fail.\n");
		return -EIO;
	}

	/* De-reset and enable clock*/
	regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_reset_reg + 4,
			reset_val);
	regmap_write(hisi_hba->ctrl, hisi_hba->ctrl_clock_ena_reg,
			reset_val);
	msleep(1);
	regmap_read(hisi_hba->ctrl, hisi_hba->ctrl_reset_sts_reg,
			&val);
	if (val & reset_val) {
		dev_err(dev, "SAS de-reset fail.\n");
		return -EIO;
	}

	return 0;
}

static void init_reg_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	struct device_node *np = dev->of_node;
	int i;
	int macro = 0, lane = 0;
	u32 dieid_value = 0;

	/* Global registers init*/
	if (of_get_property(np, "hip06-sas-v2-quirk-amt", NULL)) {
		hisi_sas_write32(hisi_hba, AM_CFG_MAX_TRANS, 0x2020);
		hisi_sas_write32(hisi_hba, AM_CFG_SINGLE_PORT_MAX_TRANS,
				 0x2020);
		dev_err(dev, "hip06-sas-v2-quirk-amt\n");
	}

	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE,
			 (u32)((1ULL << hisi_hba->queue_count) - 1));
	hisi_sas_write32(hisi_hba, QUEUE_ARB_THRES_VALUE, 1);
	hisi_sas_write32(hisi_hba, HGC_CON_TIME_LIMIT, 0);
	hisi_sas_write32(hisi_hba, AXI_USER1, 0xc0000000);
	hisi_sas_write32(hisi_hba, AXI_USER2, 0x10000);
	hisi_sas_write32(hisi_hba, SAS_HGC_STP_UPD_HDER_CFG, 0x1);
	hisi_sas_write32(hisi_hba, SAS_HGC_SEND_DONE_CFG, 0x3);
	hisi_sas_write32(hisi_hba, SAS_HGC_OPEN_RTY_CNT_CFG, 0x1);
	hisi_sas_write32(hisi_hba, HGC_SAS_TXFAIL_RETRY_CTRL, 0x0);
	hisi_sas_write32(hisi_hba, HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL, 0x7FF);
	hisi_sas_write32(hisi_hba, OPENA_WT_CONTI_TIME, 0x1);
	hisi_sas_write32(hisi_hba, I_T_NEXUS_LOSS_TIME, 0x1F4);
	hisi_sas_write32(hisi_hba, MAX_CON_TIME_LIMIT_TIME, 0x32);
	hisi_sas_write32(hisi_hba, BUS_INACTIVE_LIMIT_TIME, 0x1);
	hisi_sas_write32(hisi_hba, REJECT_TO_OPEN_LIMIT_TIME, 0x1);
	hisi_sas_write32(hisi_hba, CFG_AGING_TIME, 0x1);
	hisi_sas_write32(hisi_hba, HGC_ERR_STAT_EN, 0x401);
	hisi_sas_write32(hisi_hba, INT_COAL_EN, 0xc);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_TIME, 0x60);
	hisi_sas_write32(hisi_hba, OQ_INT_COAL_CNT, 0x3);
	hisi_sas_write32(hisi_hba, ENT_INT_COAL_TIME, 0x1);
	hisi_sas_write32(hisi_hba, ENT_INT_COAL_CNT, 0x1);
	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 0x0);
	hisi_sas_write32(hisi_hba, OQ_INT_SRC_MSK, 0xfffffff0);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0xfefefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0x7efefefe);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0x7ffea0fe);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xfffff3c0);
	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK+0x4*i, 0);

	hisi_sas_write32(hisi_hba, AXI_AHB_CLK_CFG, 1);
	hisi_sas_write32(hisi_hba, HYPER_STREAM_ID_EN_CFG, 1);

	regmap_read(alg_hccs_subctrl_base, SC_DIE_ID4_OFFSET,
		&dieid_value);
	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_write32(hisi_hba, i, PROG_PHY_LINK_RATE, 0x855);
		hisi_sas_phy_write32(hisi_hba, i, SAS_PHY_CTRL, 0x30b9908);
		hisi_sas_phy_write32(hisi_hba, i, SL_TOUT_CFG, 0x7d7d7d7d);
		hisi_sas_phy_write32(hisi_hba, i, SL_CONTROL, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, TXID_AUTO, 0x2);
		hisi_sas_phy_write32(hisi_hba, i, STP_CON_CLOSE_REG, 0x040000);
		hisi_sas_phy_write32(hisi_hba, i, PRIM_TOUT_CFG, 0x007d7d7d);
		hisi_sas_phy_write32(hisi_hba, i, DONE_RECEIVED_TIME, 0x8);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, RXOP_CHECK_CFG_H, 0x1000);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0x8ffffbff);
		hisi_sas_phy_write32(hisi_hba, i, SL_CFG, 0x23f801fc);
		hisi_sas_phy_write32(hisi_hba, i, SAS_TX_TRAIN_TIMER0, 0x7a120);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_NOT_RDY_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_DWS_RESET_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_PHY_ENA_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, SL_RX_BCAST_CHK_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT_COAL_EN, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHYCTRL_OOB_RESTART_MSK, 0x0);
		hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL, 0x199B5f4);
		if (!(CRYSTAL_OSCILLATOR_50G == ((dieid_value & 0xfff00)>>8)))
			hisi_sas_phy_write32(hisi_hba, i, PHY_CTRL, 0x199B694);

		if (!get_macro_lane(hisi_hba->core_id, i, &macro, &lane))
			serdes_regbits_write(macro, lane, 60, 15, 0x1);
		else
			pr_info("%s get_macro_lane fail macro: %d lane: %d\n",
				__func__, macro, lane);
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		/* Delivery queue */
		hisi_sas_write32(hisi_hba,
				 DLVRY_Q_0_BASE_ADDR_HI + (i * 0x14),
				 upper_32_bits(hisi_hba->cmd_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, DLVRY_Q_0_BASE_ADDR_LO + (i * 0x14),
				 lower_32_bits(hisi_hba->cmd_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, DLVRY_Q_0_DEPTH + (i * 0x14),
				 HISI_SAS_QUEUE_SLOTS);

		/* Completion queue */
		hisi_sas_write32(hisi_hba, COMPL_Q_0_BASE_ADDR_HI + (i * 0x14),
				 upper_32_bits(hisi_hba->complete_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, COMPL_Q_0_BASE_ADDR_LO + (i * 0x14),
				 lower_32_bits(hisi_hba->complete_hdr_dma[i]));

		hisi_sas_write32(hisi_hba, COMPL_Q_0_DEPTH + (i * 0x14),
				 HISI_SAS_QUEUE_SLOTS);
	}

	/* itct */
	hisi_sas_write32(hisi_hba, ITCT_BASE_ADDR_LO,
			 lower_32_bits(hisi_hba->itct_dma));

	hisi_sas_write32(hisi_hba, ITCT_BASE_ADDR_HI,
			 upper_32_bits(hisi_hba->itct_dma));

	/* iost */
	hisi_sas_write32(hisi_hba, IOST_BASE_ADDR_LO,
			 lower_32_bits(hisi_hba->iost_dma));

	hisi_sas_write32(hisi_hba, IOST_BASE_ADDR_HI,
			 upper_32_bits(hisi_hba->iost_dma));

	/* breakpoint */
	hisi_sas_write32(hisi_hba, IO_BROKEN_MSG_ADDR_LO,
			 lower_32_bits(hisi_hba->breakpoint_dma));

	hisi_sas_write32(hisi_hba, IO_BROKEN_MSG_ADDR_HI,
			 upper_32_bits(hisi_hba->breakpoint_dma));

	/* SATA broken msg */
	hisi_sas_write32(hisi_hba, IO_SATA_BROKEN_MSG_ADDR_LO,
			 lower_32_bits(hisi_hba->sata_breakpoint_dma));

	hisi_sas_write32(hisi_hba, IO_SATA_BROKEN_MSG_ADDR_HI,
			 upper_32_bits(hisi_hba->sata_breakpoint_dma));

	/* SATA initial fis */
	hisi_sas_write32(hisi_hba, SATA_INITI_D2H_STORE_ADDR_LO,
			 lower_32_bits(hisi_hba->initial_fis_dma));

	hisi_sas_write32(hisi_hba, SATA_INITI_D2H_STORE_ADDR_HI,
			 upper_32_bits(hisi_hba->initial_fis_dma));
}

void hisi_sas_link_timedout(unsigned long data)
{
	static int stpdisable_flag = SAS_DISABLE_STP_LINK;
	struct hisi_hba *hisi_hba = (struct hisi_hba *)data;
	int i = 0;
	u32 tmp;

	if (stpdisable_flag == SAS_DISABLE_STP_LINK) {
		tmp = hisi_sas_read32(hisi_hba, PHY_STATE);
		for (i = 0; i < hisi_hba->n_phy; i++) {
			if (tmp & BIT(i)) {
				hisi_sas_phy_write32(hisi_hba, i,
						CON_CONTROL, 0x6);
				break;
			}
		}

		mod_timer(&hisi_hba->link_timer,
			jiffies + msecs_to_jiffies(100));
		stpdisable_flag = SAS_ENABLE_STP_LINK;
		return;
	}

	for (i = 0; i < hisi_hba->n_phy; i++) {
		tmp = hisi_sas_phy_read32(hisi_hba, i, CON_CONTROL);
		if (!(tmp & BIT(0))) {
			hisi_sas_phy_write32(hisi_hba, i,
				CON_CONTROL, 0x7);
			break;
		}

		stpdisable_flag = SAS_DISABLE_STP_LINK;
	}

	mod_timer(&hisi_hba->link_timer, jiffies + msecs_to_jiffies(900));
}

void set_link_timer(struct hisi_hba *hisi_hba)
{
	init_timer(&hisi_hba->link_timer);
	hisi_hba->link_timer.data = (unsigned long)hisi_hba;
	hisi_hba->link_timer.function = hisi_sas_link_timedout;
	hisi_hba->link_timer.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&hisi_hba->link_timer);
}

static int hw_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	int rc;

	rc = reset_hw_v2_hw(hisi_hba);
	if (rc) {
		dev_err(dev, "hisi_sas_reset_hw failed, rc=%d", rc);
		return rc;
	}

	msleep(100);

	/* clear old interrupt */
	interrupt_clear_v2_hw(hisi_hba);

	init_reg_v2_hw(hisi_hba);

	init_id_frame_v2_hw(hisi_hba);

	set_link_timer(hisi_hba);

	return 0;
}

static void enable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg |= PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void disable_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 cfg = hisi_sas_phy_read32(hisi_hba, phy_no, PHY_CFG);

	cfg &= ~PHY_CFG_ENA_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, PHY_CFG, cfg);
}

static void start_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	config_id_frame_v2_hw(hisi_hba, phy_no);
	config_phy_opt_mode_v2_hw(hisi_hba, phy_no);
	enable_phy_v2_hw(hisi_hba, phy_no);
}

static void stop_phy_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	disable_phy_v2_hw(hisi_hba, phy_no);
}

static void phy_hard_reset_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	u32 txid_auto;

	stop_phy_v2_hw(hisi_hba, phy_no);
	if (phy->identify.device_type == SAS_END_DEVICE) {
		txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
		hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
			txid_auto | TX_HARDRST_MSK);
	}
	msleep(100);
	start_phy_v2_hw(hisi_hba, phy_no);
}

static void start_phys_v2_hw(unsigned long data)
{
	struct hisi_hba *hisi_hba = (struct hisi_hba *)data;
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		start_phy_v2_hw(hisi_hba, i);
}

static void stop_phys_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		stop_phy_v2_hw(hisi_hba, i);
}

static void phys_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct timer_list *timer = &hisi_hba->timer;

	setup_timer(timer, start_phys_v2_hw, (unsigned long)hisi_hba);
	mod_timer(timer, jiffies + HZ);
}

static void sl_notify_v2_hw(struct hisi_hba *hisi_hba, int phy_no)
{
	u32 sl_control;

	sl_control = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	sl_control |= SL_CONTROL_NOTIFY_EN_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL, sl_control);
	msleep(1);
	sl_control = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	sl_control &= ~SL_CONTROL_NOTIFY_EN_MSK;
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL, sl_control);
}

static int get_wideport_bitmap_v2_hw(struct hisi_hba *hisi_hba, int port_id)
{
	int i, bitmap = 0;
	u32 phy_port_num_ma = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
	u32 phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);

	for (i = 0; i < (hisi_hba->n_phy < 9 ? hisi_hba->n_phy : 8); i++)
		if (phy_state & 1 << i)
			if (((phy_port_num_ma >> (i * 4)) & 0xf) == port_id)
				bitmap |= 1 << i;

	if (hisi_hba->n_phy == 9) {
		u32 port_state = hisi_sas_read32(hisi_hba, PORT_STATE);

		if (phy_state & 1 << 8)
			if (((port_state & PORT_STATE_PHY8_PORT_NUM_MSK) >>
			     PORT_STATE_PHY8_PORT_NUM_OFF) == port_id)
				bitmap |= 1 << 9;
	}

	return bitmap;
}

/**
 * This function allocates across all queues to load balance.
 * Slots are allocated from queues in a round-robin fashion.
 *
 * The callpath to this function and upto writing the write
 * queue pointer should be safe from interruption.
 */
static int get_free_slot_v2_hw(struct hisi_hba *hisi_hba,
				u32 dev_id, int *q, int *s)
{
	u32 r, w;
	int queue = dev_id % hisi_hba->queue_count;
	struct hisi_sas_dq *dq = &hisi_hba->dq[queue];

	while (1) {
		w = dq->wr_point;
		r = hisi_sas_read32_relaxed(hisi_hba, DLVRY_Q_0_RD_PTR
				+ (queue * 0x14));

		if (r == w+1 % HISI_SAS_QUEUE_SLOTS) {
			dev_warn(&hisi_hba->pdev->dev, "%s full queue=%d r=%d w=%d\n",
				 __func__, queue, r, w);

			return -1;
		}
		break;
	}

	*q = queue;
	*s = w;
	return 0;
}

static void start_delivery_v2_hw(struct hisi_hba *hisi_hba, int dq_id)
{
	int dlvry_queue = hisi_hba->slot_prep[dq_id]->dlvry_queue;
	int dlvry_queue_slot = hisi_hba->slot_prep[dq_id]->dlvry_queue_slot;

	struct hisi_sas_dq *dq = &hisi_hba->dq[dlvry_queue];

	dq->wr_point = ++dlvry_queue_slot % HISI_SAS_QUEUE_SLOTS;
	hisi_sas_write32(hisi_hba, DLVRY_Q_0_WR_PTR + (dlvry_queue * 0x14),
			 dq->wr_point);

}

static int prep_prd_sge_v2_hw(struct hisi_hba *hisi_hba,
			      struct hisi_sas_slot *slot,
			      struct hisi_sas_cmd_hdr *hdr,
			      struct scatterlist *scatter,
			      int n_elem)
{
	struct device *dev = &hisi_hba->pdev->dev;
	struct scatterlist *sg;
	int i;

	if (n_elem > HISI_SAS_SGE_PAGE_CNT) {
		dev_err(dev, "prd err: n_elem(%d) > HISI_SAS_SGE_PAGE_CNT",
			n_elem);
		return -EINVAL;
	}


	for_each_sg(scatter, sg, n_elem, i) {
		struct hisi_sas_sge *entry = &slot->sge_page->sge[i];

		entry->addr = cpu_to_le64(sg_dma_address(sg));
		entry->page_ctrl_0 = entry->page_ctrl_1 = 0;
		entry->data_len = cpu_to_le32(sg_dma_len(sg));
		entry->data_off = 0;
	}

	hdr->prd_table_addr = cpu_to_le64(slot->sge_page_dma);

	hdr->sg_len |= cpu_to_le32(n_elem << CMD_HDR_DATA_SGL_LEN_OFF);

	return 0;
}

#ifdef SAS_DIF
static int prep_prd_sge_dif_v2_hw(struct hisi_hba *hisi_hba,
				 struct hisi_sas_slot *slot,
				 struct hisi_sas_cmd_hdr *hdr,
				 struct scatterlist *scatter,
				 int n_elem)
{
	struct device *dev = &hisi_hba->pdev->dev;
	struct scatterlist *sg;
	int i;

	if (n_elem > HISI_SAS_SGE_PAGE_CNT) {
		dev_err(dev, "%s n_elem(%d) > HISI_SAS_SGE_PAGE_CNT",
			__func__, n_elem);
		return -EINVAL;
	}

	hdr->dw7 |= 1 << CMD_HDR_ADDR_MODE_SEL_OFF;

	for_each_sg(scatter, sg, n_elem, i) {
		struct hisi_sas_sge *entry = &slot->sge_dif_page->sge[i];

		entry->addr = cpu_to_le64(sg_dma_address(sg));
		entry->page_ctrl_0 = entry->page_ctrl_1 = 0;
		entry->data_len = sg_dma_len(sg);
		entry->data_off = 0;
	}

	hdr->dif_prd_table_addr = cpu_to_le64(slot->sge_dif_page_dma);
	hdr->sg_len |= n_elem;

	return 0;
}
#endif

static int prep_smp_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct device *dev = &hisi_hba->pdev->dev;
	struct hisi_sas_port *port = slot->port;
	struct scatterlist *sg_req, *sg_resp;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	dma_addr_t req_dma_addr;
	unsigned int req_len, resp_len;
	int elem, rc;

	/*
	* DMA-map SMP request, response buffers
	*/
	/* req */
	sg_req = &task->smp_task.smp_req;
	elem = dma_map_sg(dev, sg_req, 1, DMA_TO_DEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);
	req_dma_addr = sg_dma_address(sg_req);

	/* resp */
	sg_resp = &task->smp_task.smp_resp;
	elem = dma_map_sg(dev, sg_resp, 1, DMA_FROM_DEVICE);
	if (!elem) {
		rc = -ENOMEM;
		goto err_out_req;
	}
	resp_len = sg_dma_len(sg_resp);
	if ((req_len & 0x3) || (resp_len & 0x3)) {
		rc = -EINVAL;
		goto err_out_resp;
	}

	/* create header */
	/* dw0 */
	hdr->dw0 = cpu_to_le32((port->id << CMD_HDR_PORT_OFF) |
			       (1 << CMD_HDR_PRIORITY_OFF) | /* high pri */
			       (2 << CMD_HDR_CMD_OFF)); /* smp */

	/* map itct entry */
	hdr->dw1 = cpu_to_le32((sas_dev->device_id << CMD_HDR_DEV_ID_OFF) |
				(1 << CMD_HDR_FRAME_TYPE_OFF) |
				(DIR_NO_DATA << CMD_HDR_DIR_OFF));

	/* dw2 */
	hdr->dw2 = cpu_to_le32((((req_len - 4) / 4) << CMD_HDR_CFL_OFF) |
			       (HISI_SAS_MAX_SMP_RESP_SZ / 4 <<
			       CMD_HDR_MRFL_OFF));

	hdr->transfer_tags = cpu_to_le32(slot->idx << CMD_HDR_IPTT_OFF);

	hdr->cmd_table_addr = cpu_to_le64(req_dma_addr);
	hdr->sts_buffer_addr = cpu_to_le64(slot->status_buffer_dma);

	return 0;

err_out_resp:
	dma_unmap_sg(dev, &slot->task->smp_task.smp_resp, 1,
		     DMA_FROM_DEVICE);
err_out_req:
	dma_unmap_sg(dev, &slot->task->smp_task.smp_req, 1,
		     DMA_TO_DEVICE);
	return rc;
}

static int prep_ssp_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot, int is_tmf,
			  struct hisi_sas_tmf_task *tmf)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port = slot->port;
	struct sas_ssp_task *ssp_task = &task->ssp_task;
	struct scsi_cmnd *scsi_cmnd = ssp_task->cmd;
	int has_data = 0, rc, priority = is_tmf;
	u8 *buf_cmd;
	u32 dw1 = 0, dw2 = 0;

	if (!is_tmf && scsi_cmnd && scsi_cmnd->retries)
		priority = 1;

#ifdef SAS_DIF
	if (!is_tmf) {
		u8 prot_type = scsi_get_prot_type(scsi_cmnd);
		u8 prot_op = scsi_get_prot_op(scsi_cmnd);
		u32 lbat_chk_val = (u32)(0xffffffff & scsi_get_lba(scsi_cmnd));
		struct device *dev = &hisi_hba->pdev->dev;
		union hisi_sas_command_table *cmd =
			(union hisi_sas_command_table *) slot->command_table;
		struct protect_iu_v2_hw *prot =
			(struct protect_iu_v2_hw *)&cmd->ssp.u.prot;

		if (prot_type != SCSI_PROT_DIF_TYPE0) {
			dw1 |= 1 << CMD_HDR_PIR_OFF;

			prot->dw0 |= (1 << PIRF_INCR_LBAT_OFF) |
			 ((scsi_prot_interval(scsi_cmnd) / 4) << PIRF_UDBS_OFF);

			if (prot_op == SCSI_PROT_WRITE_PASS) {
				prot->dw0 |= (1 << PIRF_T10_CHK_EN_OFF);
				prot->lbrtcv |= lbat_chk_val;
			} else if (prot_op == SCSI_PROT_READ_PASS) {
				prot->dw0 |= (1 << PIRF_T10_CHK_EN_OFF) |
					(0xfc << PIRF_T10_CHK_MSK_OFF);
			}

			if (scsi_prot_sg_count(scsi_cmnd)) {
				int n_elem = dma_map_sg(dev,
						scsi_prot_sglist(scsi_cmnd),
						scsi_prot_sg_count(scsi_cmnd),
						task->data_dir);
				if (!n_elem) {
					rc = -ENOMEM;
					return rc;
				}

				rc = prep_prd_sge_dif_v2_hw(hisi_hba, slot, hdr,
						scsi_prot_sglist(scsi_cmnd),
						n_elem);
				if (rc)
					return rc;
			}
		}
	}
#endif

	/* create header */
	hdr->dw0 = cpu_to_le32((1 << CMD_HDR_RESP_REPORT_OFF) |
			       (0x2 << CMD_HDR_TLR_CTRL_OFF) |
			       (port->id << CMD_HDR_PORT_OFF) |
			       (priority << CMD_HDR_PRIORITY_OFF) |
			       (1 << CMD_HDR_CMD_OFF));

	dw1 |= 1 << CMD_HDR_VDTL_OFF;
	if (is_tmf) {
		dw1 |= 2 << CMD_HDR_FRAME_TYPE_OFF;
		dw1 |= DIR_NO_DATA << CMD_HDR_DIR_OFF;
	} else {
		dw1 |= 1 << CMD_HDR_FRAME_TYPE_OFF;
		switch (scsi_cmnd->sc_data_direction) {
		case DMA_TO_DEVICE:
			has_data = 1;
			dw1 |= DIR_TO_DEVICE << CMD_HDR_DIR_OFF;
			break;
		case DMA_FROM_DEVICE:
			has_data = 1;
			dw1 |= DIR_TO_INI << CMD_HDR_DIR_OFF;
			break;
		default:
			dw1 &= ~CMD_HDR_DIR_MSK;
		}
		hdr->data_transfer_len =
			cpu_to_le32(scsi_transfer_length(scsi_cmnd));
	}

	/* map itct entry */
	dw1 |= sas_dev->device_id << CMD_HDR_DEV_ID_OFF;
	hdr->dw1 = cpu_to_le32(dw1);

	dw2 = (((sizeof(struct ssp_command_iu) +
		sizeof(struct ssp_frame_hdr) + 3) / 4) << CMD_HDR_CFL_OFF) |
		(HISI_SAS_MAX_SSP_RESP_SZ / 4) << CMD_HDR_MRFL_OFF |
		(2 << CMD_HDR_SG_MOD_OFF);
	hdr->dw2 = cpu_to_le32(dw2);

	hdr->transfer_tags = cpu_to_le32(slot->idx);

	if (has_data) {
		rc = prep_prd_sge_v2_hw(hisi_hba, slot, hdr, task->scatter,
					slot->n_elem);
		if (rc)
			return rc;
	}

	hdr->cmd_table_addr = cpu_to_le64(slot->command_table_dma);
	hdr->sts_buffer_addr = cpu_to_le64(slot->status_buffer_dma);

	buf_cmd = slot->command_table + sizeof(struct ssp_frame_hdr);

	memcpy(buf_cmd, &task->ssp_task.LUN, 8);
	if (!is_tmf) {
		buf_cmd[9] = task->ssp_task.task_attr |
				(task->ssp_task.task_prio << 3);
		memcpy(buf_cmd + 12, task->ssp_task.cmd->cmnd,
				task->ssp_task.cmd->cmd_len);
	} else {
		buf_cmd[10] = tmf->tmf;
		switch (tmf->tmf) {
		case TMF_ABORT_TASK:
		case TMF_QUERY_TASK:
			buf_cmd[12] =
				(tmf->tag_of_task_to_be_managed >> 8) & 0xff;
			buf_cmd[13] =
				tmf->tag_of_task_to_be_managed & 0xff;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void sata_done_v2_hw(struct hisi_hba *hisi_hba, struct sas_task *task,
			    struct hisi_sas_slot *slot)
{
	struct task_status_struct *ts = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)ts->buf;
	struct dev_to_host_fis *d2h = slot->status_buffer +
			sizeof(struct hisi_sas_err_record);

	resp->frame_len = sizeof(struct dev_to_host_fis);
	memcpy(&resp->ending_fis[0], d2h, sizeof(struct dev_to_host_fis));

	ts->buf_valid_size = sizeof(*resp);
}

#define HISI_MAX_BIT_POS    (32)
static u32 hisi_parse_err_dw(u32 err_dw, u32 *err_prio, u32 prio_lvl)
{
	int i;
	u32 tmp;
	u32 bit_pos = 0;

	for (i = 0; i < prio_lvl; i++)
		if ((err_dw & err_prio[i]) == err_prio[i])
			break;

	if (i == prio_lvl)
		return HISI_MAX_BIT_POS;

	tmp = err_prio[i];
	while (1) {
		if (tmp & 0x1)
			break;
		tmp >>= 1;
		bit_pos++;
	}

	return bit_pos;
}

static u32 hisi_parse_err_info(u32 *err)
{
	u32 *err_dw = (u32 *) err;
	u8 bit_ops;
	u8 dw_no;
	/*
	 * index 0~N, priority from high to low,
	 * i hope our stack is large enough.
	 * Here i use local varibles because this function
	 * maybe called in multicore
	 */
	u32 err_dw3_prio[] = {
		DW3_IO_RX_UNKNOWN_FRAME_ERR,
		DW3_IO_RX_DATA_LEN_OVERFLOW_ERR,
		DW3_IO_RX_DATA_LEN_UNDERFLOW_ERR,

		DW3_IO_RX_DATA_OFFSET_ERR,
		/* BIT8 RESERVED */
		DW3_IO_RX_SATA_FRAME_TYPE_ERR,
		DW3_IO_RX_RESP_BUFF_OVERFLOW_ERR,
		DW3_IO_RX_UNEXP_RETRANS_RESP_ERR,
		DW3_IO_RX_UNEXP_NORMAL_RESP_ERR,
		DW3_IO_RX_UNEXP_RDFRAME_ERR,
		DW3_IO_RX_PIO_DATA_LEN_ERR,
		DW3_IO_RX_RDSETUP_STATUS_ERR,
		DW3_IO_RX_RDSETUP_STATUS_DRQ_ERR,
		DW3_IO_RX_RDSETUP_STATUS_BSY_ERR,
		DW3_IO_RX_RDSETUP_LEN_ODD_ERR,
		DW3_IO_RX_RDSETUP_LEN_ZERO_ERR,
		DW3_IO_RX_RDSETUP_LEN_OVER_ERR,
		DW3_IO_RX_RDSETUP_OFFSET_ERR,
		DW3_IO_RX_RDSETUP_ACTIVE_ERR,
		DW3_IO_RX_RDSETUP_ESTATUS_ERR,

		DW3_IO_RX_RAM_ECC_ERR,
		DW3_IO_RX_DIF_CRC_ERR,
		DW3_IO_RX_DIF_APP_ERR,
		DW3_IO_RX_DIF_RPP_ERR,
		DW3_IO_RX_DATA_SGL_OVERFLOW_ERR,
		DW3_IO_RX_DIF_SGL_OVERFLOW_ERR,
	};
	u32 err_dw3_lvl = sizeof(err_dw3_prio) / sizeof(u32);

	u32 err_dw2_prio[] = {
		/* RX(BIT31~ BIT16) */
		DW2_IO_RX_FIS_STATUS_ERR_BIT_VLD,
		DW2_IO_RX_PIO_WRSETUP_STATUS_DRQ_ERR,
		DW2_IO_RX_FIS_STATUS_BSY_ERR,
		DW2_IO_RX_WRSETUP_LEN_ODD_ERR,
		DW2_IO_RX_WRSETUP_LEN_ZERO_ERR,

		DW2_IO_RX_WRDATA_LEN_NOT_MATCH_ERR,
		DW2_IO_RX_NCQ_WRSETUP_OFFSET_ERR,
		DW2_IO_RX_NCQ_WRSETUP_AUTO_ACTIVE_ERR,

		DW2_RX_SATA_UNEXP_FIS_ERR,
		DW2_IO_RX_WRSETUP_ESTATUS_ERR,
		DW2_IO_DATA_UNDERFLOW_ERR,

		/* TX(BIT15~ BIT0) */
		DW2_IO_UNEXP_XFER_ERR,
		DW2_IO_UNEXP_RETRANS_ERR,
		DW2_IO_TX_XFER_LEN_OVERFLOW_ERR,
		DW2_IO_TX_XFER_OFFSET_OVERFLOW_ERR,
		DW2_IO_TX_RAM_ECC_ERR,
		DW2_IO_TX_DIF_LEN_ALIGN_ERR,

		DW2_IO_TX_DIF_CRC_ERR,
		DW2_IO_TX_DIF_APP_ERR,
		DW2_IO_TX_DIF_RPP_ERR,
		DW2_IO_TX_DATA_SGL_OVERFLOW_ERR,
		DW2_IO_TX_DIF_SGL_OVERFLOW_ERR,

	};
	u32 err_dw2_lvl = sizeof(err_dw2_prio) / sizeof(u32);

	u32 err_dw1_prio[] = {
		/* high prio */
		DW1_IO_RX_FRAME_CRC_ERR,
		DW1_IO_RX_FIS_8B10B_DISP_ERR,
		DW1_IO_RX_FIS_8B10B_CODE_ERR,
		DW1_IO_RX_FIS_DECODE_ERR,
		DW1_IO_RX_FIS_CRC_ERR,
		DW1_IO_RX_FIS_TX_SYNCP_ERR,
		DW1_IO_RX_FIS_RX_SYNCP_ERR,
		DW1_IO_RX_LINK_BUFF_OVERRUN_ERR,

		/* middle prio */
		DW1_IO_RX_ERR_CLOSE_PHY_DISABLE,
		DW1_IO_RX_ERR_CLOSE_DWS_TIMEOUT,
		DW1_IO_RX_ERR_CLOSE_COMINIT,
		DW1_IO_RX_ERR_BREAK_TIMEOUT,
		DW1_IO_RX_ERR_BREAK_REQ,
		DW1_IO_RX_ERR_BREAK_RCVD,
		/* BIT11 reserved, fixed to me, c00308265 */

		DW1_IO_RX_ERR_CLOSE_NORMAL,

		/* low prio */
		DW1_IO_RX_ERR_DATA_LEN_ZERO,
		DW1_IO_RX_ERR_FIS_LEN_TOO_SHORT,
		DW1_IO_RX_ERR_FIS_LEN_TOO_LONG,
		DW1_IO_RX_ERR_SATA_DEV_LOST,
		/* BIT20~BIT23 RESERVED */
		DW1_IO_RX_ERR_FRAME_TYPE_ERR,
		DW1_IO_RX_ERR_SMP_FRAME_LEN_ERR,

		DW1_IO_RX_ERR_WAIT_SMP_RESP_TIMEOUT,
		DW1_IO_RX_ERR_R_ERR_PRIM_TXD,	/* BIT31 sata error info */

	};
	u32 err_dw1_lvl = sizeof(err_dw1_prio) / sizeof(u32);

	u32 err_dw0_prio[] = {
		/* high prio */
		DW0_IO_OPEN_FAIL_I_T_NEXUS_LOSS,
		DW0_IO_PHY_NOT_ENABLE_ERR,
		DW0_IO_OPEN_CNX_WRONG_DEST_ERR,
		/* others */
		DW0_IO_OPEN_CNX_AIP_TIMEOUT_ERR,
		DW0_IO_OPEN_CNX_STP_RSC_BUSY_ERR,
		DW0_IO_OPEN_CNX_PROTO_NOT_SUPPORT_ERR,
		DW0_IO_OPEN_CNX_CONN_RATE_NOT_SUPPORT_ERR,
		DW0_IO_OPEN_CNX_BAD_DEST_ERR,
		DW0_IO_OPEN_CNX_BREAK_RCVD_ERR,
		DW0_IO_OPEN_CNX_LOW_PHY_POWER_ERR,
		DW0_IO_OPEN_CNX_PATHWAY_BLOCKED_ERR,
		DW0_IO_OPEN_CNX_OPEN_TIMEOUT_ERR,
		DW0_IO_OPEN_CNX_NO_DEST_ERR,

		DW0_IO_OPEN_RETRY_THRESHOLD_REACHED_ERR,

		/* middle prio */
		DW0_IO_TX_ERR_CLOSE_PHY_DISABLE,
		DW0_IO_TX_ERR_CLOSE_DWS_TIMEOUT,
		DW0_IO_TX_ERR_CLOSE_COMINIT_RCVD,
		DW0_IO_TX_ERR_BREAK_TIMEOUT,
		DW0_IO_TX_ERR_BREAK_REQ,
		DW0_IO_TX_ERR_BREAK_RCVD,
		DW0_IO_TX_ERR_CLOSE_TIMEOUT,
		DW0_IO_TX_ERR_CLOSE_NORMAL,

		/* low prio */
		DW0_IO_TX_ERR_NAK_RCVD,
		DW0_IO_TX_ERR_ACK_NAK_TIMEOUT,

		DW0_IO_TX_ERR_CREDIT_TIMEOUT,

		DW0_IO_IPTT_CONFLICT_ERR,

		DW0_IO_TX_ERR_OPEN_BY_DEST_OR_OTHERS,

		DW0_IO_TX_ERR_WAIT_RCVD_TIMEOUT,
	};
	u32 err_dw0_lvl = sizeof(err_dw0_prio) / sizeof(u32);

	/*
	 * sas rx mixed error parse, rx mixed error
	 * can not happen with tx mixed error simultaneously.
	 */
	if (err_dw[1] && (err_dw[3] || (err_dw[2] >> 16))) {
		/* A class Trans error first */
		bit_ops = hisi_parse_err_dw(err_dw[1], err_dw1_prio,
						 err_dw1_lvl);
		dw_no = 1;
		if (bit_ops != HISI_MAX_BIT_POS)
			goto succ;
	}

	/* sas tx mixed error parse */
	if (err_dw[0] && (err_dw[2] & 0xffff)) {
		/* B class DMA error first */
		bit_ops = hisi_parse_err_dw((err_dw[2] & 0xffff),
						 err_dw2_prio, err_dw2_lvl);
		dw_no = 2;
		if (bit_ops != HISI_MAX_BIT_POS)
			goto succ;
	}

	/* parse err info priority: DW3>DW2>DW1>DW0 */
	bit_ops = hisi_parse_err_dw(err_dw[3], err_dw3_prio, err_dw3_lvl);
	dw_no = 3;
	if (bit_ops != HISI_MAX_BIT_POS)
		goto succ;

	bit_ops = hisi_parse_err_dw(err_dw[2], err_dw2_prio, err_dw2_lvl);
	dw_no = 2;
	if (bit_ops != HISI_MAX_BIT_POS)
		goto succ;

	bit_ops = hisi_parse_err_dw(err_dw[1], err_dw1_prio, err_dw1_lvl);
	dw_no = 1;
	if (bit_ops != HISI_MAX_BIT_POS)
		goto succ;

	bit_ops = hisi_parse_err_dw(err_dw[0], err_dw0_prio, err_dw0_lvl);
	dw_no = 0;
	if (bit_ops != HISI_MAX_BIT_POS)
		goto succ;

	return H_IO_ERROR_MAX;

succ:
	return ERR_ENCODE_V2_HW(dw_no, bit_ops);
}

/* by default, task resp is complete */
static void slot_err_v2_hw(struct hisi_hba *hisi_hba, struct sas_task *task,
			   struct hisi_sas_slot *slot)
{
	struct task_status_struct *ts = &task->task_status;
	struct hisi_sas_err_record_v2 *err_record = slot->status_buffer;
	int error_code = -1;

	error_code = hisi_parse_err_info(slot->status_buffer);
	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
	{
		switch (error_code) {
		case H_DW0_IO_OPEN_CNX_NO_DEST_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_NO_DEST;
			break;
		}
		case H_DW0_IO_OPEN_CNX_PROTO_NOT_SUPPORT_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_EPROTO;
			break;
		}
		case H_DW0_IO_OPEN_CNX_CONN_RATE_NOT_SUPPORT_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_CONN_RATE;
			break;
		}
		case H_DW0_IO_OPEN_CNX_BAD_DEST_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_BAD_DEST;
			break;
		}
		case H_DW0_IO_OPEN_CNX_WRONG_DEST_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
			break;
		}
		case H_DW3_IO_RX_UNEXP_NORMAL_RESP_ERR:
		case H_DW0_IO_OPEN_CNX_ZONE_VIOLATION_ERR:
		case H_DW3_IO_RX_RESP_BUFF_OVERFLOW_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_UNKNOWN;
			break;
		}
		case H_DW0_IO_OPEN_CNX_LOW_PHY_POWER_ERR:
		{
			/* not sure */
			ts->stat = SAS_DEV_NO_RESPONSE;
			break;
		}
		case H_DW3_IO_RX_DATA_LEN_OVERFLOW_ERR:
		{
			ts->stat = SAS_DATA_OVERRUN;
			ts->residual = 0;
			break;
		}
		case H_DW3_IO_RX_DATA_LEN_UNDERFLOW_ERR:
		/* case SIPC_RX_DATA_UNDERFLOW_ERR: */
		{
			ts->residual = err_record->trans_tx_fail_type;
			if (hisi_sas_is_rw_cmd(task->ssp_task.cmd->cmnd[0])) {
				struct device *dev = &hisi_hba->pdev->dev;
				struct ssp_response_iu *iu = slot->status_buffer
					+ sizeof(struct hisi_sas_err_record);

				/* if hdd has response, analyse it first */
				sas_ssp_task_response(dev, task, iu);

				/* avoid chip bug */
				if (ts->stat == SAM_STAT_GOOD)
					ts->stat = SAS_DATA_UNDERRUN;
			} else {
				ts->stat =  SAM_STAT_GOOD;
			}
			break;
		}
		default:
		{
			/* io may remain in disk, so need to abort it */
			ts->stat = SAS_IO_MAY_REMAIN_IN_DISK;
			break;
		}
	}
		break;
	}
	case SAS_PROTOCOL_SMP:
		ts->stat = SAM_STAT_CHECK_CONDITION;
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
	{
		switch (error_code) {
		case H_DW0_IO_OPEN_CNX_PROTO_NOT_SUPPORT_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_EPROTO;
			break;
		}
		case H_DW0_IO_OPEN_CNX_CONN_RATE_NOT_SUPPORT_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_CONN_RATE;
			break;
		}
		case H_DW0_IO_OPEN_CNX_BAD_DEST_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_BAD_DEST;
			break;
		}
		case H_DW0_IO_OPEN_CNX_WRONG_DEST_ERR:
		case H_DW0_IO_TX_ERR_SATA_DEV_LOST:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_WRONG_DEST;
			break;
		}
		case H_DW3_IO_RX_UNEXP_NORMAL_RESP_ERR:
		case H_DW0_IO_OPEN_CNX_ZONE_VIOLATION_ERR:
		case H_DW3_IO_RX_RESP_BUFF_OVERFLOW_ERR:
		{
			ts->stat = SAS_OPEN_REJECT;
			ts->open_rej_reason = SAS_OREJ_UNKNOWN;
			break;
		}
		case H_DW0_IO_OPEN_CNX_LOW_PHY_POWER_ERR:
		{
			/* not sure */
			ts->stat = SAS_DEV_NO_RESPONSE;
			break;
		}
		case H_DW3_IO_RX_DATA_LEN_OVERFLOW_ERR:
		{
			ts->stat = SAS_DATA_OVERRUN;
			ts->residual = 0;
			break;
		}
		case H_DW3_IO_RX_DATA_LEN_UNDERFLOW_ERR:
		{
			ts->residual = err_record->trans_tx_fail_type;
			if (hisi_sas_is_ata_rw_cmd(task))
				ts->stat = SAS_DATA_UNDERRUN;
			else
				ts->stat =  SAM_STAT_GOOD;
			break;
		}
		default:
		{
			/* io may remain in disk, so need to abort it */
			ts->stat = SAS_IO_MAY_REMAIN_IN_DISK;
			break;
		}
		}
		sata_done_v2_hw(hisi_hba, task, slot);
	}
		break;
	default:
		break;
	}
}

static int slot_complete_v2_hw(struct hisi_hba *hisi_hba,
			       struct hisi_sas_slot *slot, int abort)
{
	struct sas_task *task = slot->task;
	struct hisi_sas_device *sas_dev;
	struct device *dev = &hisi_hba->pdev->dev;
	struct task_status_struct *ts;
	struct domain_device *device;
	enum exec_status sts;
	u32 *error_info = slot->status_buffer;
	struct hisi_sas_complete_v2_hdr *complete_queue =
			hisi_hba->complete_hdr[slot->cmplt_queue];
	struct hisi_sas_complete_v2_hdr *complete_hdr =
		&complete_queue[slot->cmplt_queue_slot];
	struct hisi_sas_io_context context;

	if (unlikely(!task || !task->lldd_task || !task->dev))
		return -EINVAL;

	ts = &task->task_status;
	device = task->dev;
	sas_dev = device->lldd_dev;

	memset(ts, 0, sizeof(*ts));
	ts->resp = SAS_TASK_COMPLETE;
	ts->stat = SAM_STAT_GOOD;

	if (unlikely(!sas_dev || abort)) {
		if (!sas_dev)
			dev_dbg(dev, "slot complete: port has not device\n");
		ts->stat = SAS_PHY_DOWN;
		goto out;
	}

	if (slot->cmd_type == HISI_CMD_TYPE_ABORT_DEV ||
		slot->cmd_type == HISI_CMD_TYPE_ABORT_SINGLE)
		goto out;

	/*
	 *even io come back, the io mey remain in disk.
	 *so we need to parse errorinfo and then decide what to do
	 */
	if ((complete_hdr->dw0 & CMPLT_HDR_ERX_MSK) ||
		(complete_hdr->dw0 & CMPLT_HDR_CMPLT_MSK) != 1) {

		slot_err_v2_hw(hisi_hba, task, slot);

		if (SAM_STAT_GOOD != ts->stat) {
			/* print cq&error info for dbg */
			pr_info("cq: iptt:%d, task:%p, cmpl_status:%d, err_rcrd_xfrd:%d,rspns_xfrd:%d,error_phase:%d,devid:%d,io_cfg_err_code:%d,cfg_err_code:%d\n",
				slot->idx, task,
				complete_hdr->dw0 & CMPLT_HDR_CMPLT_MSK,
				(complete_hdr->dw0 & CMPLT_HDR_ERX_MSK)
				>> CMPLT_HDR_ERX_OFF,
				(complete_hdr->dw0 & CMPLT_HDR_RSPNS_XFRD_MSK)
				>> CMPLT_HDR_RSPNS_XFRD_OFF,
				(complete_hdr->dw0 & CPLIT_HDR_ERROR_PHASE_MSK)
				>> CMPLT_HDR_ERROR_PHASE_OFF,
				(complete_hdr->dw1 & 0xffff0000) >> 16,
				(complete_hdr->dw0 & 0xffff0000) >> 16,
				complete_hdr->dw3 & 0xffff);
			pr_info("iptt:%d dw0:0x%x,dw1:0x%x,dw2:0x%x,dw3:0x%x\n",
				slot->idx, error_info[0], error_info[1],
				error_info[2], error_info[3]);
#ifdef SLOT_ABORT
		if (unlikely(slot->abort)) {
			queue_work(hisi_hba->wq, &slot->abort_slot);
			return ts->stat;
		}
#endif
			/* simulate io timeout, so scsi will do abort + retry */
			if (SAS_IO_MAY_REMAIN_IN_DISK == ts->stat) {
				task->task_state_flags &= ~SAS_TASK_STATE_DONE;
				return -EINVAL;
			}

			goto out;
		}
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
	{
		struct ssp_response_iu *iu = slot->status_buffer +
			sizeof(struct hisi_sas_err_record);

		sas_ssp_task_response(dev, task, iu);
		break;
	}
	case SAS_PROTOCOL_SMP:
	{
		struct scatterlist *sg_resp = &task->smp_task.smp_resp;
		void *to;

		ts->stat = SAM_STAT_GOOD;
		to = kmap_atomic(sg_page(sg_resp));

		dma_unmap_sg(dev, &task->smp_task.smp_resp, 1,
			     DMA_FROM_DEVICE);
		dma_unmap_sg(dev, &task->smp_task.smp_req, 1,
			     DMA_TO_DEVICE);
		memcpy(to + sg_resp->offset,
		       slot->status_buffer +
		       sizeof(struct hisi_sas_err_record),
		       sg_dma_len(sg_resp));
		kunmap_atomic(to);
		break;
	}
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
	{
		ts->stat = SAM_STAT_GOOD;
		sata_done_v2_hw(hisi_hba, task, slot);
		break;
	}
	default:
		ts->stat = SAM_STAT_CHECK_CONDITION;
		break;
	}

	if (!slot->port->port_attached) {
		dev_err(dev, "slot complete: port %d has removed\n",
			slot->port->id);
		ts->stat = SAS_PHY_DOWN;
	}

out:

	sts = ts->stat;
	memset(&context, 0, sizeof(struct hisi_sas_io_context));
	context.event = HISI_SLOT_EVENT_COMPLETE;
	context.hba = hisi_hba;
	context.iptt = slot->idx;
	context.task = task;
	context.dev = sas_dev;
	context.handler = hisi_sas_complete_io;
	context.handler_locked = hisi_sas_free_slot;
	hisi_sas_slot_fsm(slot, &context);

	return sts;
}

static int get_ncq_tag_v2_hw(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
			qc->tf.command == ATA_CMD_FPDMA_READ) {
			*tag = qc->tag;
			return 1;
		}
	}
	return 0;
}

static int prep_ata_v2_hw(struct hisi_hba *hisi_hba,
			  struct hisi_sas_slot *slot)
{
	struct sas_task *task = slot->task;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct hisi_sas_port *port = device->port->lldd_port;
	struct domain_device *parent_dev = device->parent;
	int hdr_tag = 0;
	u8 *buf_cmd;
	int has_data = 0;
	int rc = 0;
	u32 dw1 = 0, dw2 = 0;

	/* create header */
	/* dw0 */
	hdr->dw0 = cpu_to_le32(port->id << CMD_HDR_PORT_OFF);
	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type))
		hdr->dw0 |= cpu_to_le32(3 << CMD_HDR_CMD_OFF);
	else
		hdr->dw0 |= cpu_to_le32(4 << CMD_HDR_CMD_OFF);

	/* dw1 */
	switch (task->data_dir) {
	case DMA_TO_DEVICE:
		has_data = 1;
		dw1 |= DIR_TO_DEVICE << CMD_HDR_DIR_OFF;
		break;
	case DMA_FROM_DEVICE:
		has_data = 1;
		dw1 |= DIR_TO_INI << CMD_HDR_DIR_OFF;
		break;
	default:
		dw1 &= ~CMD_HDR_DIR_MSK;
	}

	if ((task->ata_task.fis.command == ATA_CMD_DEV_RESET)
			&& (task->ata_task.fis.control == ATA_SRST))
		dw1 |= 1 << CMD_HDR_RESET_OFF;

	dw1 |= (hisi_sas_get_ata_protocol(task->ata_task.fis.command,
	 task->data_dir)) << CMD_HDR_FRAME_TYPE_OFF;
	dw1 |= sas_dev->device_id << CMD_HDR_DEV_ID_OFF;
	hdr->dw1 = cpu_to_le32(dw1);

	/* dw2 */
	if (task->ata_task.use_ncq && get_ncq_tag_v2_hw(task, &hdr_tag)) {
		task->ata_task.fis.sector_count |= (u8) (hdr_tag << 3);
		dw2 |= hdr_tag << CMD_HDR_NCQ_TAG_OFF;
	}

	dw2 |= ((sizeof(struct hisi_sas_command_table_stp) + 3) / 4) |
		((HISI_SAS_MAX_STP_RESP_SZ / 4) << CMD_HDR_MRFL_OFF) |
		2 << CMD_HDR_SG_MOD_OFF;
	hdr->dw2 = cpu_to_le32(dw2);

	/* dw3 */
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	if (has_data) {
		rc = prep_prd_sge_v2_hw(hisi_hba, slot, hdr, task->scatter,
					slot->n_elem);
		if (rc)
			return rc;
	}


	hdr->data_transfer_len = cpu_to_le32(task->total_xfer_len);
	hdr->cmd_table_addr = cpu_to_le64(slot->command_table_dma);
	hdr->sts_buffer_addr = cpu_to_le64(slot->status_buffer_dma);

	buf_cmd = slot->command_table;

	if (likely(!task->ata_task.device_control_reg_update))
		task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS and ATAPI CDB */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));
	if (device->sata_dev.class == ATA_DEV_ATAPI)
		memcpy(buf_cmd + 0x20,
			task->ata_task.atapi_packet, ATAPI_CDB_LEN);

	return 0;
}

static int prep_abort_v2_hw(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	struct sas_task *task = slot->task;
	struct domain_device *dev = task->dev;
	struct hisi_sas_cmd_hdr *hdr = slot->cmd_hdr;
	struct hisi_sas_port *port = slot->port;

	/* dw0 */
	hdr->dw0 = cpu_to_le32((5 << CMD_HDR_CMD_OFF) | /*abort*/
			       (port->id << CMD_HDR_PORT_OFF) |
				   ((dev_is_sata(dev) ? 1:0)
					<< CMD_HDR_ABORT_DEVICE_TYPE_OFF) |
					(abort_flag
					 << CMD_HDR_ABORT_FLAG_OFF));

	/* dw1 */
	hdr->dw1 = cpu_to_le32(device_id
			<< CMD_HDR_DEV_ID_OFF);

	/* dw7 */
	hdr->dw7 = cpu_to_le32(tag_to_abort << CMD_HDR_ABORT_IPTT_OFF);
	hdr->transfer_tags = cpu_to_le32(slot->idx);

	return 0;
}

static int phy_up_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	int i, res = 0;
	u32 context, port_id, link_rate, hard_phy_linkrate, macro = 0, lane = 0;
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = &hisi_hba->pdev->dev;
	u32 *frame_rcvd = (u32 *)sas_phy->frame_rcvd;
	struct sas_identify_frame *id = (struct sas_identify_frame *)frame_rcvd;

	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 1);

	hisi_sas_phy_write32(hisi_hba, phy_no, SAS_TX_TRAIN_TIMER1, 0xFFFFF);
	if (!get_macro_lane(hisi_hba->core_id, phy_no, &macro, &lane))
		serdes_regbits_write(macro, lane, 60, 15, 0x0);
	else
		pr_info("%s get_macro_lane fail macro: %d lane: %d\n", __func__,
			macro, lane);

	context = hisi_sas_read32(hisi_hba, PHY_CONTEXT);
	if (context & (1 << phy_no))
		goto end;

	if (phy_no == 8) {
		u32 port_state = hisi_sas_read32(hisi_hba, PORT_STATE);

		port_id = (port_state & PORT_STATE_PHY8_PORT_NUM_MSK) >>
			  PORT_STATE_PHY8_PORT_NUM_OFF;
		link_rate = (port_state & PORT_STATE_PHY8_CONN_RATE_MSK) >>
			    PORT_STATE_PHY8_CONN_RATE_OFF;
	} else {
		port_id = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
		port_id = (port_id >> (4 * phy_no)) & 0xf;
		link_rate = hisi_sas_read32(hisi_hba, PHY_CONN_RATE);
		link_rate = (link_rate >> (phy_no * 4)) & 0xf;
	}

	if (port_id == 0xf) {
		dev_err(dev, "phyup: phy%d invalid portid\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}

	for (i = 0; i < 6; i++) {
		u32 idaf = hisi_sas_phy_read32(hisi_hba, phy_no,
					RX_IDAF_DWORD0 + (i * 4));
		frame_rcvd[i] = __swab32(idaf);
	}

	sas_phy->linkrate = link_rate;
	hard_phy_linkrate = hisi_sas_phy_read32(hisi_hba, phy_no,
						HARD_PHY_LINKRATE);
	phy->maximum_linkrate = hard_phy_linkrate & 0xf;
	phy->minimum_linkrate = (hard_phy_linkrate >> 4) & 0xf;

	sas_phy->oob_mode = SAS_OOB_MODE;
	memcpy(sas_phy->attached_sas_addr, &id->sas_addr, SAS_ADDR_SIZE);
	dev_info(dev, "phyup: phy%d link_rate=%d port=%d\n",
			phy_no, link_rate, port_id);
	phy->port_id = port_id;
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	phy->phy_type |= PORT_TYPE_SAS;
	phy->phy_attached = 1;
	phy->identify.device_type = id->dev_type;
	phy->frame_rcvd_size =	sizeof(struct sas_identify_frame);
	if (phy->identify.device_type == SAS_END_DEVICE)
		phy->identify.target_port_protocols =
			SAS_PROTOCOL_SSP;
	else if (phy->identify.device_type != SAS_PHY_UNUSED)
		phy->identify.target_port_protocols =
			SAS_PROTOCOL_SMP;
	queue_work(hisi_hba->wq, &phy->phyup_ws);

end:
	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_PHY_ENABLE_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_PHY_ENA_MSK, 0);

	return res;
}

static int phy_down_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	int res = 0;
	u32 phy_state, sl_ctrl, txid_auto;
	u32 macro = 0, lane = 0;

	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 1);

	phy_state = hisi_sas_read32(hisi_hba, PHY_STATE);
	hisi_sas_phy_down(hisi_hba, phy_no, (phy_state & 1 << phy_no) ? 1 : 0);

	sl_ctrl = hisi_sas_phy_read32(hisi_hba, phy_no, SL_CONTROL);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_CONTROL,
						sl_ctrl & (~SL_CTA_MSK));

	txid_auto = hisi_sas_phy_read32(hisi_hba, phy_no, TXID_AUTO);
	hisi_sas_phy_write32(hisi_hba, phy_no, TXID_AUTO,
						txid_auto | CT3_MSK);

	hisi_sas_phy_write32(hisi_hba, phy_no, SAS_TX_TRAIN_TIMER1, 0x075300);
	if (!get_macro_lane(hisi_hba->core_id, phy_no, &macro, &lane))
		serdes_regbits_write(macro, lane, 60, 15, 0x1);
	else
		pr_info("%s get_macro_lane fail macro: %d lane: %d\n", __func__,
			macro, lane);

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0, CHL_INT0_NOT_RDY_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, PHYCTRL_NOT_RDY_MSK, 0);

	return res;
}

static irqreturn_t int_phy_updown_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	u32 irq_msk;
	int phy_no = 0;
	irqreturn_t res = IRQ_HANDLED;

	irq_msk = (hisi_sas_read32(hisi_hba, HGC_INVLD_DQE_INFO)
		   >> HGC_INVLD_DQE_INFO_FB_CH0_OFF) & 0x1ff;
	while (irq_msk) {
		if (irq_msk  & 1) {
			u32 irq_value = hisi_sas_phy_read32(hisi_hba, phy_no,
							    CHL_INT0);

			if (irq_value & CHL_INT0_SL_PHY_ENABLE_MSK)
				/* phy up */
				if (phy_up_v2_hw(phy_no, hisi_hba)) {
					res = IRQ_NONE;
					goto end;
				}

			if (irq_value & CHL_INT0_NOT_RDY_MSK)
				/* phy down */
				if (phy_down_v2_hw(phy_no, hisi_hba)) {
					res = IRQ_NONE;
					goto end;
				}
		}
		irq_msk >>= 1;
		phy_no++;
	}

end:
	return res;
}

static void phy_bcast_v2_hw(int phy_no, struct hisi_hba *hisi_hba)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	unsigned long flags;
	u32 bcast_status;

	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 1);
	bcast_status = hisi_sas_phy_read32(hisi_hba, phy_no, RX_PRIMS_STATUS);
	/* process broadcast change only */
	if (bcast_status & BIT(1)) {
		pr_info("phyid:%d bcast change rcvd\n", phy_no);
		spin_lock_irqsave(&hisi_hba->lock, flags);
		sas_ha->notify_port_event(sas_phy, PORTE_BROADCAST_RCVD);
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
	}

	hisi_sas_phy_write32(hisi_hba, phy_no, CHL_INT0,
			     CHL_INT0_SL_RX_BCST_ACK_MSK);
	hisi_sas_phy_write32(hisi_hba, phy_no, SL_RX_BCAST_CHK_MSK, 0);
}

static irqreturn_t int_chnl_int_v2_hw(int irq_no, void *p)
{
	struct hisi_hba *hisi_hba = p;
	struct device *dev = &hisi_hba->pdev->dev;
	u32 ent_msk, ent_tmp, irq_msk;
	int phy_no = 0;

	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK3);
	ent_tmp = ent_msk;
	ent_msk |= ENT_INT_SRC_MSK3_ENT95_MSK_MSK;
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_msk);

	irq_msk = (hisi_sas_read32(hisi_hba, HGC_INVLD_DQE_INFO) >>
			HGC_INVLD_DQE_INFO_FB_CH3_OFF) & 0x1ff;

	while (irq_msk) {
		if (irq_msk & (1 << phy_no)) {
			u32 irq_value0 = hisi_sas_phy_read32(hisi_hba, phy_no,
							     CHL_INT0);
			u32 irq_value1 = hisi_sas_phy_read32(hisi_hba, phy_no,
							     CHL_INT1);
			u32 irq_value2 = hisi_sas_phy_read32(hisi_hba, phy_no,
							     CHL_INT2);

			if (irq_value1) {
				if (irq_value1 & (CHL_INT1_DMAC_RX_ECC_ERR_MSK |
						  CHL_INT1_DMAC_TX_ECC_ERR_MSK))
					panic("%s: DMAC RX/TX ecc bad error! (0x%x)",
						dev_name(dev), irq_value1);

				hisi_sas_phy_write32(hisi_hba, phy_no,
						     CHL_INT1, irq_value1);
			}

			if (irq_value2)
				hisi_sas_phy_write32(hisi_hba, phy_no,
						     CHL_INT2, irq_value2);


			if (irq_value0) {
				if (irq_value0 & CHL_INT0_SL_RX_BCST_ACK_MSK)
					phy_bcast_v2_hw(phy_no, hisi_hba);

				hisi_sas_phy_write32(hisi_hba,
					phy_no, CHL_INT0, irq_value0
				& (~CHL_INT0_HOTPLUG_TOUT_MSK)
				& (~CHL_INT0_SL_PHY_ENABLE_MSK)
				& (~CHL_INT0_NOT_RDY_MSK));
			}
		}
		irq_msk &= ~(1 << phy_no);
		phy_no++;
	}

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, ent_tmp);

	return IRQ_HANDLED;
}

static irqreturn_t cq_interrupt_v2_hw(int irq_no, void *p)
{
	struct hisi_sas_cq *cq = p;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	struct hisi_sas_slot *slot;
	struct hisi_sas_itct *itct;
	struct hisi_sas_complete_v2_hdr *complete_queue;
	u32 irq_value, rd_point = cq->rd_point, wr_point, dev_id;
	int queue = cq->id;
	struct hisi_sas_io_context context;

	complete_queue = hisi_hba->complete_hdr[queue];
	irq_value = hisi_sas_read32(hisi_hba, OQ_INT_SRC);

	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 1 << queue);

	wr_point = hisi_sas_read32(hisi_hba, COMPL_Q_0_WR_PTR +
				   (0x14 * queue));

	while (rd_point != wr_point) {
		struct hisi_sas_complete_v2_hdr *complete_hdr;
		int iptt;

		complete_hdr = &complete_queue[rd_point];

		if (complete_hdr->act) {
			u32 act_tmp = complete_hdr->act;
			int ncq_tag_count = ffs(act_tmp);

			dev_id = (complete_hdr->dw1 & CMPLT_HDR_DEV_ID_MSK) >>
				 CMPLT_HDR_DEV_ID_OFF;
			itct = &hisi_hba->itct[dev_id];

			while (ncq_tag_count) {
				__le64 *ncq_tag = &itct->qw4_15[0];

				ncq_tag_count -= 1;
				iptt = (ncq_tag[ncq_tag_count / 5]
					>> (ncq_tag_count % 5) * 12) & 0xfff;

				slot = &hisi_hba->slot_info[iptt];
				slot->cmplt_queue_slot = rd_point;
				slot->cmplt_queue = queue;
				context.event = HISI_SLOT_EVENT_PARSE_CQ_ENTRY;
				context.hba = hisi_hba;
				context.handler = hisi_sas_parse_cq_entry;
				context.handler_locked = NULL;
				hisi_sas_slot_fsm(slot, &context);

				act_tmp &= ~(1 << ncq_tag_count);
				ncq_tag_count = ffs(act_tmp);
			}
		} else {
			iptt = (complete_hdr->dw1) & CMPLT_HDR_IPTT_MSK;
			slot = &hisi_hba->slot_info[iptt];
			slot->cmplt_queue_slot = rd_point;
			slot->cmplt_queue = queue;
			context.event = HISI_SLOT_EVENT_PARSE_CQ_ENTRY;
			context.hba = hisi_hba;
			context.handler = hisi_sas_parse_cq_entry;
			context.handler_locked = NULL;
			hisi_sas_slot_fsm(slot, &context);
		}

		if (++rd_point >= HISI_SAS_QUEUE_SLOTS)
			rd_point = 0;
	}

	/* update rd_point */
	cq->rd_point = rd_point;
	hisi_sas_write32(hisi_hba, COMPL_Q_0_RD_PTR + (0x14 * queue), rd_point);

	return IRQ_HANDLED;
}

static irqreturn_t sata_int_v2_hw(int irq_no, void *p)
{
	struct hisi_sas_phy *phy = p;
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct device *dev = &hisi_hba->pdev->dev;
	struct	hisi_sas_initial_fis *initial_fis;
	struct dev_to_host_fis *fis;
	u32 ent_tmp, ent_msk, ent_int, port_id, link_rate, hard_phy_linkrate;
	irqreturn_t res = IRQ_HANDLED;
	u8 attached_sas_addr[SAS_ADDR_SIZE] = {0};
	int phy_no, offset;

	phy_no = sas_phy->id;
	initial_fis = &hisi_hba->initial_fis[phy_no];
	fis = &initial_fis->fis;

	offset = 4 * (phy_no / 4);
	ent_msk = hisi_sas_read32(hisi_hba, ENT_INT_SRC_MSK1 + offset);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1 + offset,
			ent_msk | 1 << ((phy_no % 4) * 8));

	ent_int = hisi_sas_read32(hisi_hba, ENT_INT_SRC1 + offset);
	ent_tmp = ent_int & (1 << (ENT_INT_SRC1_D2H_FIS_CH1_OFF *
				(phy_no % 4)));
	ent_int >>= ENT_INT_SRC1_D2H_FIS_CH1_OFF * (phy_no % 4);
	if ((ent_int & ENT_INT_SRC1_D2H_FIS_CH0_MSK) == 0) {
		dev_warn(dev, "sata int: phy%d did not receive FIS\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}

	if (unlikely(phy_no == 8)) {
		u32 port_state = hisi_sas_read32(hisi_hba, PORT_STATE);

		port_id = (port_state & PORT_STATE_PHY8_PORT_NUM_MSK) >>
			  PORT_STATE_PHY8_PORT_NUM_OFF;
		link_rate = (port_state & PORT_STATE_PHY8_CONN_RATE_MSK) >>
			    PORT_STATE_PHY8_CONN_RATE_OFF;
	} else {
		port_id = hisi_sas_read32(hisi_hba, PHY_PORT_NUM_MA);
		port_id = (port_id >> (4 * phy_no)) & 0xf;
		link_rate = hisi_sas_read32(hisi_hba, PHY_CONN_RATE);
		link_rate = (link_rate >> (phy_no * 4)) & 0xf;
	}

	if (port_id == 0xf) {
		dev_err(dev, "sata int: phy%d invalid portid\n", phy_no);
		res = IRQ_NONE;
		goto end;
	}

	sas_phy->linkrate = link_rate;
	hard_phy_linkrate = hisi_sas_phy_read32(hisi_hba, phy_no,
						HARD_PHY_LINKRATE);
	phy->maximum_linkrate = hard_phy_linkrate & 0xf;
	phy->minimum_linkrate = (hard_phy_linkrate >> 4) & 0xf;

	sas_phy->oob_mode = SATA_OOB_MODE;
	/* Make up some unique SAS address */
	attached_sas_addr[0] = 0x50;
	attached_sas_addr[1] = hisi_hba->core_id;
	attached_sas_addr[7] = phy_no;
	memcpy(sas_phy->attached_sas_addr, attached_sas_addr, SAS_ADDR_SIZE);
	memcpy(sas_phy->frame_rcvd, fis, sizeof(struct dev_to_host_fis));
	dev_info(dev, "sata int phyup: phy%d link_rate=%d\n", phy_no, link_rate);
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	phy->port_id = port_id;
	phy->phy_type |= PORT_TYPE_SATA;
	phy->phy_attached = 1;
	phy->identify.device_type = SAS_SATA_DEV;
	phy->frame_rcvd_size = sizeof(struct dev_to_host_fis);
	phy->identify.target_port_protocols = SAS_PROTOCOL_SATA;
	queue_work(hisi_hba->wq, &phy->phyup_ws);

end:
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1 + offset, ent_tmp);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1 + offset, ent_msk);

	return res;
}

static irq_handler_t phy_interrupts[HISI_SAS_PHY_INT_NR] = {
	int_phy_updown_v2_hw,
	int_chnl_int_v2_hw,
};

static int interrupt_init_v2_hw(struct hisi_hba *hisi_hba)
{
	struct platform_device *pdev = hisi_hba->pdev;
	struct device *dev = &pdev->dev;
	int i, irq, rc, irq_map[128];
	int cpu;
	cpumask_t mask;

	for (i = 0; i < 128; i++)
		irq_map[i] = platform_get_irq(pdev, i);

	for (i = 0; i < HISI_SAS_PHY_INT_NR; i++) {
		int idx = i;

		irq = irq_map[idx + 1];
		if (!irq) {
			dev_err(dev, "irq init: fail map phy interrupt %d\n",
				idx);
			return -ENOENT;
		}

		rc = devm_request_irq(dev, irq, phy_interrupts[i], 0,
				      DRV_NAME " phy", hisi_hba);
		if (rc) {
			dev_err(dev, "irq init: could not request "
				"phy interrupt %d, rc=%d\n",
				irq, rc);
			return -ENOENT;
		}
	}

	for (i = 0; i < hisi_hba->n_phy; i++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[i];
		int idx = i + 72;

		irq = irq_map[idx];
		if (!irq) {
			dev_err(dev, "irq init: fail map phy interrupt %d\n",
				idx);
			return -ENOENT;
		}

		rc = devm_request_irq(dev, irq, sata_int_v2_hw, 0,
				      DRV_NAME " sata", phy);
		if (rc) {
			dev_err(dev, "irq init: could not request "
				"sata interrupt %d, rc=%d\n",
				irq, rc);
			return -ENOENT;
		}
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		int idx = i + 96;

		irq = irq_map[idx];
		if (!irq) {
			dev_err(dev,
				"irq init: could not map cq interrupt %d\n",
				idx);
			return -ENOENT;
		}
		rc = devm_request_irq(dev, irq, cq_interrupt_v2_hw, 0,
				      DRV_NAME " cq", &hisi_hba->cq[i]);
		if (rc) {
			dev_err(dev,
				"irq init: could not request cq interrupt %d, rc=%d\n",
				irq, rc);
			return -ENOENT;
		}
		/*
		 * set CQ irq affinity.we only use 16 irqs for completion
		 * queues, the 1610 chip support 16 completion queues
		 * at most.  And most of the work is processing these
		 * CQ interrupts.
		 */
		if (cpu_online(i)) {
			cpumask_clear(&mask);
			cpu = i;
			cpumask_set_cpu(cpu, &mask);
			irq_set_affinity_hint(irq, &mask);
		}
	}

	return 0;
}

static int hisi_sas_v2_init(struct hisi_hba *hisi_hba)
{
	int rc;

	rc = hw_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	rc = interrupt_init_v2_hw(hisi_hba);
	if (rc)
		return rc;

	phys_init_v2_hw(hisi_hba);

	return 0;
}

static void reinit_chip_resource_v2_hw(struct hisi_hba *hisi_hba)
{
	int i, s;

	/* re-initialize DQ/CQ/IOST/IO breakpoint table. */
	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct hisi_sas_dq *dq = &hisi_hba->dq[i];

		/* Delivery queue */
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		memset(hisi_hba->cmd_hdr[i], 0, s);
		dq->wr_point = 0;
		hisi_sas_write32(hisi_hba, DLVRY_Q_0_WR_PTR + (i * 0x14), 0);

		/* Completion queue */
		s = sizeof(struct hisi_sas_complete_v2_hdr) *
				HISI_SAS_QUEUE_SLOTS;
		memset(hisi_hba->complete_hdr[i], 0, s);
		cq->rd_point = 0;
		hisi_sas_write32(hisi_hba, COMPL_Q_0_RD_PTR + (i * 0x14), 0);
	}

	/* clear initial d2h fis memory, new info will get after enable phys */
	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	memset(hisi_hba->initial_fis, 0, s);

	/* clear iost table */
	s = HISI_SAS_COMMAND_ENTRIES_V2_HW * sizeof(struct hisi_sas_iost);
	memset(hisi_hba->iost, 0, s);

	/* clear sas io bkpt table */
	s = HISI_SAS_COMMAND_ENTRIES_V2_HW * sizeof(struct hisi_sas_breakpoint);
	memset(hisi_hba->breakpoint, 0, s);

	/* clear ata io bkpt table */
	s = HISI_SAS_COMMAND_ENTRIES_V2_HW *
			sizeof(struct hisi_sas_breakpoint) * 2;
	memset(hisi_hba->sata_breakpoint, 0, s);
}

static int sas_logic_soft_reset_v2_hw(struct hisi_hba *hisi_hba)
{
	int cnt = 1;
	/* 1,disable all dq. */
	hisi_sas_write32(hisi_hba, DLVRY_QUEUE_ENABLE, (u32)0x0);

	/* 2,stop all phys. */
	stop_phys_v2_hw(hisi_hba);

	/* 3,wait at least 50us after phy disable. */
	mdelay(10);

	/* 4,AM_CTRL_GLOBAL.ctrl_shutdown_req = 1 */
	hisi_sas_write32(hisi_hba, AXI_MASTER_CFG_BASE +
			AM_CTRL_GLOBAL, (u32)0x1);

	/* 5, wait until bus is idle. */
	while (1) {
		u32 status = hisi_sas_read32_relaxed(hisi_hba,
				AXI_MASTER_CFG_BASE + AM_CURR_TRANS_RETURN);

		if (status == 0x3)
			break;

		udelay(10);
		if (cnt++ > 10) {
			pr_info("%s, wait axi bus state to idle timeout!\n",
				__func__);
			return -1;
		}
	}

	reinit_chip_resource_v2_hw(hisi_hba);

	/* 6,reset and de-reset operation. */
	return hw_init_v2_hw(hisi_hba);
}

static int interrupt_disable_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->queue_count; i++)
		hisi_sas_write32(hisi_hba, OQ0_INT_SRC_MSK + 0x4 * i, 0x1);

	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC_MSK3, 0xffffffff);
	hisi_sas_write32(hisi_hba, SAS_ECC_INTR_MSK, 0xffffffff);

	for (i = 0; i < hisi_hba->n_phy; i++) {

		/* disable(mask) channel interrupt */
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1_MSK, 0xffffffff);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2_MSK, 0xffffffff);
	}

	return 0;
}

static int interrupt_clear_v2_hw(struct hisi_hba *hisi_hba)
{
	int i;

	hisi_sas_write32(hisi_hba, OQ_INT_SRC, 0x0);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC1, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC2, 0xffffffff);
	hisi_sas_write32(hisi_hba, ENT_INT_SRC3, 0xffffffff);

	for (i = 0; i < hisi_hba->n_phy; i++) {

		/* clear channel interrupt */
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT0, 0xFFFFFFFF);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT1, 0xFFFFFFFF);
		hisi_sas_phy_write32(hisi_hba, i, CHL_INT2, 0xFFFFFFFF);

	}

	return 0;
}

static int soft_reset_v2_hw(struct hisi_hba *hisi_hba)
{
	int rc;

	del_timer_sync(&hisi_hba->link_timer);

	/* disable interrupt, so interrupt can not distrub soft reset */
	interrupt_disable_v2_hw(hisi_hba);

	/* 1. sas logic soft reset */
	rc = sas_logic_soft_reset_v2_hw(hisi_hba);
	if (rc)
		return rc;

	/* 2.enable all phys(re-negoiate) */
	start_phys_v2_hw((unsigned long)hisi_hba);

	return 0;
}

static const struct hisi_sas_hw hisi_sas_v2_hw = {
	.hw_init = hisi_sas_v2_init,
	.setup_itct = setup_itct_v2_hw,
	.slot_index_alloc = slot_index_alloc_v2_hw,
	.alloc_dev = alloc_dev_v2_hw,
	.sl_notify = sl_notify_v2_hw,
	.get_wideport_bitmap = get_wideport_bitmap_v2_hw,
	.free_device = free_device_v2_hw,
	.prep_smp = prep_smp_v2_hw,
	.prep_ssp = prep_ssp_v2_hw,
	.prep_stp = prep_ata_v2_hw,
	.prep_abort = prep_abort_v2_hw,
	.get_free_slot = get_free_slot_v2_hw,
	.start_delivery = start_delivery_v2_hw,
	.slot_complete = slot_complete_v2_hw,
	.phy_enable = enable_phy_v2_hw,
	.phy_disable = disable_phy_v2_hw,
	.phy_hard_reset = phy_hard_reset_v2_hw,
	.soft_reset = soft_reset_v2_hw,
	.max_command_entries = HISI_SAS_COMMAND_ENTRIES_V2_HW,
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v2_hdr),
#ifdef SAS_DIF
	.prot_cap = SHOST_DIF_TYPE1_PROTECTION |
		SHOST_DIF_TYPE2_PROTECTION |
		SHOST_DIF_TYPE3_PROTECTION |
		SHOST_DIX_TYPE1_PROTECTION |
		SHOST_DIX_TYPE2_PROTECTION |
		SHOST_DIX_TYPE3_PROTECTION,
#endif
};

static int hisi_sas_v2_probe(struct platform_device *pdev)
{
	if (!alg_hccs_subctrl_base)
		hisi_sas_serdes_init(pdev);

	return hisi_sas_probe(pdev, &hisi_sas_v2_hw);
}

static int hisi_sas_v2_remove(struct platform_device *pdev)
{
	if (alg_hccs_subctrl_base)
		hisi_sas_serdes_exit();

	return hisi_sas_remove(pdev);
}

static const struct of_device_id sas_v2_of_match[] = {
	{ .compatible = "hisilicon,hip06-sas-v2",},
	{},
};
MODULE_DEVICE_TABLE(of, sas_v2_of_match);

static struct platform_driver hisi_sas_v2_driver = {
	.probe = hisi_sas_v2_probe,
	.remove = hisi_sas_v2_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sas_v2_of_match,
	},
};

module_platform_driver(hisi_sas_v2_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v2 hw driver");
MODULE_ALIAS("platform:" DRV_NAME);
