/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HNS_DSAF_MAIN_H
#define __HNS_DSAF_MAIN_H
#include "hnae.h"

#include "hns_dsaf_reg.h"
#include "hns_dsaf_mac.h"

struct hns_mac_cb;

#define DSAF_DRV_NAME "hns_dsaf"
#define DSAF_MOD_VERSION "v1.0"
#define DSAF_DEVICE_NAME "dsaf"

#define HNS_DSAF_DEBUG_NW_REG_OFFSET 0x100000

#define DSAF_BASE_INNER_PORT_NUM 127  /* mac tbl qid*/

#define DSAF_MAX_CHIP_NUM 2  /*max 2 chips */

#define DSAF_DEFAUTL_QUEUE_NUM_PER_PPE 22

#define HNS_DSAF_MAX_DESC_CNT 1024
#define HNS_DSAF_MIN_DESC_CNT 16

#define DSAF_INVALID_ENTRY_IDX 0xffff

#define DSAF_CFG_READ_CNT   30

#define DSAF_DUMP_REGS_NUM 504
#define DSAF_STATIC_NUM 28
#define DSAF_V2_STATIC_NUM	44
#define DSAF_PRIO_NR	8
#define DSAF_REG_PER_ZONE	3

#define DSAF_ROCE_CREDIT_CHN 8
#define DSAF_ROCE_CHAN_MODE 3

#define HNS_MAX_WAIT_CNT 10000

#define HNS_DSAF_XGE_IRQ_ENABLE_MSK	\
		(~((1U << DSAF_SBM_XGE_PFC_EN_ALLZERO_CFG_INT) | \
		 (1U << DSAF_SBM_XGE_PFC_EN_ALLONE_CFG_INT) | \
		 (1U << DSAF_SBM_XGE_PFC_EN_PART_CFG_INT) | \
		 (1U << DSAF_SBM_XGE_CFG_SET_BUF_INT) | \
		 (1U << DSAF_SBM_XGE_CFG_RESET_BUF_INT) | \
		 (1U << DSAF_VOQ_XGE_ECC_ERR_INT) | \
		 (1U << DSAF_SBM_XGE_MIB_RELS_EXTRA_INT) | \
		 (1U << DSAF_SBM_XGE_MIB_REQ_EXTRA_INT) | \
		 (1U << DSAF_SBM_XGE_MIB_BUF_SUM_ERR_INT) | \
		 (1U << DSAF_SBM_XGE_SRAM_ECC_2BIT_INT) | \
		 (1U << DSAF_SBM_XGE_MIB_RELS_FSM_TIMOUT_INT) | \
		 (1U << DSAF_SBM_XGE_MIB_REQ_FSM_TIMEOUT_INT) | \
		 (1U << DSAF_SBM_XGE_MIB_REQ_FAILED_INT) | \
		 (1U << DSAF_SBM_XGE_LNK_ECC_2BIT_INT) | \
		 (1U << DSAF_SBM_XGE_LNK_FSM_TIMEOUT_INT) | \
		 (1U << DSAF_XID_XGE_SHT_PKT_LEN_INT) | \
		 (1U << DSAF_XID_XGE_LKTB_RSLT_ERR_INT) | \
		 (1U << DSAF_XID_XGE_ECC_ERR_INT)))

#define HNS_DSAF_PPE_IRQ_ENABLE_MSK	\
		(~((1U << DSAF_XOD_PPE_FIFO_WR_FULL_INT) | \
		 (1U << DSAF_XOD_PPE_FIFO_RD_EMPTY_INT) | \
		 (1U << DSAF_VOQ_PPE_ECC_ERR_INT) | \
		 (1U << DSAF_SBM_PPE_CFG_USEFUL_PID_NUM_INT) | \
		 (1U << DSAF_SBM_PPE_MIB_RELS_EXTRA_INT) | \
		 (1U << DSAF_SBM_PPE_MIB_REQ_EXTRA_INT) | \
		 (1U << DSAF_SBM_PPE_MIB_BUF_SUM_ERR_INT) | \
		 (1U << DSAF_SBM_PPE_SRAM_ECC_2BIT_INT) | \
		 (1U << DSAF_SBM_PPE_MIB_RELS_FSM_TIMEOUT_INT) | \
		 (1U << DSAF_SBM_PPE_MIB_REQ_FSM_TIMEOUT_INT) | \
		 (1U << DSAF_SBM_PPE_MIB_REQ_FAILED_INT) | \
		 (1U << DSAF_SBM_PPE_LNK_ECC_2BIT_INT) | \
		 (1U << DSAF_SBM_PPE_LNK_FSM_TIMEOUT_INT) | \
		 (1U << DSAF_XID_PPE_LONG_MCAST_PKT_INT) | \
		 (1U << DSAF_XID_PPE_LKTB_RSLT_ERR_INT)))

