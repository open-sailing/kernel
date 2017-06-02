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
#define DRV_NAME "hisi_sas"

#define DEV_IS_GONE(dev) \
	((!dev) || (dev->dev_type == SAS_PHY_UNUSED))

#define DEV_IS_IN_EH_STATE(dev) \
	((dev) && (dev->dev_status == HISI_SAS_DEV_EH))

static int hisi_sas_debug_issue_ssp_tmf(struct domain_device *device,
				u8 *lun, struct hisi_sas_tmf_task *tmf);
static int hisi_sas_softreset_ata_disk(struct domain_device *device);
static int hisi_sas_debug_I_T_nexus_reset(struct domain_device *device);
static int hisi_sas_chip_reset(struct hisi_hba *hisi_hba);
static void hisi_sas_cnh_eh_all_slots(struct hisi_hba *hisi_hba);
static void hisi_sas_cnh_release_all_slots(struct hisi_hba *hisi_hba);
static void hisi_sas_refresh_dev_info(struct asd_sas_phy *sas_phy);
static void hisi_sas_dev_gone(struct domain_device *device);

u8 hisi_sas_get_ata_protocol(u8 cmd, int direction)
{
	switch (cmd) {
	case ATA_CMD_FPDMA_WRITE:
	case ATA_CMD_FPDMA_READ:
	return SATA_PROTOCOL_FPDMA;

	case ATA_CMD_ID_ATA:
	case ATA_CMD_PMP_READ:
	case ATA_CMD_READ_LOG_EXT:
	case ATA_CMD_PIO_READ:
	case ATA_CMD_PIO_READ_EXT:
	case ATA_CMD_PMP_WRITE:
	case ATA_CMD_WRITE_LOG_EXT:
	case ATA_CMD_PIO_WRITE:
	case ATA_CMD_PIO_WRITE_EXT:
	case ATA_CMD_DOWNLOAD_MICRO:
	return SATA_PROTOCOL_PIO;

	case ATA_CMD_READ:
	case ATA_CMD_READ_EXT:
	case ATA_CMD_READ_LOG_DMA_EXT:
	case ATA_CMD_WRITE:
	case ATA_CMD_WRITE_EXT:
	case ATA_CMD_WRITE_QUEUED:
	case ATA_CMD_WRITE_LOG_DMA_EXT:
	case ATA_CMD_DSM:
	case ATA_CMD_READ_STREAM_DMA_EXT:
	case ATA_CMD_WRITE_STREAM_DMA_EXT:
	case ATA_CMD_WRITE_FUA_EXT:
	case ATA_CMD_TRUSTED_RCV_DMA:
	case ATA_CMD_TRUSTED_SND_DMA:
	case ATA_CMD_DOWNLOAD_MICRO_DMA:
	case ATA_CMD_PMP_READ_DMA:
	case ATA_CMD_PMP_WRITE_DMA:
	return SATA_PROTOCOL_DMA;

	case ATA_CMD_EDD:
	case ATA_CMD_DEV_RESET:
	case ATA_CMD_CHK_POWER:
	case ATA_CMD_FLUSH:
	case ATA_CMD_FLUSH_EXT:
	case ATA_CMD_VERIFY:
	case ATA_CMD_VERIFY_EXT:
	case ATA_CMD_SET_FEATURES:
	case ATA_CMD_STANDBY:
	case ATA_CMD_STANDBYNOW1:
	return SATA_PROTOCOL_NONDATA;
	default:
		if (direction == DMA_NONE)
			return SATA_PROTOCOL_NONDATA;
		return SATA_PROTOCOL_PIO;
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_get_ata_protocol);

int hisi_sas_is_rw_cmd(unsigned char op)
{
	return op == READ_6 || op == WRITE_6 ||
	    op == READ_10 || op == WRITE_10 ||
	    op == READ_12 || op == WRITE_12 ||
		op == READ_16 || op == WRITE_16;
}
EXPORT_SYMBOL_GPL(hisi_sas_is_rw_cmd);

int hisi_sas_is_ata_rw_cmd(struct sas_task *task)
{
	u32 cmd_proto;

	cmd_proto = hisi_sas_get_ata_protocol(task->ata_task.fis.command,
						  task->data_dir);

	return (cmd_proto != SATA_PROTOCOL_NONDATA);
}
EXPORT_SYMBOL_GPL(hisi_sas_is_ata_rw_cmd);

static struct hisi_hba *dev_to_hisi_hba(struct domain_device *device)
{
	return device->port->ha->lldd_ha;
}
static inline int hisi_sas_get_dq_id(struct hisi_hba *hisi_hba,
				struct hisi_sas_device *hisi_sas_dev)
{
	int qid = 0;

	qid = hisi_sas_dev->device_id % hisi_hba->queue_count;

	return qid;
}


static void hisi_sas_slot_index_clear(struct hisi_hba *hisi_hba, int slot_idx)
{
	void *bitmap = hisi_hba->slot_index_tags;

	clear_bit(slot_idx, bitmap);
}

static void hisi_sas_slot_index_free(struct hisi_hba *hisi_hba, int slot_idx)
{
	hisi_sas_slot_index_clear(hisi_hba, slot_idx);
}

static void hisi_sas_slot_index_set(struct hisi_hba *hisi_hba, int slot_idx)
{
	void *bitmap = hisi_hba->slot_index_tags;

	set_bit(slot_idx, bitmap);
}

static int hisi_sas_slot_index_alloc(struct hisi_hba *hisi_hba, int *slot_idx)
{
	unsigned int index;
	void *bitmap = hisi_hba->slot_index_tags;

	index = find_first_zero_bit(bitmap, hisi_hba->slot_index_count);
	if (index >= hisi_hba->slot_index_count)
		return -SAS_QUEUE_FULL;
	hisi_sas_slot_index_set(hisi_hba, index);
	*slot_idx = index;
	return 0;
}

static void hisi_sas_slot_index_init(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->slot_index_count; ++i)
		hisi_sas_slot_index_clear(hisi_hba, i);
}

void hisi_sas_slot_task_free(struct hisi_hba *hisi_hba, struct sas_task *task,
			     struct hisi_sas_slot *slot)
{
	struct hisi_sas_io_context context;

	memset(&context, 0, sizeof(struct hisi_sas_io_context));
	context.event = HISI_SLOT_EVENT_COMPLETE;
	context.hba = hisi_hba;
	context.task = task;
	context.iptt = slot->idx;
	context.handler = hisi_sas_free_iptt;
	context.handler_locked = hisi_sas_free_slot;
	hisi_sas_slot_fsm(slot, &context);
}
EXPORT_SYMBOL_GPL(hisi_sas_slot_task_free);

int hisi_sas_slot_state_move(struct hisi_sas_slot *slot,
			enum hisi_slot_event event)
{
	int rc = -1;

	switch (slot->state) {
	case HISI_SLOT_FREE:
		if (event == HISI_SLOT_EVENT_START_IO) {
			rc = 0;
			slot->state = HISI_SLOT_RUNNING;
		}
		break;

	case HISI_SLOT_RUNNING:
		 if (event == HISI_SLOT_EVENT_PARSE_CQ_ENTRY) {
			rc = 0;
			slot->state = HISI_SLOT_RUNNING;


		} else if ((event == HISI_SLOT_EVENT_COMPLETE)
				|| (event == HISI_SLOT_EVENT_START_FAIL)) {
			rc = 0;
			slot->state = HISI_SLOT_FREE;


		} else if ((event == HISI_SLOT_EVENT_EX_ABT)
				|| (event == HISI_SLOT_EVENT_EX_QT)
				|| (event == HISI_SLOT_EVENT_EX_ABTS)
				|| (event == HISI_SLOT_EVENT_EX_LR)
				|| (event == HISI_SLOT_EVENT_EX_ITNR)
				|| (event == HISI_SLOT_EVENT_EX_CNP)
				|| (event == HISI_SLOT_EVENT_EX_CNH)) {
			rc = 0;
			slot->state = HISI_SLOT_EXTERNAL_RECOVER;

		}

		break;

	case HISI_SLOT_EXTERNAL_RECOVER:
		if (event == HISI_SLOT_EVENT_EX_EH_OK) {
			rc = 0;
			slot->state = HISI_SLOT_FREE;

		} else if ((event == HISI_SLOT_EVENT_EX_ABT)
				|| (event == HISI_SLOT_EVENT_EX_QT)
				|| (event == HISI_SLOT_EVENT_EX_ABTS)
				|| (event == HISI_SLOT_EVENT_EX_LR)
				|| (event == HISI_SLOT_EVENT_EX_ITNR)
				|| (event == HISI_SLOT_EVENT_EX_CNP)
				|| (event == HISI_SLOT_EVENT_EX_CNH)) {
			rc = 0;
			slot->state = HISI_SLOT_EXTERNAL_RECOVER;


		}
		break;

	default:
		break;
	}

	return rc;
}

int hisi_sas_slot_fsm(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&slot->lock, flags);
	rc = hisi_sas_slot_state_move(slot, context->event);
	if (!rc) {
		if (context->handler_locked)
			context->lck_rc = context->handler_locked(slot,
				context);
	}

	spin_unlock_irqrestore(&slot->lock, flags);

	if (!rc) {
		if (context->handler)
			context->rc = context->handler(slot, context);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(hisi_sas_slot_fsm);

static int hisi_sas_task_prep_smp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	return hisi_hba->hw->prep_smp(hisi_hba, slot);
}

static int hisi_sas_task_prep_ssp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot, int is_tmf,
				  struct hisi_sas_tmf_task *tmf)
{
	return hisi_hba->hw->prep_ssp(hisi_hba, slot, is_tmf, tmf);
}

static int hisi_sas_task_prep_ata(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	return hisi_hba->hw->prep_stp(hisi_hba, slot);
}

#ifdef SLOT_ABORT
static void hisi_sas_slot_abort(struct work_struct *work)
{
	struct hisi_sas_slot *abort_slot =
		container_of(work, struct hisi_sas_slot, abort_slot);
	struct sas_task *task = abort_slot->task;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(task->dev);
	struct scsi_cmnd *cmnd = task->uldd_task;
	struct hisi_sas_tmf_task tmf_task;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct scsi_lun lun;
	struct device *dev = &hisi_hba->pdev->dev;
	int tag = abort_slot->idx;

	if (!(task->task_proto & SAS_PROTOCOL_SSP)) {
		dev_err(dev, "cannot abort slot for non-ssp task\n");
		goto out;
	}

	int_to_scsilun(cmnd->device->lun, &lun);
	tmf_task.tmf = TMF_ABORT_TASK;
	tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

	hisi_sas_debug_issue_ssp_tmf(task->dev, lun.scsi_lun, &tmf_task);
out:
	hisi_sas_slot_task_free(hisi_hba, task, abort_slot);
	if (task->task_done)
		task->task_done(task);
	if (sas_dev && sas_dev->running_req)
		sas_dev->running_req--;
}
#endif

static int hisi_sas_task_prep_abort(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	return hisi_hba->hw->prep_abort(hisi_hba, slot,
			device_id, abort_flag, tag_to_abort);
}

static inline void hisi_sas_init_slot(struct hisi_sas_slot *slot)
{
	memset(slot->status_buffer, 0, HISI_SAS_STATUS_BUF_SZ);
	memset(slot->command_table, 0, HISI_SAS_COMMAND_TABLE_SZ);
	memset(slot->sge_page, 0, sizeof(struct hisi_sas_sge_page));

#ifdef SAS_DIF
	memset(slot->sge_dif_page, 0, sizeof(struct hisi_sas_sge_page));
#endif

	slot->task = NULL;
	slot->port = NULL;
	slot->device = NULL;
	slot->n_elem = 0;
	slot->dlvry_queue = 0;
	slot->dlvry_queue_slot = 0;
	slot->cmplt_queue = 0;
	slot->cmplt_queue_slot = 0;
	slot->cmd_type = HISI_CMD_TYPE_NONE;
	slot->cmd_hdr = NULL;
	slot->cmd_hdr_dma = (dma_addr_t)0;
}

