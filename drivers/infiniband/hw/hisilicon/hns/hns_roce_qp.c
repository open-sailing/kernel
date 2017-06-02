/*
 * Copyright (c) 2016 Hisilicon Limited.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/platform_device.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_umem.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_hem.h"
#include "hns_roce_user.h"

#define SQP_NUM				(2 * HNS_ROCE_MAX_PORTS)

void hns_roce_qp_event(struct hns_roce_dev *hr_dev, u32 qpn, int port,
	int event_type)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_qp *qp;
	u32 real_qpn;

	real_qpn = hns_get_real_qpn(qpn, hr_dev->caps.num_ports, port);
	spin_lock(&qp_table->lock);
	qp = __hns_roce_qp_lookup(hr_dev, real_qpn);
	if (qp)
		atomic_inc(&qp->refcount);

	spin_unlock(&qp_table->lock);

	if (!qp) {
		dev_err(dev, "Async event for non-existent QP 0x%08x\n", qpn);
		return;
	}

	qp->event(qp, (enum hns_roce_event)event_type);

	if (atomic_dec_and_test(&qp->refcount))
		complete(&qp->free);
}

static void hns_roce_ib_qp_event(struct hns_roce_qp *hr_qp,
				 enum hns_roce_event type)
{
	struct ib_event event;
	struct ib_qp *ibqp = &hr_qp->ibqp;

	if (ibqp->event_handler) {
		event.device = ibqp->device;
		event.element.qp = ibqp;
		switch (type) {
		case HNS_ROCE_EVENT_TYPE_PATH_MIG:
			event.event = IB_EVENT_PATH_MIG;
			break;
		case HNS_ROCE_EVENT_TYPE_COMM_EST:
			event.event = IB_EVENT_COMM_EST;
			break;
		case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
			event.event = IB_EVENT_SQ_DRAINED;
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
			event.event = IB_EVENT_QP_LAST_WQE_REACHED;
			break;
		case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
			event.event = IB_EVENT_QP_FATAL;
			break;
		case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
			event.event = IB_EVENT_PATH_MIG_ERR;
			break;
		case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
			event.event = IB_EVENT_QP_REQ_ERR;
			break;
		case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			break;
		default:
			dev_dbg(ibqp->device->dma_device, "roce_ib: Unexpected event type %d on QP %06lx\n",
				type, hr_qp->qpn);
			return;
		}
		ibqp->event_handler(&event, ibqp->qp_context);
	}
}

static int hns_roce_reserve_range_qp(struct hns_roce_dev *hr_dev, int cnt,
				     int align, unsigned long *base)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	return hns_roce_bitmap_alloc_range(&qp_table->bitmap, cnt, align, base);
}

enum hns_roce_qp_state to_hns_roce_state(enum ib_qp_state state)
{
	switch (state) {
	case IB_QPS_RESET:
		return HNS_ROCE_QP_STATE_RST;
	case IB_QPS_INIT:
		return HNS_ROCE_QP_STATE_INIT;
	case IB_QPS_RTR:
		return HNS_ROCE_QP_STATE_RTR;
	case IB_QPS_RTS:
		return HNS_ROCE_QP_STATE_RTS;
	case IB_QPS_SQD:
		return HNS_ROCE_QP_STATE_SQD;
	case IB_QPS_ERR:
		return HNS_ROCE_QP_STATE_ERR;
	default:
		return HNS_ROCE_QP_NUM_STATE;
	}
}

static int hns_roce_gsi_qp_alloc(struct hns_roce_dev *hr_dev, unsigned long qpn,
				 struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	struct device *dev = &hr_dev->pdev->dev;
	int ret;

	if (!qpn) {
		dev_err(dev, "Qpn must not 0! Current qpn is 0x%lx.\n", qpn);
		return -EINVAL;
	}

	hr_qp->qpn = qpn;

	spin_lock_irq(&qp_table->lock);
	ret = radix_tree_insert(&hr_dev->qp_table_tree,
				hr_qp->qpn & (hr_dev->caps.num_qps - 1), hr_qp);
	spin_unlock_irq(&qp_table->lock);
	if (ret) {
		dev_err(&hr_dev->pdev->dev, "QPC radix_tree_insert failed\n");
		goto err_put_irrl;
	}

	atomic_set(&hr_qp->refcount, 1);
	init_completion(&hr_qp->free);

	return 0;

err_put_irrl:

	return ret;
}

static int hns_roce_qp_alloc(struct hns_roce_dev *hr_dev, unsigned long qpn,
			     struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	struct device *dev = &hr_dev->pdev->dev;
	int ret;

	if (!qpn) {
		dev_err(dev, "Qpn must not 0! Current qpn is 0x%lx!\n", qpn);
		return -EINVAL;
	}

	hr_qp->qpn = qpn;

	/* Alloc memory for QPC */
	ret = hns_roce_table_get(hr_dev, &qp_table->qp_table, hr_qp->qpn);
	if (ret) {
		dev_err(dev, "QPC table get failed\n");
		goto err_out;
	}

	/* Alloc memory for IRRL */
	ret = hns_roce_table_get(hr_dev, &qp_table->irrl_table, hr_qp->qpn);
	if (ret) {
		dev_err(dev, "IRRL table get failed\n");
		goto err_put_qp;
	}

	spin_lock_irq(&qp_table->lock);
	ret = radix_tree_insert(&hr_dev->qp_table_tree,
				hr_qp->qpn & (hr_dev->caps.num_qps - 1), hr_qp);
	spin_unlock_irq(&qp_table->lock);
	if (ret) {
		dev_err(dev, "QPC radix_tree_insert failed\n");
		goto err_put_irrl;
	}

	atomic_set(&hr_qp->refcount, 1);
	init_completion(&hr_qp->free);

	return 0;