#define HNS_DSAF_IRQ_DISABLE_MSK	(0xFFFFFFFFU)


#define DSAF_STATS_READ(p, offset) (*((u64 *)((u8 *)(p) + (offset))))

enum hal_dsaf_mode {
	HRD_DSAF_NO_DSAF_MODE	= 0x0,
	HRD_DSAF_MODE		= 0x1,
};

enum hal_dsaf_tc_mode {
	HRD_DSAF_4TC_MODE		= 0X0,
	HRD_DSAF_8TC_MODE		= 0X1,
};

struct dsaf_vm_def_vlan {
	u32 vm_def_vlan_id;
	u32 vm_def_vlan_cfi;
	u32 vm_def_vlan_pri;
};

struct dsaf_tbl_tcam_data {
	u32 tbl_tcam_data_high;
	u32 tbl_tcam_data_low;
};

#define DSAF_PORT_MSK_NUM \
	((DSAF_TOTAL_QUEUE_NUM + DSAF_SERVICE_NW_NUM - 1) / 32 + 1)
struct dsaf_tbl_tcam_mcast_cfg {
	u8 tbl_mcast_old_en;
	u8 tbl_mcast_item_vld;
	u32 tbl_mcast_port_msk[DSAF_PORT_MSK_NUM];
};

struct dsaf_tbl_tcam_ucast_cfg {
	u32 tbl_ucast_old_en;
	u32 tbl_ucast_item_vld;
	u32 tbl_ucast_mac_discard;
	u32 tbl_ucast_dvc;
	u32 tbl_ucast_out_port;
};

struct dsaf_tbl_line_cfg {
	u32 tbl_line_mac_discard;
	u32 tbl_line_dvc;
	u32 tbl_line_out_port;
};

enum dsaf_port_rate_mode {
	DSAF_PORT_RATE_1000 = 0,
	DSAF_PORT_RATE_2500,
	DSAF_PORT_RATE_10000
};

enum dsaf_stp_port_type {
	DSAF_STP_PORT_TYPE_DISCARD = 0,
	DSAF_STP_PORT_TYPE_BLOCK = 1,
	DSAF_STP_PORT_TYPE_LISTEN = 2,
	DSAF_STP_PORT_TYPE_LEARN = 3,
	DSAF_STP_PORT_TYPE_FORWARD = 4
};

enum dsaf_sw_port_type {
	DSAF_SW_PORT_TYPE_NON_VLAN = 0,
	DSAF_SW_PORT_TYPE_ACCESS = 1,
	DSAF_SW_PORT_TYPE_TRUNK = 2,
};

#define DSAF_SUB_BASE_SIZE                        (0x10000)

/* dsaf mode define */
enum dsaf_mode {
	DSAF_MODE_INVALID = 0,	/**< Invalid dsaf mode */
	DSAF_MODE_ENABLE_FIX,	/**< en DSAF-mode, fixed to queue*/
	DSAF_MODE_ENABLE_0VM,	/**< en DSAF-mode, support 0 VM */
	DSAF_MODE_ENABLE_8VM,	/**< en DSAF-mode, support 8 VM */
	DSAF_MODE_ENABLE_16VM,	/**< en DSAF-mode, support 16 VM */
	DSAF_MODE_ENABLE_32VM,	/**< en DSAF-mode, support 32 VM */
	DSAF_MODE_ENABLE_128VM,	/**< en DSAF-mode, support 128 VM */
	DSAF_MODE_ENABLE,		/**< before is enable DSAF mode*/
	DSAF_MODE_DISABLE_FIX,	/**< non-dasf, fixed to queue*/
	DSAF_MODE_DISABLE_2PORT_8VM,	/**< non-dasf, 2port 8VM */
	DSAF_MODE_DISABLE_2PORT_16VM,	/**< non-dasf, 2port 16VM */
	DSAF_MODE_DISABLE_2PORT_64VM,	/**< non-dasf, 2port 64VM */
	DSAF_MODE_DISABLE_6PORT_0VM,	/**< non-dasf, 6port 0VM */
	DSAF_MODE_DISABLE_6PORT_2VM,	/**< non-dasf, 6port 2VM */
	DSAF_MODE_DISABLE_6PORT_4VM,	/**< non-dasf, 6port 4VM */
	DSAF_MODE_DISABLE_6PORT_16VM,	/**< non-dasf, 6port 16VM */
	DSAF_MODE_MAX		/**< the last one, use as the num */
};