int hisi_sas_free_slot(struct hisi_sas_slot *slot,
	struct hisi_sas_io_context *context)
{
	struct hisi_hba *hisi_hba = context->hba;
	struct device *dev = &hisi_hba->pdev->dev;
	struct sas_task *task = context->task;

	if (!slot->task)
		return -1;

	if (!sas_protocol_ata(task->task_proto))
		if (slot->n_elem)
			dma_unmap_sg(dev, task->scatter, slot->n_elem,
				     task->data_dir);

#ifdef SAS_DIF
	if (task->ssp_task.cmd)
		if (scsi_prot_sg_count(task->ssp_task.cmd))
			dma_unmap_sg(dev,
					scsi_prot_sglist(task->ssp_task.cmd),
					scsi_prot_sg_count(task->ssp_task.cmd),
					task->data_dir);
#endif
	task->lldd_task = NULL;

	hisi_sas_init_slot(slot);
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_free_slot);

int hisi_sas_free_iptt(struct hisi_sas_slot *slot,
			struct hisi_sas_io_context *context)
{
	struct hisi_hba *hisi_hba = context->hba;
	int iptt = context->iptt;
	unsigned long flags;

	/* iptt is a global resouce, so need hba lock */
	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_sas_slot_index_free(hisi_hba, iptt);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	return 0;
}

int hisi_sas_parse_cq_entry(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	struct hisi_hba *hisi_hba = context->hba;

	hisi_hba->hw->slot_complete(hisi_hba, slot, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_parse_cq_entry);

int hisi_sas_complete_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	struct sas_task *task = context->task;
	unsigned long flags;
	struct hisi_sas_device *sas_dev = context->dev;

	if (sas_dev && sas_dev->running_req)
		sas_dev->running_req--;

	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags &=
		~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	if (task->task_done)
		task->task_done(task);

	hisi_sas_free_iptt(slot, context);
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_complete_io);

static int hisi_sas_start_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	int rc = 0;
	struct hisi_hba *hisi_hba = context->hba;
	struct hisi_sas_cmd_hdr	*cmd_hdr_base;
	struct sas_task *task = context->task;
	int dlvry_queue_slot, dlvry_queue;
	struct hisi_sas_device *sas_dev = context->dev;
	int queue_id = 0;
	unsigned long flags;
	struct hisi_sas_dq *dq;
	struct device *dev = &hisi_hba->pdev->dev;

	queue_id = hisi_sas_get_dq_id(hisi_hba, sas_dev);
	dq = &hisi_hba->dq[queue_id];
	spin_lock_irqsave(&dq->lock, flags);

	rc = hisi_hba->hw->get_free_slot(hisi_hba, sas_dev->device_id,
					&dlvry_queue, &dlvry_queue_slot);
	if (rc) {
		spin_unlock_irqrestore(&dq->lock, flags);
		goto err_out;
	}
	if (!context->tmf)
		slot->cmd_type = HISI_CMD_TYPE_NORMAL;
	else
		slot->cmd_type = HISI_CMD_TYPE_TMF;

	slot->device = sas_dev;
	slot->n_elem = context->n_elem;
	slot->dlvry_queue = dlvry_queue;
	slot->dlvry_queue_slot = dlvry_queue_slot;
	cmd_hdr_base = hisi_hba->cmd_hdr[dlvry_queue];
	slot->cmd_hdr = &cmd_hdr_base[dlvry_queue_slot];
	slot->task = task;
	slot->port = context->port;
	task->lldd_task = slot;
#ifdef SLOT_ABORT
	INIT_WORK(&slot->abort_slot, hisi_sas_slot_abort);
#endif

	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		rc = hisi_sas_task_prep_smp(hisi_hba, slot);
		break;
	case SAS_PROTOCOL_SSP:
		rc = hisi_sas_task_prep_ssp(hisi_hba, slot,
			context->is_tmf, context->tmf);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		rc = hisi_sas_task_prep_ata(hisi_hba, slot);
		break;
	default:
		dev_err_ratelimited(dev, "task prep: unknown/unsupported proto (0x%x)\n",
			task->task_proto);
		rc = -EINVAL;
		break;
	}

	if (rc) {
		spin_unlock_irqrestore(&dq->lock, flags);
		dev_err_ratelimited(dev, "task prep: rc = 0x%x\n", rc);
		goto err_out;
	}

	spin_lock(&task->task_state_lock);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock(&task->task_state_lock);

	hisi_hba->slot_prep[queue_id] = slot;

	/* flush dq slot info to memory */
	wmb();

	hisi_hba->hw->start_delivery(hisi_hba, queue_id);
	sas_dev->running_req++;

	spin_unlock_irqrestore(&dq->lock, flags);

	return 0;
err_out:
	return rc;
}

static int hisi_sas_task_prep(struct sas_task *task, struct hisi_hba *hisi_hba,
			      int is_tmf, struct hisi_sas_tmf_task *tmf)
{
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot;
	struct device *dev = &hisi_hba->pdev->dev;
	int n_elem = 0, rc, slot_idx;
	struct hisi_sas_io_context context;
	unsigned long flags;

	if (unlikely(test_bit(HISI_SAS_RESET_BIT, &hisi_hba->rst_flag))) {
		dev_info_ratelimited(dev, "sas controller is resetting, hung io task=%p, is_tmf=%d.\n",
				task, is_tmf);
		return 0;
	}

	if (!device->port) {
		struct task_status_struct *ts = &task->task_status;

		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		/*
		 * libsas will use dev->port, should
		 * not call task_done for sata
		 */
		if (device->dev_type != SAS_SATA_DEV)
			task->task_done(task);
		return 0;
	}

	if (DEV_IS_GONE(sas_dev)) {
		if (sas_dev)
			dev_info_ratelimited(dev, "task prep: device %d not ready\n",
				 sas_dev->device_id);
		else
			dev_info_ratelimited(dev, "task prep: device %016llx not ready\n",
				 SAS_ADDR(device->sas_addr));

		rc = SAS_PHY_DOWN;
		return rc;
	}

	if (unlikely(dev_is_sata(device)
		&& DEV_IS_IN_EH_STATE(sas_dev) && (!tmf))) {
		struct task_status_struct *ts = &task->task_status;

		dev_info_ratelimited(dev, "sata disk(dev %p task %p) is recovering!\n",
			device, task);

		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAM_STAT_BUSY;

		return 0;
	}

	port = device->port->lldd_port;
	if (!port || !port->port_attached) {
		if (sas_protocol_ata(task->task_proto))
			dev_info_ratelimited(dev,
				 "task prep: SATA/STP port%d not attach device, device_id:%d.\n",
				 device->port->id, sas_dev->device_id);
		else
			dev_info_ratelimited(dev,
				 "task prep: SAS port%d does not attach device, device_id:%d.\n",
				 device->port->id, sas_dev->device_id);

		rc = SAS_PHY_DOWN;
		return rc;
	}

	if (!sas_protocol_ata(task->task_proto)) {
		if (task->num_scatter) {
			n_elem = dma_map_sg(dev, task->scatter,
					    task->num_scatter, task->data_dir);
			if (!n_elem) {
				rc = -ENOMEM;
				goto prep_out;
			}
		}
	} else
		n_elem = task->num_scatter;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	if (hisi_hba->hw->slot_index_alloc)
		rc = hisi_hba->hw->slot_index_alloc(hisi_hba, &slot_idx,
						    device);
	else
		rc = hisi_sas_slot_index_alloc(hisi_hba, &slot_idx);

	if (rc) {
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
		goto err_out;
	}

	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	slot = &hisi_hba->slot_info[slot_idx];
	memset(&context, 0, sizeof(struct hisi_sas_io_context));
	context.event = HISI_SLOT_EVENT_START_IO;
	context.hba = hisi_hba;
	context.iptt = slot_idx;
	context.n_elem = n_elem;
	context.port = port;
	context.is_tmf = is_tmf;
	context.tmf = tmf;
	context.dev = sas_dev;
	context.handler = hisi_sas_start_io;
	context.handler_locked = NULL;
	context.task = task;
	if (!hisi_sas_slot_fsm(slot, &context)) {
		/* if move state succ, FREE-->RUN,check result of start io */
		rc = context.rc;
		if (rc) {
			context.event = HISI_SLOT_EVENT_START_FAIL;
			context.handler = hisi_sas_free_iptt;
			context.handler_locked = hisi_sas_free_slot;
			hisi_sas_slot_fsm(slot, &context);
		}
	} else {
		dev_err_ratelimited(dev, "start io fail, task=%p\n", task);
		/* impossible, because we got a idle iptt, io must be free */
		rc = -EINVAL;
	}

	return rc;

err_out:
	dev_err_ratelimited(dev, "task prep: failed[%d]!\n", rc);
	if (!sas_protocol_ata(task->task_proto))
		if (n_elem)
			dma_unmap_sg(dev, task->scatter, n_elem,
				     task->data_dir);
prep_out:
	return rc;
}

static int hisi_sas_task_exec(struct sas_task *task, gfp_t gfp_flags,
			      int is_tmf, struct hisi_sas_tmf_task *tmf)
{
	u32 rc;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(task->dev);
	struct device *dev = &hisi_hba->pdev->dev;

	rc = hisi_sas_task_prep(task, hisi_hba, is_tmf, tmf);
	if (rc)
		dev_err_ratelimited(dev, "task exec: failed[%d]!\n", rc);

	return rc;
}

static void hisi_sas_bytes_dmaed(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha;

	if (!phy->phy_attached)
		return;

	sas_ha = &hisi_hba->sha;
	sas_ha->notify_phy_event(sas_phy, PHYE_OOB_DONE);

	if (sas_phy->phy) {
		struct sas_phy *sphy = sas_phy->phy;

		sphy->negotiated_linkrate = sas_phy->linkrate;
		sphy->minimum_linkrate = phy->minimum_linkrate;
		sphy->maximum_linkrate = phy->maximum_linkrate;
	}

	if (phy->phy_type & PORT_TYPE_SAS) {
		struct sas_identify_frame *id;

		id = (struct sas_identify_frame *)phy->frame_rcvd;
		id->dev_type = phy->identify.device_type;
		id->initiator_bits = SAS_PROTOCOL_ALL;
		id->target_bits = phy->identify.target_port_protocols;
	} else if (phy->phy_type & PORT_TYPE_SATA) {
		/*Nothing*/
	}

	sas_phy->frame_rcvd_size = phy->frame_rcvd_size;
	sas_ha->notify_port_event(sas_phy, PORTE_BYTES_DMAED);
}

static struct hisi_sas_device *hisi_sas_alloc_dev(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct hisi_sas_device *sas_dev = NULL;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
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

	return sas_dev;
}

#define HISI_SAS_INIT_DISK_RETRY_COUNT (3)
#define HISI_SAS_INIT_RETRY_COUNT (20)
static int hisi_sas_init_disk(struct domain_device *device)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct scsi_lun lun;
	struct hisi_sas_tmf_task tmf_task;
	int retry = HISI_SAS_INIT_DISK_RETRY_COUNT;
	struct hisi_hba *hisi_hba;
	int rst_retry = HISI_SAS_INIT_RETRY_COUNT;

	hisi_hba = dev_to_hisi_hba(device);