err_put_irrl:
	hns_roce_table_put(hr_dev, &qp_table->irrl_table, hr_qp->qpn);

err_put_qp:
	hns_roce_table_put(hr_dev, &qp_table->qp_table, hr_qp->qpn);

err_out:
	return ret;
}

void hns_roce_qp_remove(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	struct device *dev = &hr_dev->pdev->dev;
	unsigned long flags;
	void *entry;

	spin_lock_irqsave(&qp_table->lock, flags);
	entry = radix_tree_delete(&hr_dev->qp_table_tree,
		hr_qp->qpn & (hr_dev->caps.num_qps - 1));
	spin_unlock_irqrestore(&qp_table->lock, flags);

	if (!entry)
		dev_err(dev, "QP(0x%lx) remove radix_tree_delete failed.\n",
			hr_qp->qpn);
}

void hns_roce_qp_free(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	if (atomic_dec_and_test(&hr_qp->refcount))
		complete(&hr_qp->free);
	wait_for_completion(&hr_qp->free);

	if ((hr_qp->ibqp.qp_type) != IB_QPT_GSI) {
		hns_roce_table_put(hr_dev, &qp_table->irrl_table, hr_qp->qpn);
		hns_roce_table_put(hr_dev, &qp_table->qp_table, hr_qp->qpn);
	}
}

void hns_roce_release_range_qp(struct hns_roce_dev *hr_dev, int base_qpn,
			       int cnt)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	if (base_qpn < (hr_dev->caps.sqp_start + 2 * hr_dev->caps.num_ports))
		return;

	hns_roce_bitmap_free_range(&qp_table->bitmap, base_qpn, cnt, 1);
}

static int hns_roce_set_rq_size(struct hns_roce_dev *hr_dev,
				struct ib_qp_cap *cap, int is_user, int has_srq,
				struct hns_roce_qp *hr_qp)
{
	u32 max_cnt;
	struct device *dev = &hr_dev->pdev->dev;

	/* Check the validity of QP support capacity */
	if (cap->max_recv_wr > hr_dev->caps.max_wqes ||
	    cap->max_recv_sge > hr_dev->caps.max_rq_sg) {
		dev_err(dev, "RQ WR or sge error! Max_recv_wr = %d, max_recv_sge = %d\n",
			cap->max_recv_wr, cap->max_recv_sge);
		return -EINVAL;
	}