#define DSAF_DEST_PORT_NUM 256	/* DSAF max port num */
#define DSAF_WORD_BIT_CNT 32  /* the num bit of word */
#define DSAF_TC_CHANNEL_NUM 2	/* DSAF tc channel num */

/*mac entry, mc or uc entry*/
struct dsaf_drv_mac_single_dest_entry {
	/* mac addr, match the entry*/
	u8 addr[MAC_NUM_OCTETS_PER_ADDR];
	u16 in_vlan_id; /* value of VlanId */

	/* the vld input port num, dsaf-mode fix 0, */
	/*	non-dasf is the entry whitch port vld*/
	u8 in_port_num;

	u8 port_num; /*output port num*/
	u8 rsv[6];
};

/*only mc entry*/
struct dsaf_drv_mac_multi_dest_entry {
	/* mac addr, match the entry*/
	u8 addr[MAC_NUM_OCTETS_PER_ADDR];
	u16 in_vlan_id;
	/* this mac addr output port,*/
	/*	bit0-bit5 means Port0-Port5(1bit is vld)**/
	u32 port_mask[DSAF_DEST_PORT_NUM / DSAF_WORD_BIT_CNT];

	/* the vld input port num, dsaf-mode fix 0,*/
	/*	non-dasf is the entry whitch port vld*/
	u8 in_port_num;
	u8 rsv[7];
};

struct dsaf_hw_stats {
	u64 pad_drop;
	u64 man_pkts;
	u64 rx_pkts;
	u64 rx_pkt_id;
	u64 rx_pause_frame;
	u64 release_buf_num;
	u64 sbm_drop;
	u64 crc_false;
	u64 bp_drop;
	u64 rslt_drop;
	u64 local_addr_false;
	u64 vlan_drop;
	u64 stp_drop;
	u64 rx_pfc[DSAF_PRIO_NR];
	u64 tx_pfc[DSAF_PRIO_NR];
	u64 tx_pkts;
};

struct hnae_vf_cb {
	u8 port_index;
	struct hns_mac_cb *mac_cb;
	struct dsaf_device *dsaf_dev;
	struct hnae_handle  ae_handle; /* must be the last number */
};

struct dsaf_int_xge_src {
	u32    xid_xge_ecc_err_int_src;
	u32    xid_xge_fsm_timout_int_src;
	u32    sbm_xge_lnk_fsm_timout_int_src;
	u32    sbm_xge_lnk_ecc_2bit_int_src;
	u32    sbm_xge_mib_req_failed_int_src;
	u32    sbm_xge_mib_req_fsm_timout_int_src;
	u32    sbm_xge_mib_rels_fsm_timout_int_src;
	u32    sbm_xge_sram_ecc_2bit_int_src;
	u32    sbm_xge_mib_buf_sum_err_int_src;
	u32    sbm_xge_mib_req_extra_int_src;
	u32    sbm_xge_mib_rels_extra_int_src;
	u32    voq_xge_start_to_over_0_int_src;
	u32    voq_xge_start_to_over_1_int_src;
	u32    voq_xge_ecc_err_int_src;
};