retry_clear_cmds:
	switch (device->dev_type) {
	case SAS_END_DEVICE:
		int_to_scsilun(0, &lun);

		/* sas disk:clear task set */
		tmf_task.tmf = TMF_CLEAR_TASK_SET;
		while (retry > 0) {
			rc = hisi_sas_debug_issue_ssp_tmf(device, lun.scsi_lun,
				&tmf_task);
			if (!rc)
				break;
			retry--;
		}
		break;
	case SAS_SATA_DEV:
	case SAS_SATA_PM:
	case SAS_SATA_PM_PORT:
	case SAS_SATA_PENDING:
		while (retry > 0) {
			rc = hisi_sas_softreset_ata_disk(device);
			if (!rc)
				break;
			retry--;
		}
		break;

	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
	default:
		rc = TMF_RESP_FUNC_COMPLETE;
		break;
	}

	if (rc != TMF_RESP_FUNC_COMPLETE)
		if (test_bit(HISI_SAS_RESET_BIT, &hisi_hba->rst_flag)) {
			if (--rst_retry <= 0)
				return rc;

			while (test_bit(HISI_SAS_RESET_BIT,
					&hisi_hba->rst_flag))
				msleep(1000);

			retry = HISI_SAS_INIT_DISK_RETRY_COUNT;
			goto retry_clear_cmds;
		}

	return rc;
}

static int hisi_sas_dev_found(struct domain_device *device)
{
	struct hisi_hba *hisi_hba;
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_device *sas_dev;
	struct device *dev;
	struct hisi_sas_port *hisi_port = device->port->lldd_port;
	unsigned long flags;

	hisi_hba = dev_to_hisi_hba(device);
	if (!hisi_hba || !hisi_port)
		return -EINVAL;

	dev = &hisi_hba->pdev->dev;

	if (hisi_hba->hw->alloc_dev)
		sas_dev = hisi_hba->hw->alloc_dev(device);
	else
		sas_dev = hisi_sas_alloc_dev(device);
	if (!sas_dev) {
		dev_err(dev, "fail alloc dev: max support %d devices\n",
			HISI_SAS_MAX_DEVICES);
		return -EINVAL;
	}

	spin_lock_irqsave(&hisi_hba->lock, flags);
	device->lldd_dev = sas_dev;
	sas_dev->port_id = hisi_port->id;
	hisi_hba->hw->setup_itct(hisi_hba, sas_dev);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type)) {
		int phy_no;
		u8 phy_num = parent_dev->ex_dev.num_phys;
		struct ex_phy *phy;

		for (phy_no = 0; phy_no < phy_num; phy_no++) {
			phy = &parent_dev->ex_dev.ex_phy[phy_no];
			if (SAS_ADDR(phy->attached_sas_addr) ==
				SAS_ADDR(device->sas_addr)) {
				sas_dev->attached_phy = phy_no;
				break;
			}
		}

		if (phy_no == phy_num) {
			dev_info(dev, "dev found: no attached "
				 "dev:%016llx at ex:%016llx\n",
				 SAS_ADDR(device->sas_addr),
				 SAS_ADDR(parent_dev->sas_addr));
			return -EINVAL;
		}
	}

	dev_info(dev, "device=%p, sas_address=%llx\n", device,
		SAS_ADDR(device->sas_addr));

	hisi_sas_init_disk(device);
	return 0;
}

static int hisi_sas_slave_configure(struct scsi_device *sdev)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);
	int ret = sas_slave_configure(sdev);

	if (ret)
		return ret;
	if (!dev_is_sata(dev))
		sas_change_queue_depth(sdev, 64);

	return 0;
}

static void hisi_sas_scan_start(struct Scsi_Host *shost)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);
	int i;

	for (i = 0; i < hisi_hba->n_phy; ++i)
		hisi_sas_bytes_dmaed(hisi_hba, i);

	hisi_hba->scan_finished = 1;
}

static int hisi_sas_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);
	struct sas_ha_struct *sha = &hisi_hba->sha;

	if (hisi_hba->scan_finished == 0)
		return 0;

	sas_drain_work(sha);
	return 1;
}

static void hisi_sas_phyup_work(struct work_struct *work)
{
	struct hisi_sas_phy *phy =
		container_of(work, struct hisi_sas_phy, phyup_ws);
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	int phy_no = sas_phy->id;

	hisi_sas_refresh_dev_info(sas_phy);

	/* This requires a sleep */
	hisi_hba->hw->sl_notify(hisi_hba, phy_no);

	hisi_sas_bytes_dmaed(hisi_hba, phy_no);
}

static void hisi_sas_phy_init(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	phy->need_reject_remote_stp_link = true;
	phy->hisi_hba = hisi_hba;
	phy->port = NULL;
	phy->linkrate = SAS_LINK_RATE_UNKNOWN;
	phy->minimum_linkrate = SAS_LINK_RATE_1_5_GBPS;
	phy->maximum_linkrate = SAS_LINK_RATE_12_0_GBPS;
	init_timer(&phy->timer);
	sas_phy->enabled = (phy_no < hisi_hba->n_phy) ? 1 : 0;
	sas_phy->class = SAS;
	sas_phy->iproto = SAS_PROTOCOL_ALL;
	sas_phy->tproto = 0;
	sas_phy->type = PHY_TYPE_PHYSICAL;
	sas_phy->role = PHY_ROLE_INITIATOR;
	sas_phy->oob_mode = OOB_NOT_CONNECTED;
	sas_phy->linkrate = SAS_LINK_RATE_UNKNOWN;
	sas_phy->id = phy_no;
	sas_phy->sas_addr = &hisi_hba->sas_addr[0];
	sas_phy->frame_rcvd = &phy->frame_rcvd[0];
	sas_phy->ha = (struct sas_ha_struct *)hisi_hba->shost->hostdata;
	sas_phy->lldd_phy = phy;

	INIT_WORK(&phy->phyup_ws, hisi_sas_phyup_work);
}

static void hisi_sas_refresh_devices_on_port(
	struct hisi_hba *hisi_hba,	struct asd_sas_port *sas_port,
	enum sas_linkrate new_rate, int rate_changed)
{
	struct hisi_sas_device	*sas_dev;
	struct domain_device *device;
	int i;
	struct hisi_sas_port *port;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		sas_dev = &hisi_hba->devices[i];
		device = sas_dev->sas_device;
		if ((sas_dev->dev_type != SAS_PHY_UNUSED)
				&& device && (device->port == sas_port)) {
			hisi_hba->hw->free_device(hisi_hba, sas_dev, 0);
			port = sas_port->lldd_port;
			sas_dev->port_id = port->id;

			/* device directly attached to port,update linkrate */
			if (!device->parent && rate_changed)
				device->linkrate = new_rate;

			hisi_hba->hw->setup_itct(hisi_hba, sas_dev);
		}
	}
}

static void hisi_sas_refresh_dev_info(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	struct hisi_sas_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct hisi_sas_port *port = NULL;
	struct device *dev = &hisi_hba->pdev->dev;
	unsigned long flags;
	u8 old_port_id = HISI_SAS_INVALID_PORT_ID;
	enum sas_linkrate old_linkrate = SAS_LINK_RATE_UNKNOWN;
	int rate_changed = 0;
	int refresh = 0;

	/* phy up after phy was down or port was deformed */
	if (!sas_port)
		return;

	port = sas_port->lldd_port;
	spin_lock_irqsave(&hisi_hba->lock, flags);

	/* port was deformed before we refresh device info */
	if (!(port->phy_bitmap & BIT(sas_phy->id)))
		goto out;

	old_port_id = port->id;
	old_linkrate = phy->linkrate;
	port->id = phy->port_id;
	phy->linkrate = sas_phy->linkrate;

	/* rate changed */
	if ((old_linkrate != SAS_LINK_RATE_UNKNOWN)
			&& (old_linkrate != sas_phy->linkrate)) {
		rate_changed = 1;
		refresh = 1;
	}

	/* hardware port id changed */
	if ((old_port_id != HISI_SAS_INVALID_PORT_ID)
			&& (old_port_id != phy->port_id))
		refresh = 1;

	if (refresh)
		hisi_sas_refresh_devices_on_port(hisi_hba, sas_port,
			sas_phy->linkrate, rate_changed);
out:
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	if (refresh)
		dev_info(dev, "refresh:phy(%d) linkrate(%d) port(%d) hardware port(%d)\n",
			sas_phy->id, sas_phy->linkrate,
			sas_port->id, (u8)phy->port_id);
}

static void hisi_sas_port_notify_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	struct hisi_sas_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct hisi_sas_port *port = NULL;
	struct device *dev = &hisi_hba->pdev->dev;
	unsigned long flags;

	if (!sas_port)
		return;

	port = sas_port->lldd_port;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	port->port_attached = 1;
	port->id = phy->port_id;
	port->phy_bitmap |= BIT(sas_phy->id);
	phy->port = port;
	phy->linkrate = sas_phy->linkrate;
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	dev_info(dev, "port formed:phy(%d) linkrate(%d) port(%d) hardware port(%d)\n",
		sas_phy->id, sas_phy->linkrate, sas_port->id, (u8)phy->port_id);
}

static int hisi_sas_start_abort_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	int rc = 0;
	struct hisi_hba *hisi_hba = context->hba;
	struct hisi_sas_cmd_hdr	*cmd_hdr_base;
	struct sas_task *task = context->task;
	int dlvry_queue_slot, dlvry_queue;
	struct hisi_sas_device *sas_dev = context->dev;
	int queue_id = 0;
	int abort_flag = context->abort_flag;
	unsigned long flags;
	int task_tag = context->abort_tag;
	struct hisi_sas_dq *dq;

	queue_id = hisi_sas_get_dq_id(hisi_hba, sas_dev);
	dq = &hisi_hba->dq[queue_id];
	spin_lock_irqsave(&dq->lock, flags);
	rc = hisi_hba->hw->get_free_slot(hisi_hba, sas_dev->device_id,
					&dlvry_queue, &dlvry_queue_slot);
	if (rc) {
		spin_unlock_irqrestore(&dq->lock, flags);
		goto err_out;
	}

	if (abort_flag)
		slot->cmd_type = HISI_CMD_TYPE_ABORT_DEV;
	else
		slot->cmd_type = HISI_CMD_TYPE_ABORT_SINGLE;

	slot->device = sas_dev;
	slot->dlvry_queue = dlvry_queue;
	slot->dlvry_queue_slot = dlvry_queue_slot;
	cmd_hdr_base = hisi_hba->cmd_hdr[dlvry_queue];
	slot->cmd_hdr = &cmd_hdr_base[dlvry_queue_slot];
	slot->task = task;
	task->lldd_task = slot;
	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));

	rc = hisi_sas_task_prep_abort(hisi_hba, slot, sas_dev->device_id,
				abort_flag, task_tag);
	if (rc) {
		spin_unlock_irqrestore(&dq->lock, flags);
		goto err_out;
	}

	spin_lock(&task->task_state_lock);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock(&task->task_state_lock);

	hisi_hba->slot_prep[queue_id] = slot;

	sas_dev->running_req++;

	/* flush dq slot info to memory */
	wmb();

	hisi_hba->hw->start_delivery(hisi_hba, queue_id);
	spin_unlock_irqrestore(&dq->lock, flags);

	return 0;
err_out:
	return rc;
}