	/* If srq exit, set zero for relative number of rq */
	if (has_srq) {
		if (cap->max_recv_wr) {
			dev_err(dev, "Srq no need config max_recv_wr\n");
			return -EINVAL;
		}

		hr_qp->rq.wqe_cnt = hr_qp->rq.max_gs = 0;
	} else {
		if (is_user && (!cap->max_recv_wr || !cap->max_recv_sge)) {
			dev_err(dev, "User space no need config max_recv_wr and max_recv_sge\n");
			return -EINVAL;
		}

		/* In v1 engine, parameter verification procession */
		max_cnt = cap->max_recv_wr > HNS_ROCE_MIN_WQE_NUM ?
			  cap->max_recv_wr : HNS_ROCE_MIN_WQE_NUM;
		hr_qp->rq.wqe_cnt = roundup_pow_of_two(max_cnt);

		if ((u32)hr_qp->rq.wqe_cnt > hr_dev->caps.max_wqes) {
			dev_err(dev, "Rq.wqe_cnt(0x%x) beyond caps of max_wqes(0x%x).\n",
				hr_qp->rq.wqe_cnt, hr_dev->caps.max_wqes);
			return -EINVAL;
		}

		max_cnt = max(1U, cap->max_recv_sge);
		hr_qp->rq.max_gs = roundup_pow_of_two(max_cnt);
		/* WQE is fixed for 64B */
		hr_qp->rq.wqe_shift = ilog2(hr_dev->caps.max_rq_desc_sz);
	}

	cap->max_recv_wr = hr_qp->rq.max_post = hr_qp->rq.wqe_cnt;
	cap->max_recv_sge = hr_qp->rq.max_gs;

	return 0;
}

static int hns_roce_set_user_sq_size(struct hns_roce_dev *hr_dev,
				     struct hns_roce_qp *hr_qp,
				     struct hns_roce_ib_create_qp *ucmd)
{
	u32 roundup_sq_stride = roundup_pow_of_two(hr_dev->caps.max_sq_desc_sz);
	u8 max_sq_stride = ilog2(roundup_sq_stride);

	/* Sanity check SQ size before proceeding */
	if ((u32)(1 << ucmd->log_sq_bb_count) > hr_dev->caps.max_wqes ||
	     ucmd->log_sq_stride > max_sq_stride ||
	     ucmd->log_sq_stride < HNS_ROCE_IB_MIN_SQ_STRIDE) {
		dev_err(&hr_dev->pdev->dev, "SQ size check error! sq bb count(0x%x), sq_stride(0x%x), caps of max_wqes(0x%x).\n",
			ucmd->log_sq_bb_count, ucmd->log_sq_stride,
			hr_dev->caps.max_wqes);
		return -EINVAL;
	}

	hr_qp->sq.wqe_cnt = 1 << ucmd->log_sq_bb_count;
	hr_qp->sq.wqe_shift = ucmd->log_sq_stride;

	/* Get buf size, SQ and RQ  are aligned to page_szie */
	hr_qp->buff_size = HNS_ROCE_ALOGN_UP((hr_qp->rq.wqe_cnt <<
					     hr_qp->rq.wqe_shift), PAGE_SIZE) +
			   HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);

	hr_qp->sq.offset = 0;
	hr_qp->rq.offset = HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);

	return 0;
}

static int hns_roce_set_kernel_sq_size(struct hns_roce_dev *hr_dev,
		struct ib_qp_cap *cap, struct hns_roce_qp *hr_qp)
{
	struct device *dev = &hr_dev->pdev->dev;
	u32 max_cnt;

	if (cap->max_send_wr  > hr_dev->caps.max_wqes  ||
	    cap->max_send_sge > hr_dev->caps.max_sq_sg ||
	    cap->max_inline_data > hr_dev->caps.max_sq_inline) {
		dev_err(dev, "Sq size check error! send wr(0x%x), send sge(0x%x), inline data(0x%x).\n",
			cap->max_send_wr, cap->max_send_sge,
			cap->max_inline_data);
		dev_err(dev, "caps:wqes(0x%x), sq sg(0x%x), sq inline(0x%x).\n",
			hr_dev->caps.max_wqes, hr_dev->caps.max_sq_sg,
			hr_dev->caps.max_sq_inline);
		return -EINVAL;
	}