struct dsaf_int_ppe_src {
	u32    xid_ppe_fsm_timout_int_src;
	u32    sbm_ppe_lnk_fsm_timout_int_src;
	u32    sbm_ppe_lnk_ecc_2bit_int_src;
	u32    sbm_ppe_mib_req_failed_int_src;
	u32    sbm_ppe_mib_req_fsm_timout_int_src;
	u32    sbm_ppe_mib_rels_fsm_timout_int_src;
	u32    sbm_ppe_sram_ecc_2bit_int_src;
	u32    sbm_ppe_mib_buf_sum_err_int_src;
	u32    sbm_ppe_mib_req_extra_int_src;
	u32    sbm_ppe_mib_rels_extra_int_src;
	u32    voq_ppe_start_to_over_0_int_src;
	u32    voq_ppe_ecc_err_int_src;
	u32    xod_ppe_fifo_rd_empty_int_src;
	u32    xod_ppe_fifo_wr_full_int_src;
};

struct dsaf_int_rocee_src {
	u32    xid_rocee_fsm_timout_int_src;
	u32    sbm_rocee_lnk_fsm_timout_int_src;
	u32    sbm_rocee_lnk_ecc_2bit_int_src;
	u32    sbm_rocee_mib_req_failed_int_src;
	u32    sbm_rocee_mib_req_fsm_timout_int_src;
	u32    sbm_rocee_mib_rels_fsm_timout_int_src;
	u32    sbm_rocee_sram_ecc_2bit_int_src;
	u32    sbm_rocee_mib_buf_sum_err_int_src;
	u32    sbm_rocee_mib_req_extra_int_src;
	u32    sbm_rocee_mib_rels_extra_int_src;
	u32    voq_rocee_start_to_over_0_int_src;
	u32    voq_rocee_ecc_err_int_src;
};

struct dsaf_int_tbl_src {
	u32    tbl_da0_mis_src;
	u32    tbl_da1_mis_src;
	u32    tbl_da2_mis_src;
	u32    tbl_da3_mis_src;
	u32    tbl_da4_mis_src;
	u32    tbl_da5_mis_src;
	u32    tbl_da6_mis_src;
	u32    tbl_da7_mis_src;
	u32    tbl_sa_mis_src;
	u32    tbl_old_sech_end_src;
	u32    lram_ecc_err1_src;
	u32    lram_ecc_err2_src;
	u32    tram_ecc_err1_src;
	u32    tram_ecc_err2_src;
	u32    tbl_ucast_bcast_xge0_src;
	u32    tbl_ucast_bcast_xge1_src;
	u32    tbl_ucast_bcast_xge2_src;
	u32    tbl_ucast_bcast_xge3_src;
	u32    tbl_ucast_bcast_xge4_src;
	u32    tbl_ucast_bcast_xge5_src;
	u32    tbl_ucast_bcast_ppe_src;
	u32    tbl_ucast_bcast_rocee_src;
};

struct dsaf_int_stat {
	struct dsaf_int_xge_src dsaf_int_xge_stat[DSAF_COMM_CHN];
	struct dsaf_int_ppe_src dsaf_int_ppe_stat[DSAF_COMM_CHN];
	struct dsaf_int_rocee_src dsaf_int_rocee_stat[DSAF_COMM_CHN];
	struct dsaf_int_tbl_src dsaf_int_tbl_stat[1];

};

/* Dsaf device struct define ,and mac ->  dsaf */
struct dsaf_device {
	struct device *dev;
	struct hnae_ae_dev ae_dev;

	u8 __iomem *sc_base;
	u8 __iomem *sds_base;
	u8 __iomem *ppe_base;
	u8 __iomem *io_base;
	u8 __iomem *cpld_base;

	u32 desc_num; /*  desc num per queue*/
	u32 buf_size; /*  ring buffer size */
	int buf_size_type; /* ring buffer size-type */
	enum dsaf_mode dsaf_mode;	 /* dsaf mode  */
	enum hal_dsaf_mode dsaf_en;
	enum hal_dsaf_tc_mode dsaf_tc_mode;
	u32 dsaf_ver;

	struct hns_irq_info irq_xge_info[XGE_XBAR_NUM];
	struct hns_irq_info irq_ppe_info[PPE_XBAR_NUM];

	struct ppe_common_cb *ppe_common[DSAF_COMM_DEV_NUM];
	struct rcb_common_cb *rcb_common[DSAF_COMM_DEV_NUM];
	struct hns_mac_cb *mac_cb;

	struct dsaf_hw_stats hw_stats[DSAF_NODE_NUM];
	struct dsaf_int_stat int_stat;
	/* make sure tcam table config spinlock */
	spinlock_t tcam_lock;
};

