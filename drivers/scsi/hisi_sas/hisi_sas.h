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

#ifndef _HISI_SAS_H_
#define _HISI_SAS_H_

#include <linux/acpi.h>
#include <linux/dmapool.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <scsi/sas_ata.h>
#include <scsi/libsas.h>

#define DRV_VERSION "v1.2"

#define HISI_SAS_INVALID_PORT_ID	0xff
#define SATA_PROTOCOL_NONDATA	0x1
#define SATA_PROTOCOL_PIO		0x2
#define SATA_PROTOCOL_DMA		0x4
#define SATA_PROTOCOL_FPDMA		0x8
#define SATA_PROTOCOL_ATAPI		0x10

#define HISI_SAS_MAX_PHYS	9
#define HISI_SAS_MAX_QUEUES	32
#define HISI_SAS_QUEUE_SLOTS 512
#define HISI_SAS_MAX_ITCT_ENTRIES 2048
#define HISI_SAS_MAX_DEVICES HISI_SAS_MAX_ITCT_ENTRIES
#define HISI_SAS_RESET_BIT	0

#define HISI_SAS_STATUS_BUF_SZ \
		(sizeof(struct hisi_sas_err_record) + 1024)
#define HISI_SAS_COMMAND_TABLE_SZ \
		(((sizeof(union hisi_sas_command_table)+3)/4)*4)

#define HISI_SAS_MAX_SSP_RESP_SZ (sizeof(struct ssp_frame_hdr) + 1024)
#define HISI_SAS_MAX_SMP_RESP_SZ 1028
#define HISI_SAS_MAX_STP_RESP_SZ 28

#define DEV_IS_EXPANDER(type) \
	((type == SAS_EDGE_EXPANDER_DEVICE) || \
	(type == SAS_FANOUT_EXPANDER_DEVICE))

struct hisi_hba;

enum {
	PORT_TYPE_SAS = (1U << 1),
	PORT_TYPE_SATA = (1U << 0),
};

enum dev_status {
	HISI_SAS_DEV_NORMAL,
	HISI_SAS_DEV_EH,
};

enum hisi_sas_dev_type {
	HISI_SAS_DEV_TYPE_STP = 0,
	HISI_SAS_DEV_TYPE_SSP,
	HISI_SAS_DEV_TYPE_SATA,
};
struct hisi_fatal_stat {
	/* ecc */
	unsigned int iost_1b_ecc_err_cnt;
	unsigned int iost_multib_ecc_err_cnt;
	unsigned int dq_1b_ecc_err_cnt;
	unsigned int dq_multib_ecc_err_cnt;
	unsigned int itct_1b_ecc_err_cnt;
	unsigned int itct_multib_ecc_err_cnt;
	unsigned int iost_mem_multib_ecc_err_cnt;
	unsigned int iost_mem_1b_ecc_err_cnt;
	unsigned int itct_mem_multib_ecc_err_cnt;
	unsigned int itct_mem_1b_ecc_err_cnt;
	unsigned int cq_multib_ecc_err_cnt;
	unsigned int cq_1b_ecc_err_cnt;
	unsigned int ncq_mem0_multib_ecc_err_cnt;
	unsigned int ncq_mem0_1b_ecc_err_cnt;
	unsigned int ncq_mem1_multib_ecc_err_cnt;
	unsigned int ncq_mem1_1b_ecc_err_cnt;
	unsigned int ncq_mem2_multib_ecc_err_cnt;
	unsigned int ncq_mem2_1b_ecc_err_cnt;
	unsigned int ncq_mem3_multib_ecc_err_cnt;
	unsigned int ncq_mem3_1b_ecc_err_cnt;

	/* axi */
	unsigned int overfl_axi_err_cnt;
	unsigned int axi_rob_1b_ecc_err_cnt;
	unsigned int axi_rob_multib_ecc_err_cnt;
	unsigned int axi_1b_ecc_err_cnt;
	unsigned int dmac_tx_ecc_1b_err;
	unsigned int dmac_rx_ecc_1b_err;
	unsigned int dmac_bus_int_err_cnt;

	unsigned int software_dq_wp_depth_err;
	unsigned int hgc_free_dq_iptt_slot_unmatched_err;
	unsigned int software_cq_rp_depth_err;
	unsigned int axi_rsp_err;
	unsigned int hgc_iomb_fifo_overflow_err;
	unsigned int lm_list_opt_err;
	unsigned int sas_hgc_abt_lm_list_err;
};

