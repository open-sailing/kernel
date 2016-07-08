/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_ROCE_CMD_H
#define _HNS_ROCE_CMD_H

#include <linux/dma-mapping.h>

enum {
	/* TPT commands */
	HNS_ROCE_CMD_SW2HW_MPT		= 0xd,
	HNS_ROCE_CMD_HW2SW_MPT		= 0xf,

	/* CQ commands */
	HNS_ROCE_CMD_SW2HW_CQ		= 0x16,
	HNS_ROCE_CMD_HW2SW_CQ		= 0x17,

	/* QP/EE commands */
	HNS_ROCE_CMD_RST2INIT_QP	= 0x19,
	HNS_ROCE_CMD_INIT2RTR_QP	= 0x1a,
	HNS_ROCE_CMD_RTR2RTS_QP		= 0x1b,
	HNS_ROCE_CMD_RTS2RTS_QP		= 0x1c,
	HNS_ROCE_CMD_2ERR_QP		= 0x1e,
	HNS_ROCE_CMD_RTS2SQD_QP		= 0x1f,
	HNS_ROCE_CMD_SQD2SQD_QP		= 0x38,
	HNS_ROCE_CMD_SQD2RTS_QP		= 0x20,
	HNS_ROCE_CMD_2RST_QP		= 0x21,
	HNS_ROCE_CMD_QUERY_QP		= 0x22,
};

enum {
	HNS_ROCE_CMD_TIME_CLASS_A	= 10000,
	HNS_ROCE_CMD_TIME_CLASS_B	= 10000,
	HNS_ROCE_CMD_TIME_CLASS_C	= 10000,
};

enum {
	HNS_ROCE_MAILBOX_SIZE		=  4096
};

struct hns_roce_cmd_mailbox {
	void		       *buf;
	dma_addr_t		dma;
};

int __hns_roce_cmd(struct hns_roce_dev *hr_dev, u64 in_param, u64 *out_param,
		   int out_is_imm, u32 in_modifier, u8 op_modifier, u16 op,
		   unsigned long timeout);

/* Invoke a command with no output parameter */
static inline int hns_roce_cmd(struct hns_roce_dev *hr_dev, u64 in_param,
			       u32 in_modifier, u8 op_modifier, u16 op,
			       unsigned long timeout)
{
	return __hns_roce_cmd(hr_dev, in_param, NULL, 0, in_modifier,
			      op_modifier, op, timeout);
}

/* Invoke a command with an output mailbox */
static inline int hns_roce_cmd_box(struct hns_roce_dev *hr_dev, u64 in_param,
				   u64 out_param, u32 in_modifier,
				   u8 op_modifier, u16 op,
				   unsigned long timeout)
{
	return __hns_roce_cmd(hr_dev, in_param, &out_param, 0, in_modifier,
			      op_modifier, op, timeout);
}

struct hns_roce_cmd_mailbox
	*hns_roce_alloc_cmd_mailbox(struct hns_roce_dev *hr_dev);
void hns_roce_free_cmd_mailbox(struct hns_roce_dev *hr_dev,
			       struct hns_roce_cmd_mailbox *mailbox);

#endif /* _HNS_ROCE_CMD_H */