static inline void *hns_dsaf_dev_priv(const struct dsaf_device *dsaf_dev)
{
	return (void *)((u8 *)dsaf_dev + sizeof(*dsaf_dev));
}

struct dsaf_drv_tbl_tcam_key {
	union {
		struct {
			u8 mac_3;
			u8 mac_2;
			u8 mac_1;
			u8 mac_0;
		} bits;

		u32 val;
	} high;
	union {
		struct {
#define DSAF_TBL_TCAM_KEY_PORT_S 0
#define DSAF_TBL_TCAM_KEY_PORT_M (((1ULL << 4) - 1) << 0)
#define DSAF_TBL_TCAM_KEY_VLAN_S 4
#define DSAF_TBL_TCAM_KEY_VLAN_M (((1ULL << 12) - 1) << 4)
			u16 port_vlan;
			u8 mac_5;
			u8 mac_4;
		} bits;

		u32 val;
	} low;
};

struct dsaf_drv_soft_mac_tbl {
	struct dsaf_drv_tbl_tcam_key tcam_key;
	u16 index; /*the entry's index in tcam tab*/
};

struct dsaf_drv_priv {
	/* soft tab Mac key, for hardware tab*/
	struct dsaf_drv_soft_mac_tbl *soft_mac_tbl;
};

static inline void hns_dsaf_tbl_tcam_addr_cfg(struct dsaf_device *dsaf_dev,
					      u32 tab_tcam_addr)
{
	dsaf_set_dev_field(dsaf_dev, DSAF_TBL_TCAM_ADDR_0_REG,
			   DSAF_TBL_TCAM_ADDR_M, DSAF_TBL_TCAM_ADDR_S,
			   tab_tcam_addr);
}

static inline void hns_dsaf_tbl_tcam_load_pul(struct dsaf_device *dsaf_dev)
{
	u32 o_tbl_pul;

	o_tbl_pul = dsaf_read_dev(dsaf_dev, DSAF_TBL_PUL_0_REG);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_TCAM_LOAD_S, 1);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_TCAM_LOAD_S, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
}

static inline void hns_dsaf_tbl_line_addr_cfg(struct dsaf_device *dsaf_dev,
					      u32 tab_line_addr)
{
	dsaf_set_dev_field(dsaf_dev, DSAF_TBL_LINE_ADDR_0_REG,
			   DSAF_TBL_LINE_ADDR_M, DSAF_TBL_LINE_ADDR_S,
			   tab_line_addr);
}

static inline int hns_dsaf_get_comm_idx_by_port(int port)
{
	if ((port < DSAF_COMM_CHN) || (port == DSAF_MAX_PORT_NUM_PER_CHIP))
		return 0;
	else
		return (port - DSAF_COMM_CHN + 1);
}

static inline struct hnae_vf_cb *hns_ae_get_vf_cb(
	struct hnae_handle *handle)
{
	return container_of(handle, struct hnae_vf_cb, ae_handle);
}

int hns_dsaf_init_hw(struct dsaf_device *dsaf_dev);
int hns_dsaf_set_mac_uc_entry(struct dsaf_device *dsaf_dev,
	struct dsaf_drv_mac_single_dest_entry *mac_entry,
	struct dsaf_drv_mac_single_dest_entry *mask_entry);
int hns_dsaf_add_mac_mc_port(struct dsaf_device *dsaf_dev,
			     struct dsaf_drv_mac_single_dest_entry *mac_entry);
int hns_dsaf_del_mac_entry(struct dsaf_device *dsaf_dev, u16 vlan_id,
			   u8 in_port_num, u8 *addr);
int hns_dsaf_del_mac_mc_port(struct dsaf_device *dsaf_dev,
			     struct dsaf_drv_mac_single_dest_entry *mac_entry);

void hns_dsaf_rst(struct dsaf_device *dsaf_dev, u32 val);

void hns_dsaf_clk_enable_all(struct dsaf_device *dsaf_dev);

void hns_ppe_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val);

void hns_ppe_com_srst(struct ppe_common_cb *ppe_common, u32 val);

void hns_dsaf_fix_mac_mode(struct hns_mac_cb *mac_cb);

void hns_dsaf_srst_chns(struct dsaf_device *dsaf_dev, u32 msk, u32 val);