struct hisi_sas_phy {
	struct hisi_hba	*hisi_hba;
	struct hisi_sas_port	*port;
	struct asd_sas_phy	sas_phy;
	struct sas_identify	identify;
	struct timer_list	timer;
	struct work_struct	phyup_ws;
	u64		port_id; /* from hw */
	u64		dev_sas_addr;
	u64		phy_type;
	u64		frame_rcvd_size;
	u8		frame_rcvd[32];
	u8		phy_attached;

	/*
	* lldd will ignore the flutter phy up or down
	* or bcast which caused by i_t_nexus_reset.
	*/
	u8  is_flutter;

	/* chip bug workaround */
	u8  need_reject_remote_stp_link;
	u8		reserved[1];
	enum sas_linkrate	linkrate;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate;
};

struct hisi_sas_port {
	struct asd_sas_port	sas_port;
	unsigned long phy_bitmap;
	u8	port_attached;
	u8	id; /* from hw */
	struct list_head	list;
};

struct hisi_sas_cq {
	struct hisi_hba *hisi_hba;
	int	rd_point;
	unsigned int id;
};

struct hisi_sas_dq {
	struct hisi_hba *hisi_hba;
	spinlock_t lock;
	int	wr_point;
	int	id;
};

struct hisi_sas_device {
	enum sas_device_type	dev_type;
	struct hisi_hba		*hisi_hba;
	struct domain_device	*sas_device;
	u64 attached_phy;
	u32 device_id;
	u64 running_req;
	u8 dev_status;
	u8 port_id;
	int sata_idx;
};

enum hisi_sas_cmd_type {
	HISI_CMD_TYPE_NONE = 0,
	HISI_CMD_TYPE_NORMAL,
	HISI_CMD_TYPE_ABORT_SINGLE,
	HISI_CMD_TYPE_ABORT_DEV,
	HISI_CMD_TYPE_TMF,
};

enum hisi_slot_state {
	HISI_SLOT_FREE = 0U,
	HISI_SLOT_RUNNING,
	HISI_SLOT_EXTERNAL_RECOVER,
	HISI_SLOT_MAX
};

struct hisi_sas_slot {
	struct list_head entry;
	struct sas_task *task;
	struct hisi_sas_port	*port;
	spinlock_t   lock;
	u64	n_elem;
	int	dlvry_queue;
	int	dlvry_queue_slot;
	int	cmplt_queue;
	int	cmplt_queue_slot;
	unsigned int idx;
	int	abort;
	struct hisi_sas_device *device;
	enum hisi_sas_cmd_type cmd_type;
	enum hisi_slot_state state;
	void	*cmd_hdr;
	dma_addr_t cmd_hdr_dma;
	void	*status_buffer;
	dma_addr_t status_buffer_dma;
	void *command_table;
	dma_addr_t command_table_dma;
	struct hisi_sas_sge_page *sge_page;
	dma_addr_t sge_page_dma;
#ifdef SAS_DIF
	struct hisi_sas_sge_page *sge_dif_page;
	dma_addr_t sge_dif_page_dma;
#endif
#ifdef SLOT_ABORT
	struct work_struct abort_slot;
#endif
};

enum hisi_slot_event {
	HISI_SLOT_EVENT_START_IO,
	HISI_SLOT_EVENT_START_FAIL,
	HISI_SLOT_EVENT_PARSE_CQ_ENTRY,
	HISI_SLOT_EVENT_EX_ABT,/* abort task */
	HISI_SLOT_EVENT_EX_QT,/* query task */
	HISI_SLOT_EVENT_EX_ABTS,/* abort task set */
	HISI_SLOT_EVENT_EX_LR,/* LU reset */
	HISI_SLOT_EVENT_EX_ITNR,/* I_T nexus reset */
	HISI_SLOT_EVENT_EX_CNP,/* clear nexus port */
	HISI_SLOT_EVENT_EX_CNH,/* clear nexus ha */
	HISI_SLOT_EVENT_EX_EH_OK,
	HISI_SLOT_EVENT_COMPLETE,
	HISI_SLOT_EVENT_MAX
};

struct hisi_sas_io_context {
	enum hisi_slot_event event;
	struct hisi_hba *hba;
	struct hisi_sas_port *port;
	struct hisi_sas_tmf_task *tmf;
	struct sas_task *task;
	struct hisi_sas_device *dev;
	int abort_flag;
	int abort_tag;
	u64	n_elem;
	int	iptt;
	int is_tmf;
	int rc;
	int lck_rc;
	int (*handler)(struct hisi_sas_slot *slot,
			struct hisi_sas_io_context *context);
	int (*handler_locked)(struct hisi_sas_slot *slot,
			struct hisi_sas_io_context *context);
};