	hr_qp->sq.wqe_shift = ilog2(hr_dev->caps.max_sq_desc_sz);
	hr_qp->sq_max_wqes_per_wr = 1;
	hr_qp->sq_spare_wqes = 0;

	/* In v1 engine, parameter verification procession */
	max_cnt = cap->max_send_wr > HNS_ROCE_MIN_WQE_NUM ?
		  cap->max_send_wr : HNS_ROCE_MIN_WQE_NUM;
	hr_qp->sq.wqe_cnt = roundup_pow_of_two(max_cnt);
	if ((u32)hr_qp->sq.wqe_cnt > hr_dev->caps.max_wqes) {
		dev_err(dev, "Set sq wqe_cnt(0x%x) exceed caps of max wqes(0x%x)!\n",
			hr_qp->sq.wqe_cnt, hr_dev->caps.max_wqes);
		return -EINVAL;
	}

	/* Get data_seg numbers */
	max_cnt = max(1U, cap->max_send_sge);
	hr_qp->sq.max_gs = roundup_pow_of_two(max_cnt);

	/* Get buf size, SQ and RQ  are aligned to page_szie */
	hr_qp->buff_size = HNS_ROCE_ALOGN_UP((hr_qp->rq.wqe_cnt <<
					     hr_qp->rq.wqe_shift), PAGE_SIZE) +
			   HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);
	hr_qp->sq.offset = 0;
	hr_qp->rq.offset = HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					      hr_qp->sq.wqe_shift), PAGE_SIZE);

	/* Get wr and sge number which send */
	cap->max_send_wr = hr_qp->sq.max_post = hr_qp->sq.wqe_cnt;
	cap->max_send_sge = hr_qp->sq.max_gs;

	/* We don't support inline sends for kernel QPs (yet) */
	cap->max_inline_data = 0;

	return 0;
}

static int roce_umem_init_write_mtt(struct hns_roce_dev *hr_dev,
	struct ib_pd *ib_pd, struct ib_udata *udata, struct hns_roce_qp *hr_qp)
{
	struct device *dev = &hr_dev->pdev->dev;
	int ret = 0;
	struct hns_roce_ib_create_qp ucmd;

	ret = ib_copy_from_udata(&ucmd, udata, sizeof(ucmd));
	if (ret) {
		dev_err(dev, "Copy data from user failed(%d) for init write mtt!\n",
			ret);
		return ret;
	}

	ret = hns_roce_set_user_sq_size(hr_dev, hr_qp, &ucmd);
	if (ret) {
		dev_err(dev, "Set user sq size failed(%d) for init write mtt!\n",
			ret);
		return ret;
	}

	hr_qp->umem = ib_umem_get(ib_pd->uobject->context, ucmd.buf_addr,
		hr_qp->buff_size, 0, 0);
	if (IS_ERR(hr_qp->umem)) {
		dev_err(dev, "Ib_umem_get error for init write mtt!\n");
		return PTR_ERR(hr_qp->umem);
	}

	ret = hns_roce_mtt_init(hr_dev, ib_umem_page_count(hr_qp->umem),
		ilog2((unsigned int)hr_qp->umem->page_size), &hr_qp->mtt);
	if (ret) {
		dev_err(dev, "Hns_roce_mtt_init error(%d) for init write mtt!\n",
			ret);
		goto err_buf;
	}

	ret = hns_roce_ib_umem_write_mtt(hr_dev, &hr_qp->mtt, hr_qp->umem);
	if (ret) {
		dev_err(dev, "hns_roce_ib_umem_write_mtt error(%d) for init write mtt!\n",
			ret);
		goto err_mtt;
	}

	return 0;

err_mtt:
	hns_roce_mtt_cleanup(hr_dev, &hr_qp->mtt);

err_buf:
	ib_umem_release(hr_qp->umem);
	hr_qp->umem = NULL;
	return ret;
}