static int
hisi_sas_internal_abort_task_exec(struct hisi_hba *hisi_hba,
				  struct sas_task *task, int abort_flag,
				  int task_tag)
{
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct device *dev = &hisi_hba->pdev->dev;
	struct hisi_sas_slot *slot;
	int rc, slot_idx;
	struct hisi_sas_io_context context;
	unsigned long flags;

	if (unlikely(test_bit(HISI_SAS_RESET_BIT, &hisi_hba->rst_flag))) {
		dev_info(dev, "sas controller is resetting, reject abort io task=%p.\n",
			task);
		return -EINVAL;
	}

	if (!device->port) {
		struct task_status_struct *ts = &task->task_status;

		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;

		if (device->dev_type != SAS_SATA_DEV)
			task->task_done(task);
		return 0;
	}


	/* simply get a slot and send abort command */
	spin_lock_irqsave(&hisi_hba->lock, flags);
	if (hisi_hba->hw->slot_index_alloc)
		rc = hisi_hba->hw->slot_index_alloc(hisi_hba, &slot_idx,
						device);
	else
		rc = hisi_sas_slot_index_alloc(hisi_hba, &slot_idx);

	if (rc) {
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
		goto err_out;
	}
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	slot = &hisi_hba->slot_info[slot_idx];

	memset(&context, 0, sizeof(struct hisi_sas_io_context));
	context.event = HISI_SLOT_EVENT_START_IO;
	context.task = task;
	context.abort_flag = abort_flag;
	context.abort_tag = task_tag;
	context.hba = hisi_hba;
	context.iptt = slot_idx;
	context.dev = sas_dev;
	context.handler = hisi_sas_start_abort_io;
	context.handler_locked = NULL;
	if (!hisi_sas_slot_fsm(slot, &context)) {
		/* if move state succ, FREE-->RUN,check result of start io */
		rc = context.rc;
		if (rc) {
			context.event = HISI_SLOT_EVENT_START_FAIL;
			context.handler = hisi_sas_free_iptt;
			context.handler_locked = hisi_sas_free_slot;
			hisi_sas_slot_fsm(slot, &context);
		}
	} else {
		dev_err(dev, "start abort fail, task=%p\n", task);
		/* impossible, because we got a idle iptt, io must be free */
		rc = -EINVAL;
	}

err_out:

	return rc;
}

static void hisi_sas_task_done(struct sas_task *task);
static void hisi_sas_tmf_timedout(unsigned long data);

static int
hisi_sas_internal_task_abort(struct hisi_hba *hisi_hba,
			struct domain_device *device,
			int abort_flag, int tag)
{
	struct sas_task *task = NULL;
	struct device *dev = &hisi_hba->pdev->dev;
	int res;

	if (!hisi_hba->hw->prep_abort)
		return -EOPNOTSUPP;

	task = sas_alloc_slow_task(GFP_KERNEL);
	if (!task)
		return -ENOMEM;

	task->dev = device;
	task->task_proto = device->tproto;
	task->task_done = hisi_sas_task_done;
	task->slow_task->timer.data = (unsigned long)task;
	task->slow_task->timer.function = hisi_sas_tmf_timedout;
	task->slow_task->timer.expires = jiffies + 20*HZ;
	add_timer(&task->slow_task->timer);

	res = hisi_sas_internal_abort_task_exec(hisi_hba,
						task, abort_flag, tag);
	if (res) {
		del_timer(&task->slow_task->timer);
		dev_err(dev, "internal task abort: executing internal task failed: %d\n",
			res);
		goto exit;
	}
	wait_for_completion(&task->slow_task->completion);
	res = TMF_RESP_FUNC_FAILED;
	/* TMF timed out, return direct. */
	if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
		if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
			struct hisi_sas_slot *slot = task->lldd_task;

			/* fix sata abort timeout chip bug */
			if (hisi_hba->hw->send_break)
				hisi_hba->hw->send_break(hisi_hba, device);
			task->slow_task->timer.expires = jiffies + 1*HZ;
			add_timer(&task->slow_task->timer);
			wait_for_completion(&task->slow_task->completion);

			if (task->task_state_flags & SAS_TASK_STATE_DONE)
				res = TMF_RESP_FUNC_COMPLETE;
			else {
				if (slot) {
					dev_err(dev, "internal task abort timeout, iptt:%d, device_id:%d.\n",
					  slot->idx, slot->device->device_id);

					hisi_sas_slot_task_free(hisi_hba, task,
								slot);
				} else
					dev_err(dev, "internal task abort: timeout.\n");
			}
			goto exit;
		}
	}

	if ((task->task_status.resp == SAS_TASK_COMPLETE) &&
		(task->task_status.stat == TMF_RESP_FUNC_COMPLETE)) {
		res = TMF_RESP_FUNC_COMPLETE;
	} else
		dev_info(dev, "internal task abort fail: dev %016llx iptt:%d resp: 0x%x sts 0x%x\n",
			SAS_ADDR(device->sas_addr),
			((struct hisi_sas_slot *)task->lldd_task)->idx,
			task->task_status.resp,
			task->task_status.stat);

exit:
	sas_free_task(task);
	task = NULL;

	return res;
}


static void hisi_sas_dev_gone(struct domain_device *device)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = &hisi_hba->pdev->dev;
	u32 dev_id = sas_dev->device_id;
	unsigned long flags;

	dev_info(dev, "found dev[%d:%x] is gone\n",
		 sas_dev->device_id, sas_dev->dev_type);

	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_hba->hw->free_device(hisi_hba, sas_dev, 1);
	device->lldd_dev = NULL;
	memset(sas_dev, 0, sizeof(*sas_dev));
	sas_dev->device_id = dev_id;
	sas_dev->port_id = HISI_SAS_INVALID_PORT_ID;
	sas_dev->dev_type = SAS_PHY_UNUSED;
	sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
}

static int hisi_sas_queue_command(struct sas_task *task, gfp_t gfp_flags)
{
	return hisi_sas_task_exec(task, gfp_flags, 0, NULL);
}

static int hisi_sas_control_phy(struct asd_sas_phy *sas_phy, enum phy_func func,
				void *funcdata)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	int phy_no = sas_phy->id;

	switch (func) {
	case PHY_FUNC_HARD_RESET:
		hisi_hba->hw->phy_hard_reset(hisi_hba, phy_no);
		break;

	case PHY_FUNC_LINK_RESET:
		hisi_hba->hw->phy_disable(hisi_hba, phy_no);
		msleep(100);
		hisi_hba->hw->phy_enable(hisi_hba, phy_no);
		break;

	case PHY_FUNC_DISABLE:
		hisi_hba->hw->phy_disable(hisi_hba, phy_no);
		break;

	case PHY_FUNC_GET_EVENTS:
		break;

	case PHY_FUNC_SET_LINK_RATE:
		if (hisi_hba->hw->phy_set_link_rate)
			hisi_hba->hw->phy_set_link_rate(hisi_hba,
				phy_no, funcdata);
		break;

	case PHY_FUNC_RELEASE_SPINUP_HOLD:
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static void hisi_sas_task_done(struct sas_task *task)
{
	if (!del_timer(&task->slow_task->timer))
		return;
	complete(&task->slow_task->completion);
}

static void hisi_sas_tmf_timedout(unsigned long data)
{
	struct sas_task *task = (struct sas_task *)data;

	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	complete(&task->slow_task->completion);
}

static inline void hisi_sas_fill_ata_reset_cmd(u32 reset, u8 *fis)
{
	/* Register - Host to Device FIS */
	fis[0] = 0x27;

	/* set C bit to zero, set PMP port to 0 */
	fis[1] = 0x0;

	/* soft-reset or de-reset */
	fis[2] = ATA_CMD_DEV_RESET;
	fis[3] = 0;

	fis[4] = 0;
	fis[5] = 0;
	fis[6] = 0;
	fis[7] = 0;
	fis[8] = 0;
	fis[9] = 0;
	fis[10] = 0;
	fis[11] = 0;

	fis[12] = 0;
	fis[13] = 0;
	fis[14] = 0;

	if (reset)
		/* control:reset */
		fis[15] = ATA_SRST;
	else
		/* control:de-reset */
		fis[15] = 0x0;

	/* reserved */
	fis[16] = 0;
	fis[17] = 0;
	fis[18] = 0;
	fis[19] = 0;
}

#define TASK_TIMEOUT 20
#define TASK_RETRY 3
static int hisi_sas_exec_internal_tmf_task(struct domain_device *device,
					   void *parameter, u32 para_len,
					   struct hisi_sas_tmf_task *tmf)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = sas_dev->hisi_hba;
	struct device *dev = &hisi_hba->pdev->dev;
	struct sas_task *task;
	int res, retry;

	for (retry = 0; retry < TASK_RETRY; retry++) {
		task = sas_alloc_slow_task(GFP_KERNEL);
		if (!task)
			return -ENOMEM;

		task->dev = device;
		task->task_proto = device->tproto;

		if (dev_is_sata(device)) {
			/* ata disk has no tmf command, use softreset instead */
			task->ata_task.device_control_reg_update = 1;
			hisi_sas_fill_ata_reset_cmd(tmf->tmf,
				(u8 *)&task->ata_task.fis);
		} else {
			memcpy(&task->ssp_task, parameter, para_len);
		}

		task->task_done = hisi_sas_task_done;

		task->slow_task->timer.data = (unsigned long) task;
		task->slow_task->timer.function = hisi_sas_tmf_timedout;
		task->slow_task->timer.expires = jiffies + TASK_TIMEOUT*HZ;
		add_timer(&task->slow_task->timer);

		res = hisi_sas_task_exec(task, GFP_KERNEL, 1, tmf);
		if (res) {
			del_timer(&task->slow_task->timer);
			dev_err(dev, "abort tmf: executing internal task failed: %d\n",
				res);
			goto ex_err;
		}

		wait_for_completion(&task->slow_task->completion);
		res = TMF_RESP_FUNC_FAILED;
		/* Even TMF timed out, return direct. */
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				if (task->lldd_task) {
					struct hisi_sas_slot *slot =
						task->lldd_task;
					dev_err(dev, "abort tmf: TMF task[%d] timeout, abort iptt:%d, device_id:%d.\n",
						tmf->tag_of_task_to_be_managed,
						slot->idx,
						slot->device->device_id);

					hisi_sas_slot_task_free(hisi_hba,
								task, slot);
				} else
					dev_err(dev, "abort tmf: TMF task[%d] timeout\n",
						tmf->tag_of_task_to_be_managed);

				goto ex_err;
			}
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		     task->task_status.stat == TMF_RESP_FUNC_COMPLETE) {
			res = TMF_RESP_FUNC_COMPLETE;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAS_DATA_UNDERRUN) {
			dev_warn(dev, "abort tmf: task to dev %016llx "
				 "resp: 0x%x sts 0x%x underrun\n",
				 SAS_ADDR(device->sas_addr),
				 task->task_status.resp,
				 task->task_status.stat);
			res = task->task_status.residual;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
			task->task_status.stat == SAS_DATA_OVERRUN) {
			dev_warn(dev, "abort tmf: blocked task error\n");
			res = -EMSGSIZE;
			break;
		}

		dev_warn(dev, "abort tmf: task to dev "
			 "%016llx resp: 0x%x status 0x%x\n",
			 SAS_ADDR(device->sas_addr), task->task_status.resp,
			 task->task_status.stat);
		sas_free_task(task);
		task = NULL;
	}
ex_err:
	sas_free_task(task);
	task = NULL;
	return res;
}

static int hisi_sas_debug_issue_ssp_tmf(struct domain_device *device,
					u8 *lun, struct hisi_sas_tmf_task *tmf)
{
	struct sas_ssp_task ssp_task;

	if (!(device->tproto & SAS_PROTOCOL_SSP))
		return TMF_RESP_FUNC_ESUPP;

	memcpy(ssp_task.LUN, lun, 8);

	return hisi_sas_exec_internal_tmf_task(device, &ssp_task,
					       sizeof(ssp_task), tmf);
}

static int hisi_sas_softreset_ata_disk(struct domain_device *device)
{
	int rc;
	struct hisi_sas_tmf_task tmf;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = &hisi_hba->pdev->dev;

	/* reset sata disk */
	tmf.tmf = 1;
	rc = hisi_sas_exec_internal_tmf_task(device, NULL, 0, &tmf);
	if (rc == TMF_RESP_FUNC_COMPLETE) {
		/* de-reset sata disk */
		tmf.tmf = 0;
		rc = hisi_sas_exec_internal_tmf_task(device, NULL, 0, &tmf);
		if (rc != TMF_RESP_FUNC_COMPLETE) {
			dev_info(dev, "%s, ata disk %016llx de-reset failed, device=%p rc=%d\n",
				__func__, SAS_ADDR(device->sas_addr),
				device, rc);
		}
	} else {
		dev_info(dev, "%s, ata disk %016llx reset failed, device=%p rc=%d\n",
			__func__, SAS_ADDR(device->sas_addr), device, rc);
	}

	return rc;
}