struct hisi_sas_tmf_task {
	u8 tmf;
	u16 tag_of_task_to_be_managed;
};

struct hisi_sas_hw {
	int (*hw_init)(struct hisi_hba *hisi_hba);
	void (*setup_itct)(struct hisi_hba *hisi_hba,
			struct hisi_sas_device *device);
	int (*slot_index_alloc)(struct hisi_hba *hisi_hba, int *slot_idx,
				struct domain_device *device);
	struct hisi_sas_device *(*alloc_dev)(struct domain_device *device);
	void (*sl_notify)(struct hisi_hba *hisi_hba, int phy_no);
	int (*get_free_slot)(struct hisi_hba *hisi_hba,
				u32 dev_id, int *q, int *s);
	void (*start_delivery)(struct hisi_hba *hisi_hba, int dq_id);
	int (*prep_ssp)(struct hisi_hba *hisi_hba,
			struct hisi_sas_slot *slot, int is_tmf,
			struct hisi_sas_tmf_task *tmf);
	int (*prep_smp)(struct hisi_hba *hisi_hba,
			struct hisi_sas_slot *slot);
	int (*prep_stp)(struct hisi_hba *hisi_hba,
			struct hisi_sas_slot *slot);
	int (*prep_abort)(struct hisi_hba *hisi_hba,
			struct hisi_sas_slot *slot,
			unsigned int device_id, unsigned int abort_flag,
			unsigned int tag_to_abort);
	int (*slot_complete)(struct hisi_hba *hisi_hba,
			struct hisi_sas_slot *slot, int abort);
	void (*phy_enable)(struct hisi_hba *hisi_hba, int phy_no);
	void (*phy_disable)(struct hisi_hba *hisi_hba, int phy_no);
	void (*phy_set_link_rate)(struct hisi_hba *hisi_hba, int phy_no,
			void *data);
	void (*phy_hard_reset)(struct hisi_hba *hisi_hba, int phy_no);
	void (*free_device)(struct hisi_hba *hisi_hba,
			struct hisi_sas_device *dev, int gone);
	int (*get_wideport_bitmap)(struct hisi_hba *hisi_hba, int port_id);
	int (*get_phy_state)(struct hisi_hba *hisi_hba, int phy_id);
	int (*soft_reset)(struct hisi_hba *hisi_hba);
	int (*chip_fatal_check)(struct hisi_hba *hisi_hba);
	void (*send_break)(struct hisi_hba *hisi_hba,
			struct domain_device *device);
	int max_command_entries;
	int complete_hdr_size;

#ifdef SAS_DIF
	u32 prot_cap;
#endif
};

struct hisi_hba {
	/* This must be the first element, used by SHOST_TO_SAS_HA */
	struct sas_ha_struct *p;

	struct platform_device *pdev;
	void __iomem *regs;
	struct regmap *ctrl;
	u32 ctrl_reset_reg;
	u32 ctrl_reset_sts_reg;
	u32 ctrl_clock_ena_reg;
	u8 sas_addr[SAS_ADDR_SIZE];

	int n_phy;
	int scan_finished;
	spinlock_t lock;

	struct timer_list timer;
	struct workqueue_struct *wq;

	int slot_index_count;
	unsigned long *slot_index_tags;

	/* SCSI/SAS glue */
	struct sas_ha_struct sha;
	struct Scsi_Host *shost;

	struct hisi_sas_cq cq[HISI_SAS_MAX_QUEUES];
	struct hisi_sas_dq dq[HISI_SAS_MAX_QUEUES];
	struct hisi_sas_phy phy[HISI_SAS_MAX_PHYS];
	struct hisi_sas_port port[HISI_SAS_MAX_PHYS];

	int	queue_count;
	int	queue;
	struct hisi_sas_slot *slot_prep[HISI_SAS_MAX_QUEUES];