static int roce_kernel_init_write_mtt(struct hns_roce_dev *hr_dev,
	struct ib_qp_init_attr *init_attr, struct hns_roce_qp *hr_qp)
{
	struct device *dev = &hr_dev->pdev->dev;
	int ret = 0;

	if (init_attr->create_flags & IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK) {
		dev_err(dev, "Init_attr->create_flags(0x%x) error!\n",
			init_attr->create_flags);
		return -EINVAL;
	}

	if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO) {
		dev_err(dev, "Init_attr->create_flags(0x%x) error!\n",
			init_attr->create_flags);
		return -EINVAL;
	}

	/* Set SQ size */
	ret = hns_roce_set_kernel_sq_size(hr_dev, &init_attr->cap, hr_qp);
	if (ret) {
		dev_err(dev, "Hns_roce_set_kernel_sq_size error(%d) in qp create!\n",
			ret);
		return ret;
	}

	/* QP doorbell register address */
	hr_qp->sq.db_reg_l = hr_dev->reg_base + ROCEE_DB_SQ_L_0_REG
		+ DB_REG_OFFSET * hr_dev->priv_uar.index;
	hr_qp->rq.db_reg_l = hr_dev->reg_base + ROCEE_DB_OTHERS_L_0_REG
		+ DB_REG_OFFSET * hr_dev->priv_uar.index;

	/* Allocate QP buf */
	ret = hns_roce_buf_alloc(hr_dev, hr_qp->buff_size, PAGE_SIZE * 2,
			&hr_qp->hr_buf);
	if (ret) {
		dev_err(dev, "Hns_roce_buf_alloc error(%d)!\n", ret);
		return ret;
	}

	/* Write MTT */
	ret = hns_roce_mtt_init(hr_dev, hr_qp->hr_buf.npages,
			hr_qp->hr_buf.page_shift, &hr_qp->mtt);
	if (ret) {
		dev_err(dev, "Hns_roce_mtt_init error(%d) for kernel create qp!\n",
			ret);
		goto err_buf;
	}

	ret = hns_roce_buf_write_mtt(hr_dev, &hr_qp->mtt, &hr_qp->hr_buf);
	if (ret) {
		dev_err(dev, "Hns_roce_buf_write_mtt error(%d) for kernel create qp.\n",
			ret);
		goto err_mtt;
	}

	hr_qp->sq.wrid = kcalloc(hr_qp->sq.wqe_cnt, sizeof(u64), GFP_KERNEL);
	hr_qp->rq.wrid = kcalloc(hr_qp->rq.wqe_cnt, sizeof(u64), GFP_KERNEL);
	if (!hr_qp->sq.wrid || !hr_qp->rq.wrid) {
		ret = -ENOMEM;
		goto err_wrid;
	}

	return 0;

err_wrid:
	kfree(hr_qp->sq.wrid);
	hr_qp->sq.wrid = NULL;
	kfree(hr_qp->rq.wrid);
	hr_qp->rq.wrid = NULL;
err_mtt:
	hns_roce_mtt_cleanup(hr_dev, &hr_qp->mtt);
err_buf:
	hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);
	return ret;
}

static void roce_clean_mtt_buf(struct ib_pd *ib_pd,
	struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	hns_roce_mtt_cleanup(hr_dev, &hr_qp->mtt);

	if (ib_pd->uobject) {
		kfree(hr_qp->sq.wrid);
		hr_qp->sq.wrid = NULL;
		kfree(hr_qp->rq.wrid);
		hr_qp->rq.wrid = NULL;

		ib_umem_release(hr_qp->umem);
		hr_qp->umem = NULL;
	} else {
		hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);
	}
}

static int hns_roce_create_qp_common(struct hns_roce_dev *hr_dev,
		struct ib_pd *ib_pd,
		struct ib_qp_init_attr *init_attr,
		struct ib_udata *udata, unsigned long sqpn,
		struct hns_roce_qp *hr_qp)
{
	struct device *dev = &hr_dev->pdev->dev;
	unsigned long qpn = 0;
	int ret = 0;