static void hisi_sas_handle_dev_evt(struct hisi_hba *hisi_hba,
			struct domain_device *device,
			void *handler,
			void *handler_locked,
			enum hisi_slot_event evt)
{
	struct hisi_sas_slot *slot;
	struct sas_task *task;
	int i;
	unsigned long flags;
	struct hisi_sas_io_context context;
	int max_command_entries = hisi_hba->hw->max_command_entries;

	memset(&context, 0, sizeof(struct hisi_sas_io_context));
	context.event = evt;
	context.hba = hisi_hba;
	context.handler = handler;
	context.handler_locked = handler_locked;

	for (i = 0; i < max_command_entries; i++) {
		slot = &hisi_hba->slot_info[i];

		spin_lock_irqsave(&slot->lock, flags);
		task = slot->task;

		/* only care about io from upper layer */
		if (!task || (task->dev != device)
			|| (slot->cmd_type != HISI_CMD_TYPE_NORMAL)) {
			spin_unlock_irqrestore(&slot->lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&slot->lock, flags);

		context.task = task;
		context.iptt = i;
		hisi_sas_slot_fsm(slot, &context);
	}
}

static void hisi_sas_abort_ata_device(struct hisi_hba *hisi_hba,
				struct domain_device *device)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct device *dev = &hisi_hba->pdev->dev;

	hisi_sas_handle_dev_evt(hisi_hba, device, NULL, NULL,
			HISI_SLOT_EVENT_EX_ABT);

	/* try to abort tasks in ata disk */
	rc = hisi_sas_internal_task_abort(hisi_hba, device, 1, 0);
	if (rc == TMF_RESP_FUNC_COMPLETE) {
		if (!test_bit(SAS_DEV_GONE, &device->state))
			rc = hisi_sas_softreset_ata_disk(device);
		else
			rc = TMF_RESP_FUNC_FAILED;
	} else {
		goto sas_reset;
	}

	if (rc == TMF_RESP_FUNC_COMPLETE)
		goto out;

	/* try to do I_T_nexus_reset for ata disk */
	rc = hisi_sas_debug_I_T_nexus_reset(device);
	if (rc == TMF_RESP_FUNC_COMPLETE)
		goto out;

	dev_info(dev, "%s, hisi_sas_I_T_nexus_reset failed! rc=%d\n",
		__func__, rc);

sas_reset:
	/* try to do sas controller reset to recover */
	hisi_sas_cnh_eh_all_slots(hisi_hba);
	hisi_sas_chip_reset(hisi_hba);
	hisi_sas_cnh_release_all_slots(hisi_hba);
	return;

out:
	hisi_sas_handle_dev_evt(hisi_hba, device,
		hisi_sas_free_iptt, hisi_sas_free_slot,
		HISI_SLOT_EVENT_EX_EH_OK);
}

static int hisi_sas_abort_task(struct sas_task *task)
{
	struct scsi_lun lun;
	struct hisi_sas_tmf_task tmf_task;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(task->dev);
	struct device *dev = &hisi_hba->pdev->dev;
	int rc = TMF_RESP_FUNC_FAILED;
	int rc1 = TMF_RESP_FUNC_FAILED;
	int rc2 = TMF_RESP_FUNC_FAILED;
	unsigned long flags;
	struct hisi_sas_slot *slot = task->lldd_task;
	struct hisi_sas_io_context context;

	if (!sas_dev) {
		dev_warn(dev, "Device has been removed\n");
		return TMF_RESP_FUNC_FAILED;
	}

	if (!slot) {
		dev_warn(dev, "IO has been released\n");
		return TMF_RESP_FUNC_COMPLETE;
	}

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		rc = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}

	spin_unlock_irqrestore(&task->task_state_lock, flags);
	sas_dev->dev_status = HISI_SAS_DEV_EH;

	/* external abort task */
	memset(&context, 0, sizeof(struct hisi_sas_io_context));
	context.event = HISI_SLOT_EVENT_EX_ABT;
	if (hisi_sas_slot_fsm(slot, &context)) {
		rc = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}

	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		u32 tag = slot->idx;

		int_to_scsilun(cmnd->device->lun, &lun);
		tmf_task.tmf = TMF_ABORT_TASK;
		tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

		if (!test_bit(SAS_DEV_GONE, &device->state)) {
			rc1 = hisi_sas_debug_issue_ssp_tmf(task->dev,
				lun.scsi_lun, &tmf_task);
		}

		/* if successful, clear the task and callback forwards.*/
		rc2 = hisi_sas_internal_task_abort(hisi_hba, device, 0, tag);
		if (rc2 == TMF_RESP_FUNC_COMPLETE
				&& rc1 == TMF_RESP_FUNC_COMPLETE) {
			/* external abort task success */
			memset(&context, 0, sizeof(struct hisi_sas_io_context));
			context.event = HISI_SLOT_EVENT_EX_EH_OK;
			context.hba = hisi_hba;
			context.task = task;
			context.iptt = tag;
			context.handler = hisi_sas_free_iptt;
			context.handler_locked = hisi_sas_free_slot;
			hisi_sas_slot_fsm(slot, &context);
			rc = TMF_RESP_FUNC_COMPLETE;
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
		}

	} else if (task->task_proto & SAS_PROTOCOL_SATA ||
		task->task_proto & SAS_PROTOCOL_STP) {

		hisi_sas_abort_ata_device(hisi_hba, device);
		sas_dev->dev_status = HISI_SAS_DEV_NORMAL;

		/* for sata and stp command, abort task always return success */
		rc = TMF_RESP_FUNC_COMPLETE;
	} else if (task->task_proto & SAS_PROTOCOL_SMP) {
		/* SMP */
		struct hisi_sas_slot *slot = task->lldd_task;
		u32 tag = slot->idx;

		rc = hisi_sas_internal_task_abort(hisi_hba, device, 0, tag);

		/*
		 * no matter abort succ or fail, libsas will
		 * free sas_task of smp command, so we shall release
		 * slot before return.
		 */
		memset(&context, 0, sizeof(struct hisi_sas_io_context));
		context.event = HISI_SLOT_EVENT_EX_EH_OK;
		context.hba = hisi_hba;
		context.task = task;
		context.iptt = tag;
		context.handler = hisi_sas_free_iptt;
		context.handler_locked = hisi_sas_free_slot;
		hisi_sas_slot_fsm(slot, &context);
		sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
	}

out:
	if (rc != TMF_RESP_FUNC_COMPLETE)
		dev_notice(dev, "abort task: task=%p, rc=%d\n", task, rc);
	return rc;
}

static int hisi_sas_abort_task_set(struct domain_device *device, u8 *lun)
{
	struct hisi_sas_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = &hisi_hba->pdev->dev;

	if (dev_is_sata(device))
		goto err_out;

	hisi_sas_handle_dev_evt(hisi_hba, device, NULL, NULL,
				HISI_SLOT_EVENT_EX_ABTS);

	tmf_task.tmf = TMF_ABORT_TASK_SET;
	if (!test_bit(SAS_DEV_GONE, &device->state))
		rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	if (rc == TMF_RESP_FUNC_COMPLETE) {
		rc = hisi_sas_internal_task_abort(hisi_hba, device, 1, 0);
		if (rc == TMF_RESP_FUNC_COMPLETE) {

			hisi_sas_handle_dev_evt(hisi_hba, device,
				hisi_sas_free_iptt, hisi_sas_free_slot,
				HISI_SLOT_EVENT_EX_EH_OK);
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
		}
	}

err_out:
	if (rc != TMF_RESP_FUNC_COMPLETE)
		dev_notice(dev, "abort task set:dev=%p, sas_address=%llx rc=%d\n",
			device, SAS_ADDR(device->sas_addr), rc);

	return rc;
}

static int hisi_sas_clear_aca(struct domain_device *device, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct hisi_sas_tmf_task tmf_task;

	tmf_task.tmf = TMF_CLEAR_ACA;
	if (!test_bit(SAS_DEV_GONE, &device->state))
		rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	return rc;
}

static void hisi_sas_save_old_state(struct hisi_hba *hisi_hba,
		u8 *state)
{
	struct hisi_sas_phy *phy = NULL;
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		phy = &hisi_hba->phy[i];
		phy->is_flutter = 1;

		state[i] = (u8)hisi_hba->hw->get_phy_state(hisi_hba, i);
	}
}

static void hisi_sas_notify_new_state(struct hisi_hba *hisi_hba,
		u8 *old_state)
{
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	struct hisi_sas_phy *phy = NULL;
	struct asd_sas_port  *sas_port = NULL;
	struct asd_sas_phy  *sas_phy = NULL;
	int i;
	struct domain_device *device = NULL;
	struct device *dev = &hisi_hba->pdev->dev;

	/* scan all phys of the host */
	for (i = 0; i < hisi_hba->n_phy; i++) {
		phy = &hisi_hba->phy[i];
		phy->is_flutter = 0;
		sas_phy = &phy->sas_phy;

		/* get the new state of the phy */
		phy->phy_attached =
			(u8)hisi_hba->hw->get_phy_state(hisi_hba, i);

		/* phy old state is down, new state
		 * is down, check the next phy
		 */
		if (!old_state[i] && old_state[i] == phy->phy_attached)
			continue;

		/* check if the phy was attached a device before */
		if (old_state[i]) {
			sas_port = sas_phy->port;
			if (!sas_port)
				continue;
			device = sas_port->port_dev;
			if (!device)
				continue;

			/* the port(phy) attached an expander */
			if (DEV_IS_EXPANDER(device->dev_type)) {
				/* the new state of the phy is down */
				if (!phy->phy_attached) {
					dev_info(dev, "%s, old up state sas phy %d loss of signal!\n",
						__func__, sas_phy->id);
					/* phy new state is changed
					 * to down, NOTIFY libsas
					 */
					hisi_sas_phy_down(hisi_hba,
						sas_phy->id, 0);
				} else {
					/* although phy state is not change,
					 * but user maybe plug or unplug disks
					 * from expander when sas controller
					 * reset, so send BCAST event
					 */
					dev_info(dev, "%s, phy state not change, sas phy %d notify bcast!\n",
						__func__, sas_phy->id);
					sas_ha->notify_port_event(
						&phy->sas_phy,
						PORTE_BROADCAST_RCVD);
				}
			} else {
				/* old state is up, new state is up
				 * too, check the next phy
				 */
				if (old_state[i] == phy->phy_attached)
					continue;

				dev_info(dev, "%s, old up state sas phy %d loss of signal!\n",
					__func__, sas_phy->id);
				/* if phy new state is changed to
				 * down, NOTIFY libsas
				 */
				hisi_sas_phy_down(hisi_hba, sas_phy->id, 0);
			}
		} else {
			/* the phy state is down before, after sas
			 * controller reset the phy is up, so notify
			 * libsas new device attached.
			 */
			dev_info(dev, "%s, new sas phy %d up!\n", __func__,
				sas_phy->id);
			hisi_sas_bytes_dmaed(hisi_hba, sas_phy->id);
		}
	}
}