	struct dma_pool *sge_page_pool;
#ifdef SAS_DIF
	struct dma_pool *sge_dif_page_pool;
#endif
	struct hisi_sas_device	devices[HISI_SAS_MAX_DEVICES];
	struct dma_pool *command_table_pool;
	struct dma_pool *status_buffer_pool;
	struct hisi_sas_cmd_hdr	*cmd_hdr[HISI_SAS_MAX_QUEUES];
	dma_addr_t cmd_hdr_dma[HISI_SAS_MAX_QUEUES];
	void *complete_hdr[HISI_SAS_MAX_QUEUES];
	dma_addr_t complete_hdr_dma[HISI_SAS_MAX_QUEUES];
	struct hisi_sas_initial_fis *initial_fis;
	dma_addr_t initial_fis_dma;
	struct hisi_sas_itct *itct;
	dma_addr_t itct_dma;
	struct hisi_sas_iost *iost;
	dma_addr_t iost_dma;
	struct hisi_sas_breakpoint *breakpoint;
	dma_addr_t breakpoint_dma;
	struct hisi_sas_breakpoint *sata_breakpoint;
	dma_addr_t sata_breakpoint_dma;
	struct hisi_sas_slot	*slot_info;
	const struct hisi_sas_hw *hw;	/* Low level hw interface */
	struct timer_list link_timer;
	unsigned long rst_start_time;
	unsigned long rst_flag;
	int core_id;
	struct timer_list routine_timer;
	struct hisi_fatal_stat fatal_stat;
	/* work used for sas controller reset  */
	struct work_struct rst_work;
	/* chip bug workaround:v2 hw only support 63 sata devices */
	unsigned long long sata_dev_bitmap;
	struct device_node *djtag_node;
};

/* Generic HW DMA host memory structures */
/* Delivery queue header */
struct hisi_sas_cmd_hdr {
	/* dw0 */
	__le32 dw0;

	/* dw1 */
	__le32 dw1;

	/* dw2 */
	__le32 dw2;

	/* dw3 */
	__le32 transfer_tags;

	/* dw4 */
	__le32 data_transfer_len;

	/* dw5 */
	__le32 first_burst_num;

	/* dw6 */
	__le32 sg_len;

	/* dw7 */
	__le32 dw7;

	/* dw8-9 */
	__le64 cmd_table_addr;

	/* dw10-11 */
	__le64 sts_buffer_addr;

	/* dw12-13 */
	__le64 prd_table_addr;

	/* dw14-15 */
	__le64 dif_prd_table_addr;
};

struct hisi_sas_itct {
	__le64 qw0;
	__le64 sas_addr;
	__le64 qw2;
	__le64 qw3;
	__le64 qw4_15[12];
};

struct hisi_sas_iost {
	__le64 qw0;
	__le64 qw1;
	__le64 qw2;
	__le64 qw3;
};

struct hisi_sas_err_record {
	u32	data[4];
};

struct hisi_sas_initial_fis {
	struct hisi_sas_err_record err_record;
	struct dev_to_host_fis fis;
	u32 rsvd[3];
};

struct hisi_sas_breakpoint {
	u8	data[128];	/*io128 byte*/
};

struct hisi_sas_sge {
	__le64 addr;
	__le32 page_ctrl_0;
	__le32 page_ctrl_1;
	__le32 data_len;
	__le32 data_off;
};

struct hisi_sas_command_table_smp {
	u8 bytes[44];
};

struct hisi_sas_command_table_stp {
	struct	host_to_dev_fis command_fis;
	u8	dummy[12];
	u8	atapi_cdb[ATAPI_CDB_LEN];
};

#define HISI_SAS_SGE_PAGE_CNT SCSI_MAX_SG_SEGMENTS
struct hisi_sas_sge_page {
	struct hisi_sas_sge sge[HISI_SAS_SGE_PAGE_CNT];
};

struct hisi_sas_command_table_ssp {
	struct ssp_frame_hdr hdr;
	union {
		struct {
			struct ssp_command_iu task;
			u32 prot[6];
		};
		struct ssp_tmf_iu ssp_task;
		struct xfer_rdy_iu xfer_rdy;
		struct ssp_response_iu ssp_res;
	} u;
};

union hisi_sas_command_table {
	struct hisi_sas_command_table_ssp ssp;
	struct hisi_sas_command_table_smp smp;
	struct hisi_sas_command_table_stp stp;
};
extern int hisi_sas_probe(struct platform_device *pdev,
			const struct hisi_sas_hw *ops);
extern int hisi_sas_remove(struct platform_device *pdev);

extern void hisi_sas_phy_down(struct hisi_hba *hisi_hba, int phy_no, int rdy);
extern void hisi_sas_slot_task_free(struct hisi_hba *hisi_hba,
				struct sas_task *task,
				struct hisi_sas_slot *slot);
u8 hisi_sas_get_ata_protocol(u8 cmd, int direction);

int hisi_sas_is_rw_cmd(unsigned char op);

int hisi_sas_is_ata_rw_cmd(struct sas_task *task);

int hisi_sas_free_slot(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context);

int hisi_sas_free_iptt(struct hisi_sas_slot *slot,
			struct hisi_sas_io_context *context);

int hisi_sas_parse_cq_entry(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context);
int hisi_sas_complete_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context);

int hisi_sas_slot_fsm(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context);
#endif