	mutex_init(&hr_qp->mutex);
	spin_lock_init(&hr_qp->sq.lock);
	spin_lock_init(&hr_qp->rq.lock);

	hr_qp->state = IB_QPS_RESET;

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		hr_qp->sq_signal_bits = IB_SIGNAL_ALL_WR;
	else
		hr_qp->sq_signal_bits = IB_SIGNAL_REQ_WR;

	ret = hns_roce_set_rq_size(hr_dev, &init_attr->cap, !!ib_pd->uobject,
		!!init_attr->srq, hr_qp);
	if (ret) {
		dev_err(dev, "Hns_roce_set_rq_size failed(%d)!\n", ret);
		return ret;
	}

	ret = (ib_pd->uobject) ?
		roce_umem_init_write_mtt(hr_dev, ib_pd, udata, hr_qp) :
		roce_kernel_init_write_mtt(hr_dev, init_attr, hr_qp);
	if (ret) {
		dev_err(dev, "Mem write or init mtt error for create qp,uobject(%p)\n",
			ib_pd->uobject);
		return ret;
	}

	if (sqpn) {
		qpn = sqpn;
	} else {
		/* Get QPN */
		ret = hns_roce_reserve_range_qp(hr_dev, 1, 1, &qpn);
		if (ret) {
			dev_err(dev, "Hns_roce_reserve_range_qp alloc qpn failed(%d)!\n",
				ret);
			goto err_wrid;
		}
	}

	if ((init_attr->qp_type) == IB_QPT_GSI) {
		ret = hns_roce_gsi_qp_alloc(hr_dev, qpn, hr_qp);
		if (ret) {
			dev_err(dev, "Hns_roce_gsi_qp_alloc failed(%d)!\n",
				ret);
			goto err_qpn;
		}
	} else {
		ret = hns_roce_qp_alloc(hr_dev, qpn, hr_qp);
		if (ret) {
			dev_err(dev, "Hns_roce_qp_alloc failed(%d)!\n", ret);
			goto err_qpn;
		}
	}

	if (sqpn)
		hr_qp->doorbell_qpn = 1;
	else
		hr_qp->doorbell_qpn = cpu_to_le64(hr_qp->qpn);

	hr_qp->event = hns_roce_ib_qp_event;

	return 0;

err_qpn:
	if (!sqpn)
		hns_roce_release_range_qp(hr_dev, qpn, 1);
err_wrid:
	roce_clean_mtt_buf(ib_pd, hr_dev, hr_qp);

	return ret;
}