static int hisi_sas_debug_I_T_nexus_reset(struct domain_device *device)
{
	struct sas_phy *local_phy = sas_get_local_phy(device);
	struct hisi_sas_port *port = NULL;
	struct asd_sas_phy *sas_phy = NULL;
	struct hisi_sas_phy *phy = NULL;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	struct device *dev = &hisi_hba->pdev->dev;
	u8 phy_old_state = 0;
	int rc;
	unsigned long port_phys_bitmap = 0UL;
	int i;

	/* get the host phy state before reset
	 * when port directly attached the device
	 */
	if (scsi_is_sas_phy_local(local_phy)) {
		sas_phy = sas_ha->sas_phy[local_phy->number];
		phy = container_of(sas_phy, typeof(*phy), sas_phy);
		phy_old_state =
			(u8)hisi_hba->hw->get_phy_state(hisi_hba, sas_phy->id);
		phy->is_flutter = 1;
	} else {
		/*
		 * check the host port state, if port is down then
		 * sas wire was unplugged before here, just return FAIL.
		 */
		port = device->port->lldd_port;
		if (!port)
			return TMF_RESP_FUNC_FAILED;
		port_phys_bitmap = port->phy_bitmap;

		/* ignore events of the host port that attach the device */
		for (i = 0; i < HISI_SAS_MAX_PHYS; i++) {
			if (port_phys_bitmap & BIT(i)) {
				phy = &hisi_hba->phy[i];
				phy->is_flutter = 1;
			}
		}
	}
	rc = sas_phy_reset(local_phy, 1);
	sas_put_local_phy(local_phy);
	msleep(2000);

	/* get the host phy state after reset
	 * when port directly attached the device
	 */
	if (scsi_is_sas_phy_local(local_phy)) {
		phy->is_flutter = 0;
		phy->phy_attached =
			(u8)hisi_hba->hw->get_phy_state(hisi_hba, sas_phy->id);
		/* phy state is not change when execute I_T_NEXUS_reset */
		if (phy_old_state == phy->phy_attached)
			goto out;

		/* if phy new state is changed to up, NOTIFY libsas */
		if (phy->phy_attached) {
			dev_info(dev, "%s, new sas phy %d up!\n", __func__,
				sas_phy->id);
			hisi_sas_bytes_dmaed(hisi_hba, sas_phy->id);
		} else {
			dev_info(dev, "%s, old up state sas phy %d loss of signal!\n",
				__func__, sas_phy->id);
			/* if phy new state is changed to down, NOTIFY libsas */
			sas_ha->notify_phy_event(sas_phy, PHYE_LOSS_OF_SIGNAL);
			sas_phy_disconnected(sas_phy);
		}
	} else {
		for (i = 0; i < HISI_SAS_MAX_PHYS; i++) {
			if (port_phys_bitmap & BIT(i)) {
				phy = &hisi_hba->phy[i];
				phy->is_flutter = 0;
				sas_phy = &phy->sas_phy;

				/*
				 * check the host port state, if port is
				 * down then sas wire was unplugged,
				 * send loss of signal event. otherwise
				 * send bcast event,in case user pluged in
				 * new disk, or unpluged old disk out
				 * from expander.
				 */
				if (phy->phy_attached) {
					dev_info(dev, "%s, phy %d report bcast!\n",
						__func__, sas_phy->id);
					sas_ha->notify_port_event(sas_phy,
						PORTE_BROADCAST_RCVD);
				} else {
					/* In case user unplug the sas wire
					 * when execute I_T_NEXUS_reset
					 */
					dev_info(dev, "%s, phy %d is down!\n",
						__func__, sas_phy->id);
					sas_ha->notify_phy_event(sas_phy,
						PHYE_LOSS_OF_SIGNAL);
					sas_phy_disconnected(sas_phy);
				}
			}
		}
	}

out:
	return rc;
}

static int hisi_sas_I_T_nexus_reset(struct domain_device *device)
{
	struct hisi_sas_device *sas_dev = NULL;
	struct hisi_hba *hisi_hba = NULL;
	int rc = TMF_RESP_FUNC_FAILED;
	struct device *dev = NULL;

	if (!device || !device->lldd_dev || !device->port)
		return -ENODEV;

	hisi_hba = dev_to_hisi_hba(device);
	dev = &hisi_hba->pdev->dev;

	if (dev_is_sata(device))
		if (test_bit(SAS_DEV_GONE, &device->state))
			return -ENODEV;

	sas_dev = device->lldd_dev;
	if (sas_dev->dev_status != HISI_SAS_DEV_EH)
		return TMF_RESP_FUNC_COMPLETE;

	hisi_sas_handle_dev_evt(hisi_hba, device,
			NULL, NULL, HISI_SLOT_EVENT_EX_ITNR);

	rc = hisi_sas_debug_I_T_nexus_reset(device);

	if (rc == TMF_RESP_FUNC_COMPLETE) {
		hisi_sas_handle_dev_evt(hisi_hba, device,
			hisi_sas_free_iptt, hisi_sas_free_slot,
			HISI_SLOT_EVENT_EX_EH_OK);

	    sas_dev->dev_status = HISI_SAS_DEV_NORMAL;

	}

	if (rc != TMF_RESP_FUNC_COMPLETE) {
		dev_notice(dev, "I_T_Nexus_reset:dev=%p, sas_address=%llx rc=%d\n",
			device, SAS_ADDR(device->sas_addr), rc);
		rc = TMF_RESP_FUNC_FAILED;
	}

	/* if fail, flow to clear nexus ha */
	return rc;
}

static void hisi_sas_cnh_eh_all_slots(struct hisi_hba *hisi_hba)
{
	int dev_id;
	struct hisi_sas_device *sas_dev;
	struct domain_device *device;

	for (dev_id = 0; dev_id < HISI_SAS_MAX_DEVICES; dev_id++) {

		sas_dev = &hisi_hba->devices[dev_id];
		device = sas_dev->sas_device;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED)
				|| (device == NULL))
			continue;

		sas_dev->dev_status = HISI_SAS_DEV_EH;
		hisi_sas_handle_dev_evt(hisi_hba, device,
					NULL, NULL, HISI_SLOT_EVENT_EX_CNH);
	}
}

static void hisi_sas_cnh_release_all_slots(struct hisi_hba *hisi_hba)
{
	int dev_id;
	struct hisi_sas_device *sas_dev;
	struct domain_device *device;

	for (dev_id = 0; dev_id < HISI_SAS_MAX_DEVICES; dev_id++) {

		sas_dev = &hisi_hba->devices[dev_id];
		device = sas_dev->sas_device;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED)
				|| (device == NULL))
			continue;

		hisi_sas_handle_dev_evt(hisi_hba, device,
			hisi_sas_free_iptt, hisi_sas_free_slot,
			HISI_SLOT_EVENT_EX_EH_OK);

		sas_dev->dev_status = HISI_SAS_DEV_NORMAL;

	}

}

static void hisi_sas_clr_chip_reset_flag(struct hisi_hba *hisi_hba)
{
	clear_bit(HISI_SAS_RESET_BIT, &hisi_hba->rst_flag);
}

static int hisi_sas_set_chip_reset_flag(struct hisi_hba *hisi_hba)
{
	if (!test_and_set_bit(HISI_SAS_RESET_BIT, &hisi_hba->rst_flag))
		return 0;

	return -1;
}

static int hisi_sas_exec_soft_reset(struct hisi_hba *hisi_hba)
{
	return hisi_hba->hw->soft_reset ?
		hisi_hba->hw->soft_reset(hisi_hba) : -1;
}

static int hisi_sas_chip_reset(struct hisi_hba *hisi_hba)
{
	int rc = -1;
	u8 phy_up_state[HISI_SAS_MAX_PHYS] = {0};
	struct device *dev = &hisi_hba->pdev->dev;

	rc = hisi_sas_set_chip_reset_flag(hisi_hba);
	if (rc)
		goto err_out;
	scsi_block_requests(hisi_hba->shost);

	dev_info(dev, "%s, sas controller reset start!\n",
		__func__);

	memset(phy_up_state, 0, HISI_SAS_MAX_PHYS);

	hisi_sas_save_old_state(hisi_hba, phy_up_state);

	hisi_hba->rst_start_time = jiffies;

	rc = hisi_sas_exec_soft_reset(hisi_hba);
	if (rc != 0) {
		dev_info(dev, "%s, hisi_sas_exec_soft_reset failed! rc=%d\n",
			__func__, rc);
		goto err_unblock_out;
	}

	/* wait until phy negotiate completion */
	msleep(1000);
	hisi_sas_notify_new_state(hisi_hba, phy_up_state);

err_unblock_out:
	scsi_unblock_requests(hisi_hba->shost);
	hisi_sas_clr_chip_reset_flag(hisi_hba);

	if (!rc)
		dev_info(dev, "%s, sas controller reset successfully, timecost %dms.\n",
		 __func__,
		 jiffies_to_msecs(jiffies - hisi_hba->rst_start_time));
err_out:
	return rc;
}

static int hisi_sas_clear_nexus_ha(struct sas_ha_struct *sas_ha)
{
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;

	hisi_sas_cnh_eh_all_slots(hisi_hba);

	hisi_sas_chip_reset(hisi_hba);

	hisi_sas_cnh_release_all_slots(hisi_hba);

	return 0;
}

static int hisi_sas_lu_reset(struct domain_device *device, u8 *lun)
{
	struct hisi_sas_tmf_task tmf_task;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = &hisi_hba->pdev->dev;
	int rc = TMF_RESP_FUNC_FAILED;

	if (dev_is_sata(device))
		goto err_out;

	hisi_sas_handle_dev_evt(hisi_hba, device, NULL, NULL,
				HISI_SLOT_EVENT_EX_LR);

	tmf_task.tmf = TMF_LU_RESET;
	sas_dev->dev_status = HISI_SAS_DEV_EH;
	if (!test_bit(SAS_DEV_GONE, &device->state))
		rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	if (rc == TMF_RESP_FUNC_COMPLETE) {
		rc = hisi_sas_internal_task_abort(hisi_hba, device, 1, 0);
		if (rc == TMF_RESP_FUNC_COMPLETE) {

			hisi_sas_handle_dev_evt(hisi_hba, device,
				hisi_sas_free_iptt, hisi_sas_free_slot,
				HISI_SLOT_EVENT_EX_EH_OK);
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
		}
	}

err_out:
	/* If failed, fall-through I_T_Nexus reset */
	if (rc != TMF_RESP_FUNC_COMPLETE)
		dev_notice(dev, "lu reset: dev=%p sas_address=%llx, rc=%d\n",
			device, SAS_ADDR(device->sas_addr), rc);
	return rc;
}

static int hisi_sas_query_task(struct sas_task *task)
{
	struct scsi_lun lun;
	struct hisi_sas_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;
	struct hisi_sas_io_context context;

	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		struct domain_device *device = task->dev;
		struct hisi_sas_slot *slot = task->lldd_task;
		u32 tag = slot->idx;

		int_to_scsilun(cmnd->device->lun, &lun);
		tmf_task.tmf = TMF_QUERY_TASK;
		tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

		memset(&context, 0, sizeof(struct hisi_sas_io_context));
		context.event = HISI_SLOT_EVENT_EX_QT;
		hisi_sas_slot_fsm(slot, &context);

		if (!test_bit(SAS_DEV_GONE, &device->state))
			rc = hisi_sas_debug_issue_ssp_tmf(device,
				lun.scsi_lun, &tmf_task);
		switch (rc) {
		/* The task is still in Lun, release it then */
		case TMF_RESP_FUNC_SUCC:
		/* The task is not in Lun or failed, reset the phy */
		case TMF_RESP_FUNC_FAILED:
		case TMF_RESP_FUNC_COMPLETE:
			break;
		/* for LLDD errors just return TMF_RESP_FUNC_FAILED */
		default:
			rc = TMF_RESP_FUNC_FAILED;
			break;
		}
	}
	return rc;
}

static int hisi_sas_ata_check_ready(struct domain_device *device)
{
	struct hisi_sas_port *port = NULL;

	if (!device || !device->port || !device->port->lldd_port)
		return -ENODEV;

	if (test_bit(SAS_DEV_GONE, &device->state))
		return -ENODEV;

	port = device->port->lldd_port;
	return port->port_attached;
}