void hns_dsaf_roce_srst(struct dsaf_device *dsaf_dev, u32 val);

int hns_dsaf_ae_init(struct dsaf_device *dsaf_dev);
void hns_dsaf_ae_uninit(struct dsaf_device *dsaf_dev);

void hns_dsaf_xge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val);
void hns_dsaf_ge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val);

void hns_dsaf_update_stats(struct dsaf_device *dsaf_dev, u32 inode_num);

int hns_dsaf_get_sset_count(struct dsaf_device *dsaf_dev, int stringset);
void hns_dsaf_get_stats(struct dsaf_device *ddev, u64 *data, int port);
void hns_dsaf_get_strings(int stringset, u8 *data, int port,
			  struct dsaf_device *dsaf_dev);
int hns_dsaf_xbar_irq_init(struct dsaf_device *dsaf_dev, u32 port_id);
void hns_dsaf_xbar_irq_free(struct dsaf_device *dsaf_dev, int port_id);
void hns_dsaf_get_regs(struct dsaf_device *ddev, u32 port, void *data);
int hns_dsaf_get_regs_count(void);
void hns_dsaf_set_promisc(struct dsaf_device *dsaf_dev, u32 port_id, u32 en);
int hns_ae_get_mdio_reg(struct hnae_handle *handle, unsigned int page,
			unsigned int reg, unsigned int *regvalue);
int hns_ae_set_mdio_reg(struct hnae_handle *handle, unsigned int page,
			unsigned int reg, unsigned int regvalue);
int hns_dsaf_pfc_en_cfg(struct dsaf_device *dsaf_dev,
			int mac_id, int tc_en, int tx_en, int rx_en);
void hns_dsaf_pfc_set_tc_en(struct dsaf_device *dsaf_dev, int mac_id,
			    u8 tc, u8 en);
void hns_dsaf_pfc_get_tc_en(struct dsaf_device *dsaf_dev, int mac_id,
			    u8 tc, u8 *en);
void hns_dsaf_ets_set_up2tc(struct dsaf_device *dsaf_dev, int mac_id,
			    u32 prio_tc);
void hns_dsaf_ets_get_up2tc(struct dsaf_device *dsaf_dev, int mac_id,
			    u32 *prio_tc);
void hns_dsaf_sw_port_type_cfg(struct dsaf_device *dsaf_dev, u32 sw_port,
			       enum dsaf_sw_port_type port_type);

void hns_dsaf_get_rx_mac_pause_en(struct dsaf_device *dsaf_dev, int mac_id,
				  u32 *en);
int hns_dsaf_set_rx_mac_pause_en(struct dsaf_device *dsaf_dev, int mac_id,
				 u32 en);
void hns_dsaf_pfc_get_pause_en(struct dsaf_device *dsaf_dev, int mac_id,
			       u8 *en);
void hns_dsaf_pfc_set_pause_en(struct dsaf_device *dsaf_dev, int mac_id,
			       u8 en);
void hns_dsaf_tcam_uc_get(struct dsaf_device *dsaf_dev, u32 address,
			  struct dsaf_tbl_tcam_data *ptbl_tcam_data,
			  struct dsaf_tbl_tcam_ucast_cfg *ptbl_tcam_ucast);
void hns_dsaf_tcam_mc_get(struct dsaf_device *dsaf_dev, u32 address,
			  struct dsaf_tbl_tcam_data *ptbl_tcam_data,
			  struct dsaf_tbl_tcam_mcast_cfg *ptbl_tcam_mcast);

void hns_dsaf_tcam_addr_get(struct dsaf_drv_tbl_tcam_key *mac_key, u8 *addr);
int hns_dsaf_wait_pkt_clean(struct dsaf_device *dsaf_dev, int port);

void hns_dsaf_irq_set(struct dsaf_device *dsaf_dev, int port, int en);
void hns_dsaf_irq_clear(struct dsaf_device *dsaf_dev, int port);

void hns_dsaf_sw_port_type_rstr(struct dsaf_device *dsaf_dev, u32 port);
void hns_dsaf_commit_pause_mode(struct dsaf_device *dsaf_dev,
				int channel, u8 pfc_en);
void hns_dsaf_waterline_ex_init(struct dsaf_device *dsaf_dev);

#endif /* __HNS_DSAF_MAIN_H__ */