struct ib_qp *hns_roce_create_qp(struct ib_pd *pd,
				 struct ib_qp_init_attr *init_attr,
				 struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_sqp *hr_sqp;
	struct hns_roce_qp *hr_qp;
	u32 gsi_qpn = 0;
	int ret;

	switch (init_attr->qp_type) {
	case IB_QPT_RC: {
		hr_qp = kzalloc(sizeof(*hr_qp), GFP_KERNEL);
		if (!hr_qp)
			return ERR_PTR(-ENOMEM);

		ret = hns_roce_create_qp_common(hr_dev, pd, init_attr, udata, 0,
						hr_qp);
		if (ret) {
			dev_err(dev, "Create qp common failed(%d)!\n", ret);
			kfree(hr_qp);
			return ERR_PTR(ret);
		}

		hr_qp->ibqp.qp_num = hr_qp->qpn;

		break;
	}
	case IB_QPT_GSI: {
		/* Userspace is not allowed to create special QPs: */
		if (pd->uobject) {
			dev_err(dev, "Don't support usr space GSI\n");
			return ERR_PTR(-EINVAL);
		}

		hr_sqp = kzalloc(sizeof(*hr_sqp), GFP_KERNEL);
		if (!hr_sqp)
			return ERR_PTR(-ENOMEM);

		hr_qp = &hr_sqp->hr_qp;

		gsi_qpn = hr_dev->caps.sqp_start +
			hns_get_real_qpn(1, hr_dev->caps.num_ports,
				init_attr->port_num - 1);
		ret = hns_roce_create_qp_common(hr_dev, pd, init_attr,
			udata, gsi_qpn, hr_qp);
		if (ret) {
			dev_err(dev, "Create qp common for GSI failed(%d)!\n",
				ret);
			kfree(hr_sqp);
			return ERR_PTR(ret);
		}

		hr_qp->port = init_attr->port_num - 1;
		hr_qp->ibqp.qp_num = gsi_qpn;
		break;
	}
	default:{
		/* Don't support raw QPs and SMI QP */
		dev_err(dev, "Not support qp type(%d)!\n", init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}
	}

	return &hr_qp->ibqp;
}

int to_hr_qp_type(int qp_type)
{
	int transport_type;

	if (qp_type == IB_QPT_RC)
		transport_type = SERV_TYPE_RC;
	else if (qp_type == IB_QPT_UC)
		transport_type = SERV_TYPE_UC;
	else if (qp_type == IB_QPT_UD)
		transport_type = SERV_TYPE_UD;
	else if (qp_type == IB_QPT_GSI)
		transport_type = SERV_TYPE_UD;
	else
		transport_type = -1;

	return transport_type;
}

static int qp_attr_valid_check(struct ib_qp *ibqp,
				const struct ib_qp_attr *attr, int attr_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct device *dev = &hr_dev->pdev->dev;
	int p = 0;
	u32 active_mtu = 0;

	if ((attr_mask & IB_QP_PORT) &&
		(attr->port_num == 0 ||
		attr->port_num > hr_dev->caps.num_ports)) {
		dev_err(dev, "Attr port_num invalid! attr->port_num=%d\n",
			attr->port_num);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		p = attr_mask & IB_QP_PORT ? attr->port_num : (hr_qp->port + 1);
		if (attr->pkey_index >= hr_dev->caps.pkey_table_len[p]) {
			dev_err(dev, "attr pkey_index invalid! attr->pkey_index=%d,p =%d\n",
				attr->pkey_index, hr_qp->port);
			return -EINVAL;
		}
	}

	if (attr_mask & IB_QP_PATH_MTU) {
		p = (attr_mask & IB_QP_PORT) ? (attr->port_num - 1) :
			hr_qp->port;
		active_mtu = iboe_get_mtu(hr_dev->iboe.netdevs[p]->mtu);

		if (attr->path_mtu > IB_MTU_2048 ||
			attr->path_mtu < IB_MTU_256 ||
			attr->path_mtu > active_mtu) {
			dev_err(dev, "attr path_mtu(%d)invalid while modify qp",
				attr->path_mtu);
			return -EINVAL;
		}
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
		attr->max_rd_atomic > hr_dev->caps.max_qp_init_rdma) {
		dev_err(dev, "attr max_rd_atomic invalid while modify qp(0x%x), attr->max_rd_atomic=%d.\n",
			ibqp->qp_num, attr->max_rd_atomic);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
		attr->max_dest_rd_atomic > hr_dev->caps.max_qp_dest_rdma) {
		dev_err(dev, "attr max_dest_rd_atomic invalid while modify qp(0x%x), attr->max_dest_rd_atomic=%d\n",
			ibqp->qp_num, attr->max_dest_rd_atomic);
		return -EINVAL;
	}

	return 0;
}

int hns_roce_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		       int attr_mask, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	enum ib_qp_state cur_state, new_state;
	struct device *dev = &hr_dev->pdev->dev;
	int ret = -EINVAL;

	mutex_lock(&hr_qp->mutex);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		    attr->cur_qp_state : (enum ib_qp_state)hr_qp->state;
	new_state = attr_mask & IB_QP_STATE ?
		    attr->qp_state : cur_state;

	ret = ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type, attr_mask,
				 IB_LINK_LAYER_ETHERNET);
	if (!ret) {
		dev_err(dev, "Ib_modify_qp_is_ok failed(%d).\n", ret);
		goto out;
	}

	ret = qp_attr_valid_check(ibqp, attr, attr_mask);
	if (ret)
		goto out;

	if (cur_state == new_state && cur_state == IB_QPS_RESET) {
		ret = -EPERM;
		dev_err(dev, "State error while modify qp(0x%x). cur_state=%d, new_state=%d\n",
			ibqp->qp_num, cur_state, new_state);
		goto out;
	}

	ret = hr_dev->hw->modify_qp(ibqp, attr, attr_mask, cur_state,
				    new_state);