static void hisi_sas_port_formed(struct asd_sas_phy *sas_phy)
{
	hisi_sas_port_notify_formed(sas_phy);
}

static void hisi_sas_fake_scsi_done(struct scsi_cmnd *cmd)
{
	/* This function do nothing, so libata can not done io to scsi */
}

int hisi_sas_report_fail_ata_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	struct sas_task *task = context->task;
	struct ata_queued_cmd *qc = task->uldd_task;
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct task_status_struct *ts = &task->task_status;

	/* fake io success to libsas and libata */
	ts->stat = SAM_STAT_GOOD;

	/* do not let libata done io to scsi */
	qc->scsidone = hisi_sas_fake_scsi_done;

	hisi_sas_complete_io(slot, context);

	/* we done fail for the sata commands from scsi */
	cmd->result = DID_BAD_TARGET << 16;
	cmd->scsi_done(cmd);

	return 0;
}

int hisi_sas_report_fail_internal_ata_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	struct sas_task *task = context->task;
	struct task_status_struct *ts = &task->task_status;

	/* libata internal ata command, done fail and complete. */
	ts->stat = SAS_OPEN_REJECT;

	hisi_sas_complete_io(slot, context);

	return 0;
}

int hisi_sas_report_fail_ssp_smp_io(struct hisi_sas_slot *slot,
		struct hisi_sas_io_context *context)
{
	struct sas_task *task = context->task;
	struct task_status_struct *ts = &task->task_status;

	/* tell upper layer that target is gone */
	ts->stat = SAS_DEVICE_UNKNOWN;
	ts->resp = SAS_TASK_UNDELIVERED;

	hisi_sas_complete_io(slot, context);

	return 0;
}

static void hisi_sas_before_dev_gone(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	int max_command_entries = hisi_hba->hw->max_command_entries;
	struct hisi_sas_slot *slot;
	struct sas_task *task;
	int i;
	unsigned long flags;
	struct hisi_sas_io_context context;
	int rc = TMF_RESP_FUNC_FAILED;

	rc = hisi_sas_internal_task_abort(hisi_hba, device, 1, 0);
	if ((rc == TMF_RESP_FUNC_COMPLETE)
			&& (!DEV_IS_EXPANDER(device->dev_type)))
		rc = hisi_sas_debug_I_T_nexus_reset(device);

	if (rc != TMF_RESP_FUNC_COMPLETE)
		hisi_sas_chip_reset(hisi_hba);

	/* release slots for commands of device */
	for (i = 0; i < max_command_entries; i++) {
		slot = &hisi_hba->slot_info[i];

		spin_lock_irqsave(&slot->lock, flags);
		task = slot->task;

		/* only care about io from upper layer */
		if (!task || (task->dev != device)
			|| (slot->state != HISI_SLOT_RUNNING)
			|| (slot->cmd_type != HISI_CMD_TYPE_NORMAL)) {
			spin_unlock_irqrestore(&slot->lock, flags);
			continue;
		}

		/* get io context */
		memset(&context, 0, sizeof(struct hisi_sas_io_context));
		context.event = HISI_SLOT_EVENT_COMPLETE;
		context.hba = hisi_hba;
		context.iptt = slot->idx;
		context.task = task;
		context.dev = device->lldd_dev;
		context.handler_locked = hisi_sas_free_slot;

		if (dev_is_sata(task->dev)) {
			struct ata_queued_cmd *qc = task->uldd_task;

			if (qc->scsicmd)
				context.handler = hisi_sas_report_fail_ata_io;
			else
				context.handler =
					hisi_sas_report_fail_internal_ata_io;
		} else {
			context.handler = hisi_sas_report_fail_ssp_smp_io;
		}
		spin_unlock_irqrestore(&slot->lock, flags);

		hisi_sas_slot_fsm(slot, &context);
	}
}

static void hisi_sas_phy_disconnected(struct hisi_sas_phy *phy)
{
	phy->phy_type = 0;
	phy->port = NULL;
	phy->linkrate = SAS_LINK_RATE_UNKNOWN;
}

void hisi_sas_phy_down(struct hisi_hba *hisi_hba, int phy_no, int rdy)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	struct device *dev = &hisi_hba->pdev->dev;

	if (rdy) {
		/* Phy down but ready */
		dev_info(dev, "%s phy %d down but ready!\n", __func__, phy_no);
		hisi_sas_bytes_dmaed(hisi_hba, phy_no);
		hisi_sas_port_notify_formed(sas_phy);
	} else {
		dev_info(dev, "%s phy %d down!\n", __func__, phy_no);

		/* phy is down */
		phy->phy_attached = 0;

		if (phy->is_flutter) {
			dev_info(dev, "%s,ignore flutter event of phy%d down!\n",
				__func__, phy_no);
			return;
		}

		hisi_sas_phy_disconnected(phy);
		sas_ha->notify_phy_event(sas_phy, PHYE_LOSS_OF_SIGNAL);
		sas_phy_disconnected(sas_phy);
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_phy_down);

static void hisi_sas_port_deformed(struct asd_sas_phy *sas_phy)
{
	struct asd_sas_port *sas_port = sas_phy->port;
	struct hisi_sas_phy *phy = container_of(sas_phy, typeof(*phy), sas_phy);
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct hisi_sas_port *port  = sas_port->lldd_port;
	struct device *dev = &hisi_hba->pdev->dev;
	unsigned long flags;

	dev_info(dev, "%s phy %d disconnect\n", __func__, sas_phy->id);

	spin_lock_irqsave(&hisi_hba->lock, flags);
	if (port) {
		port->phy_bitmap &= ~BIT(sas_phy->id);
		if (!port->phy_bitmap) {
			dev_info(dev, "%s, phy(%d) port(%d) hardware port(%d) has deformed!\n",
				__func__, sas_phy->id, sas_port->id, port->id);

			port->port_attached = 0;
			port->id = HISI_SAS_INVALID_PORT_ID;
		}
	}
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
}

static struct scsi_transport_template *hisi_sas_stt;

static struct scsi_host_template hisi_sas_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= hisi_sas_slave_configure,
	.scan_finished		= hisi_sas_scan_finished,
	.scan_start		= hisi_sas_scan_start,
	.change_queue_depth	= sas_change_queue_depth,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
};

static struct sas_domain_function_template hisi_sas_transport_ops = {
	.lldd_dev_found		= hisi_sas_dev_found,
	.lldd_dev_gone		= hisi_sas_dev_gone,
	.lldd_execute_task	= hisi_sas_queue_command,
	.lldd_control_phy	= hisi_sas_control_phy,
	.lldd_abort_task	= hisi_sas_abort_task,
	.lldd_abort_task_set	= hisi_sas_abort_task_set,
	.lldd_clear_aca		= hisi_sas_clear_aca,
	.lldd_I_T_nexus_reset	= hisi_sas_I_T_nexus_reset,
	.lldd_clear_nexus_ha	= hisi_sas_clear_nexus_ha,
	.lldd_lu_reset		= hisi_sas_lu_reset,
	.lldd_query_task	= hisi_sas_query_task,

	/* ata recovery called from check ready of libata eh */
	.lldd_ata_check_ready	= hisi_sas_ata_check_ready,

	.lldd_port_formed	= hisi_sas_port_formed,
	.lldd_port_deformed = hisi_sas_port_deformed,
	.lldd_before_dev_gone	= hisi_sas_before_dev_gone,
};

static int hisi_sas_alloc(struct hisi_hba *hisi_hba)
{
	struct platform_device *pdev = hisi_hba->pdev;
	struct device *dev = &pdev->dev;
	int i, s, max_command_entries = hisi_hba->hw->max_command_entries;
	struct hisi_sas_slot *slot;

	spin_lock_init(&hisi_hba->lock);
	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_init(hisi_hba, i);
		hisi_hba->port[i].port_attached = 0;
		hisi_hba->port[i].id = -1;
		INIT_LIST_HEAD(&hisi_hba->port[i].list);
	}

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		hisi_hba->devices[i].dev_type = SAS_PHY_UNUSED;
		hisi_hba->devices[i].device_id = i;
		hisi_hba->devices[i].dev_status = HISI_SAS_DEV_NORMAL;
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct hisi_sas_dq *dq = &hisi_hba->dq[i];

		/* Completion queue structure */
		cq->id = i;
		cq->hisi_hba = hisi_hba;
		cq->rd_point = 0;

		/* Delivery queue structure */
		dq->id = i;
		dq->hisi_hba = hisi_hba;
		dq->wr_point = 0;
		spin_lock_init(&dq->lock);

		/* Delivery queue */
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->cmd_hdr[i] = dma_alloc_coherent(dev, s,
					&hisi_hba->cmd_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->cmd_hdr[i])
			goto err_out;
		memset(hisi_hba->cmd_hdr[i], 0, s);

		/* Completion queue */
		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->complete_hdr[i] = dma_alloc_coherent(dev, s,
				&hisi_hba->complete_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->complete_hdr[i])
			goto err_out;
		memset(hisi_hba->complete_hdr[i], 0, s);
	}

	s = HISI_SAS_STATUS_BUF_SZ;
	hisi_hba->status_buffer_pool = dma_pool_create("status_buffer",
						       dev, s, 16, 0);
	if (!hisi_hba->status_buffer_pool)
		goto err_out;

	s = HISI_SAS_COMMAND_TABLE_SZ;
	hisi_hba->command_table_pool = dma_pool_create("command_table",
						       dev, s, 16, 0);
	if (!hisi_hba->command_table_pool)
		goto err_out;

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	hisi_hba->itct = dma_alloc_coherent(dev, s, &hisi_hba->itct_dma,
					    GFP_KERNEL);
	if (!hisi_hba->itct)
		goto err_out;

	memset(hisi_hba->itct, 0, s);

	s = max_command_entries * sizeof(struct hisi_sas_iost);
	hisi_hba->iost = dma_alloc_coherent(dev, s, &hisi_hba->iost_dma,
					    GFP_KERNEL);
	if (!hisi_hba->iost)
		goto err_out;

	memset(hisi_hba->iost, 0, s);

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint);
	hisi_hba->breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->breakpoint)
		goto err_out;

	memset(hisi_hba->breakpoint, 0, s);

	hisi_hba->slot_index_count = max_command_entries;
	s = hisi_hba->slot_index_count / BITS_PER_BYTE;
	hisi_hba->slot_index_tags = devm_kzalloc(dev, s, GFP_KERNEL);
	if (!hisi_hba->slot_index_tags)
		goto err_out;

	hisi_hba->sge_page_pool = dma_pool_create("status_sge", dev,
				sizeof(struct hisi_sas_sge_page), 16, 0);
	if (!hisi_hba->sge_page_pool)
		goto err_out;

#ifdef SAS_DIF
	hisi_hba->sge_dif_page_pool = dma_pool_create("status_sge", dev,
				sizeof(struct hisi_sas_sge_page), 16, 0);
	if (!hisi_hba->sge_dif_page_pool)
		goto err_out;