out:
	mutex_unlock(&hr_qp->mutex);

	return ret;
}

void hns_roce_lock_cqs(struct hns_roce_cq *send_cq, struct hns_roce_cq *recv_cq)
		       __acquires(&send_cq->lock) __acquires(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		spin_lock_irq(&send_cq->lock);
		__acquire(&recv_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		spin_lock_irq(&send_cq->lock);
		spin_lock_nested(&recv_cq->lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock_irq(&recv_cq->lock);
		spin_lock_nested(&send_cq->lock, SINGLE_DEPTH_NESTING);
	}
}

void hns_roce_unlock_cqs(struct hns_roce_cq *send_cq,
			 struct hns_roce_cq *recv_cq) __releases(&send_cq->lock)
			 __releases(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		__release(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		spin_unlock(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else {
		spin_unlock(&send_cq->lock);
		spin_unlock_irq(&recv_cq->lock);
	}
}

__be32 send_ieth(struct ib_send_wr *wr)
{
	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		return cpu_to_le32(wr->ex.imm_data);
	case IB_WR_SEND_WITH_INV:
		return cpu_to_le32(wr->ex.invalidate_rkey);
	default:
		return 0;
	}
}

static void *get_wqe(struct hns_roce_qp *hr_qp, int offset)
{

	return hns_roce_buf_offset(&hr_qp->hr_buf, offset);
}

void *get_recv_wqe(struct hns_roce_qp *hr_qp, int n)
{
	struct ib_qp *ibqp = &hr_qp->ibqp;
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);

	if ((n < 0) || (n > hr_qp->rq.wqe_cnt)) {
		dev_err(&hr_dev->pdev->dev, "rq wqe index:%d,rq wqe cnt:%d.\n",
			n, hr_qp->rq.wqe_cnt);
		return NULL;
	}

	return get_wqe(hr_qp, hr_qp->rq.offset + (n << hr_qp->rq.wqe_shift));
}

void *get_send_wqe(struct hns_roce_qp *hr_qp, int n)
{
	struct ib_qp *ibqp = &hr_qp->ibqp;
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);

	if ((n < 0) || (n > hr_qp->sq.wqe_cnt)) {
		dev_err(&hr_dev->pdev->dev, "sq wqe index:%d,sq wqe cnt:%d\r\n",
			n, hr_qp->sq.wqe_cnt);
		return NULL;
	}

	return get_wqe(hr_qp, hr_qp->sq.offset + (n << hr_qp->sq.wqe_shift));
}

bool hns_roce_wq_overflow(struct hns_roce_wq *hr_wq, int nreq,
			  struct ib_cq *ib_cq)
{
	struct hns_roce_cq *hr_cq;
	u32 cur;

	cur = hr_wq->head - hr_wq->tail;
	if (likely(cur + nreq < hr_wq->max_post))
		return 0;

	hr_cq = to_hr_cq(ib_cq);
	spin_lock(&hr_cq->lock);
	cur = hr_wq->head - hr_wq->tail;
	spin_unlock(&hr_cq->lock);

	return cur + nreq >= hr_wq->max_post;
}

int hns_roce_init_qp_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	int reserved_from_top = 0;
	int ret;

	spin_lock_init(&qp_table->lock);
	INIT_RADIX_TREE(&hr_dev->qp_table_tree, GFP_ATOMIC);

	/* A port include two SQP, six port total 12 */
	ret = hns_roce_bitmap_init(&qp_table->bitmap, hr_dev->caps.num_qps,
				   hr_dev->caps.num_qps - 1,
				   hr_dev->caps.sqp_start + SQP_NUM,
				   reserved_from_top);
	if (ret) {
		dev_err(&hr_dev->pdev->dev, "Qp bitmap init failed(%d)!\n",
			ret);
		return ret;
	}

	return 0;
}

void hns_roce_cleanup_qp_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->qp_table.bitmap);
}