#endif

	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	hisi_hba->initial_fis = dma_alloc_coherent(dev, s,
				&hisi_hba->initial_fis_dma, GFP_KERNEL);
	if (!hisi_hba->initial_fis)
		goto err_out;
	memset(hisi_hba->initial_fis, 0, s);

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint) * 2;
	hisi_hba->sata_breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->sata_breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->sata_breakpoint)
		goto err_out;
	memset(hisi_hba->sata_breakpoint, 0, s);

	hisi_hba->slot_info = devm_kcalloc(dev, max_command_entries,
					   sizeof(struct hisi_sas_slot),
					   GFP_KERNEL);
	if (!hisi_hba->slot_info)
		goto err_out;

	for (i = 0; i < max_command_entries; i++) {
		slot = &hisi_hba->slot_info[i];
		slot->idx = i;

		spin_lock_init(&slot->lock);
		/* status buffer init */
		slot->status_buffer =
			dma_pool_alloc(hisi_hba->status_buffer_pool,
					GFP_ATOMIC, &slot->status_buffer_dma);
		if (!slot->status_buffer)
			goto err_out;

		/* command table init */
		slot->command_table =
			dma_pool_alloc(hisi_hba->command_table_pool,
				GFP_ATOMIC, &slot->command_table_dma);
		if (!slot->command_table)
			goto err_out;

		/* sge init */
		slot->sge_page = dma_pool_alloc(hisi_hba->sge_page_pool,
				GFP_ATOMIC, &slot->sge_page_dma);
		if (!slot->sge_page)
			goto err_out;

#ifdef SAS_DIF
		slot->sge_dif_page = dma_pool_alloc(hisi_hba->sge_dif_page_pool,
				GFP_ATOMIC, &slot->sge_dif_page_dma);
		if (!slot->sge_dif_page)
			goto err_out;
#endif
		hisi_sas_init_slot(slot);
	}
	hisi_sas_slot_index_init(hisi_hba);

	hisi_hba->wq = create_singlethread_workqueue(dev_name(dev));
	if (!hisi_hba->wq) {
		dev_err(dev, "sas_alloc: failed to create workqueue\n");
		goto err_out;
	}

	return 0;
err_out:
	return -ENOMEM;
}

static void hisi_sas_free(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	int i, s, max_command_entries = hisi_hba->hw->max_command_entries;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		if (hisi_hba->cmd_hdr[i]) {
			dma_free_coherent(dev, s,
					  hisi_hba->cmd_hdr[i],
					  hisi_hba->cmd_hdr_dma[i]);
			hisi_hba->cmd_hdr[i] = NULL;
			hisi_hba->cmd_hdr_dma[i] = (dma_addr_t)0;
		}

		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		if (hisi_hba->complete_hdr[i]) {
			dma_free_coherent(dev, s,
					  hisi_hba->complete_hdr[i],
					  hisi_hba->complete_hdr_dma[i]);
			hisi_hba->complete_hdr[i] = NULL;
			hisi_hba->complete_hdr_dma[i] = (dma_addr_t)0;
		}
	}

	if (hisi_hba->status_buffer_pool) {
		dma_pool_destroy(hisi_hba->status_buffer_pool);
		hisi_hba->status_buffer_pool = NULL;
	}

	if (hisi_hba->command_table_pool) {
		dma_pool_destroy(hisi_hba->command_table_pool);
		hisi_hba->command_table_pool = NULL;
	}

	if (hisi_hba->sge_page_pool) {
		dma_pool_destroy(hisi_hba->sge_page_pool);
		hisi_hba->sge_page_pool = NULL;
	}

#ifdef SAS_DIF
	if (hisi_hba->sge_dif_page_pool) {
		dma_pool_destroy(hisi_hba->sge_dif_page_pool);
		hisi_hba->sge_dif_page_pool = NULL;
	}
#endif

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	if (hisi_hba->itct) {
		dma_free_coherent(dev, s,
				  hisi_hba->itct, hisi_hba->itct_dma);
		hisi_hba->itct = NULL;
		hisi_hba->itct_dma = (dma_addr_t)0;
	}

	s = max_command_entries * sizeof(struct hisi_sas_iost);
	if (hisi_hba->iost) {
		dma_free_coherent(dev, s,
				  hisi_hba->iost, hisi_hba->iost_dma);
		hisi_hba->iost = NULL;
		hisi_hba->iost_dma = (dma_addr_t)0;
	}

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint);
	if (hisi_hba->breakpoint) {
		dma_free_coherent(dev, s,
				  hisi_hba->breakpoint,
				  hisi_hba->breakpoint_dma);
		hisi_hba->breakpoint = NULL;
		hisi_hba->breakpoint_dma = (dma_addr_t)0;
	}

	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	if (hisi_hba->initial_fis) {
		dma_free_coherent(dev, s,
				  hisi_hba->initial_fis,
				  hisi_hba->initial_fis_dma);
		hisi_hba->initial_fis = NULL;
		hisi_hba->initial_fis_dma = (dma_addr_t)0;
	}

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint) * 2;
	if (hisi_hba->sata_breakpoint) {
		dma_free_coherent(dev, s,
				  hisi_hba->sata_breakpoint,
				  hisi_hba->sata_breakpoint_dma);
		hisi_hba->sata_breakpoint = NULL;
		hisi_hba->sata_breakpoint_dma = (dma_addr_t)0;
	}

	if (hisi_hba->wq)
		destroy_workqueue(hisi_hba->wq);
}

static void hisi_sas_rst_work_handler(struct work_struct *work)
{
	struct hisi_hba *hisi_hba =
		container_of(work, struct hisi_hba, rst_work);

	if (hisi_hba)
		hisi_sas_chip_reset(hisi_hba);
}

static struct Scsi_Host *hisi_sas_shost_alloc(struct platform_device *pdev,
					      const struct hisi_sas_hw *hw)
{
	struct resource *res;
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;

	shost = scsi_host_alloc(&hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost)
		goto err_out;
	hisi_hba = shost_priv(shost);

	INIT_WORK(&hisi_hba->rst_work, hisi_sas_rst_work_handler);
	hisi_hba->hw = hw;
	hisi_hba->pdev = pdev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	init_timer(&hisi_hba->timer);

	if (device_property_read_u8_array(dev, "sas-addr", hisi_hba->sas_addr,
					  SAS_ADDR_SIZE))
		goto err_out;

	if (np) {
		hisi_hba->ctrl = syscon_regmap_lookup_by_phandle(np,
					"hisilicon,sas-syscon");
		if (IS_ERR(hisi_hba->ctrl))
			goto err_out;

		if (device_property_read_u32(dev, "ctrl-reset-reg",
					     &hisi_hba->ctrl_reset_reg))
			goto err_out;

		if (device_property_read_u32(dev, "ctrl-reset-sts-reg",
					     &hisi_hba->ctrl_reset_sts_reg))
			goto err_out;

		if (device_property_read_u32(dev, "ctrl-clock-ena-reg",
					     &hisi_hba->ctrl_clock_ena_reg))
			goto err_out;
	}

	if (device_property_read_u32(dev, "phy-count", &hisi_hba->n_phy))
		goto err_out;

	if (device_property_read_u32(dev, "queue-count",
				     &hisi_hba->queue_count))
		goto err_out;

	if (device_property_read_u32(dev, "controller-id",
				     &hisi_hba->core_id))
		goto err_out;

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)) &&
		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))) {
		dev_err(dev, "No usable DMA addressing method\n");
		goto err_out;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hisi_hba->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hisi_hba->regs))
		goto err_out;

#ifdef SAS_DIF
	scsi_host_set_prot(hisi_hba->shost, hisi_hba->hw->prot_cap);
	scsi_host_set_guard(hisi_hba->shost, SHOST_DIX_GUARD_CRC);
#endif

	if (hisi_sas_alloc(hisi_hba)) {
		hisi_sas_free(hisi_hba);
		goto err_out;
	}

	return shost;
err_out:
	dev_err(dev, "shost alloc failed\n");
	return NULL;
}

static void hisi_sas_init_add(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		memcpy(&hisi_hba->phy[i].dev_sas_addr,
		       hisi_hba->sas_addr,
		       SAS_ADDR_SIZE);
}
void hisi_chip_fatal_examine(struct hisi_hba *hisi_hba)
{
	if (hisi_hba->hw->chip_fatal_check &&
		hisi_hba->hw->chip_fatal_check(hisi_hba)) {
		if (!test_bit(HISI_SAS_RESET_BIT, &hisi_hba->rst_flag))
			/*reset chip*/
			queue_work(hisi_hba->wq, &hisi_hba->rst_work);
	}
}

static void hisi_routine_test_func(unsigned long data)
{
	struct hisi_hba *hisi_hba = (struct hisi_hba *)data;
	struct device *dev = &hisi_hba->pdev->dev;

	if (!hisi_hba) {
		dev_info(dev, "hisi_hba get failed.\n");
		return;
	}

	/* chip fatal error check */
	hisi_chip_fatal_examine(hisi_hba);

	mod_timer(&hisi_hba->routine_timer, jiffies + msecs_to_jiffies(1000));
}

static int hisi_routine_test(struct hisi_hba *hisi_hba)
{
	struct timer_list *timer = &hisi_hba->routine_timer;

	init_timer(timer);
	timer->data = (unsigned long)hisi_hba;
	timer->expires = jiffies + msecs_to_jiffies(1000);
	timer->function = hisi_routine_test_func;

	add_timer(timer);

	return 0;
}

int hisi_sas_probe(struct platform_device *pdev,
			 const struct hisi_sas_hw *hw)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha;
	int rc, phy_nr, port_nr, i;

	shost = hisi_sas_shost_alloc(pdev, hw);
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_ha;
	}

	sha = SHOST_TO_SAS_HA(shost);
	hisi_hba = shost_priv(shost);
	platform_set_drvdata(pdev, sha);
	phy_nr = port_nr = hisi_hba->n_phy;

	arr_phy = devm_kcalloc(dev, phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = devm_kcalloc(dev, port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port)
		return -ENOMEM;

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->core.shost = shost;
	sha->lldd_ha = hisi_hba;

	shost->transportt = hisi_sas_stt;
	shost->max_id = HISI_SAS_MAX_DEVICES;
	shost->max_lun = ~0;
	shost->max_channel = 1;
	shost->max_cmd_len = 16;
	shost->sg_tablesize = min_t(u16, SG_ALL, HISI_SAS_SGE_PAGE_CNT);
	shost->can_queue = hisi_hba->hw->max_command_entries;
	shost->cmd_per_lun = hisi_hba->hw->max_command_entries;

	sha->sas_ha_name = DRV_NAME;
	sha->dev = &hisi_hba->pdev->dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &hisi_hba->sas_addr[0];
	sha->num_phys = hisi_hba->n_phy;
	sha->core.shost = hisi_hba->shost;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		sha->sas_phy[i] = &hisi_hba->phy[i].sas_phy;
		sha->sas_port[i] = &hisi_hba->port[i].sas_port;
	}

	hisi_sas_init_add(hisi_hba);

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc)
		goto err_out_ha;

	rc = sas_register_ha(sha);
	if (rc)
		goto err_out_register_ha;

	/* fix the map between sas_port and hisi_sas_port */
	for (i = 0; i < hisi_hba->n_phy; i++)
		sha->sas_port[i]->lldd_port = &hisi_hba->port[i];

	rc = hisi_hba->hw->hw_init(hisi_hba);
	if (rc)
		goto err_out_register_ha;

	scsi_scan_host(shost);

	hisi_routine_test(hisi_hba);

	return 0;

err_out_register_ha:
	scsi_remove_host(shost);
err_out_ha:
	kfree(shost);
	return rc;
}
EXPORT_SYMBOL_GPL(hisi_sas_probe);

int hisi_sas_remove(struct platform_device *pdev)
{
	struct sas_ha_struct *sha = platform_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	del_timer_sync(&hisi_hba->routine_timer);

	scsi_remove_host(sha->core.shost);
	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_free(hisi_hba);
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_remove);

static __init int hisi_sas_init(void)
{
	pr_info("hisi_sas: driver version %s\n", DRV_VERSION);

	hisi_sas_stt = sas_domain_attach_transport(&hisi_sas_transport_ops);
	if (!hisi_sas_stt)
		return -ENOMEM;

	return 0;
}

static __exit void hisi_sas_exit(void)
{
	sas_release_transport(hisi_sas_stt);
}

module_init(hisi_sas_init);
module_exit(hisi_sas_exit);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
