/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_ROCE_EQ_H
#define _HNS_ROCE_EQ_H

#define HNS_ROCE_CEQ		1
#define HNS_ROCE_AEQ		2
#define HNS_ROCE_AEQ_OVF	3

#define	HNS_ROCE_CEQ_ENTRY_SIZE		0x4
#define	HNS_ROCE_AEQ_ENTRY_SIZE		0x10
#define	HNS_ROCE_CEQC_REG_OFFSET	0x18

#define HNS_ROCE_CEQ_DEFAULT_INTERVAL    0x10
#define HNS_ROCE_CEQ_DEFAULT_BURST_NUM   0x10

#define HNS_ROCE_ASYNC_EVENT_MASK  \
		((1ULL << HNS_ROCE_EVENT_TYPE_PATH_MIG)			| \
		(1ULL << HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_COMM_EST)			| \
		(1ULL << HNS_ROCE_EVENT_TYPE_SQ_DRAINED)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR)	| \
		(1ULL << HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR)	| \
		(1ULL << HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH)	| \
		(1ULL << HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_CQ_ID_INVALID)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_PORT_ACTIVE)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_PORT_CHANGE)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_LOCAL_CATAS_ERROR)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_PORT_ERROR)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_DB_OVERFLOW)		| \
		(1ULL << HNS_ROCE_EVENT_TYPE_MB)			| \
		(1ULL << HNS_ROCE_EVENT_TYPE_CEQ_OVERFLOW))

#define	HNS_ROCE_INT_MASK_DISABLE	0
#define	HNS_ROCE_INT_MASK_ENABLE	1

#define IRQ_NAMES_LEN			32
#define EQ_ENABLE			1
#define EQ_DISABLE			0
#define CONS_INDEX_MASK			0xffff

#define CEQ_REG_OFFSET			0x18

enum {
	HNS_ROCE_EQ_STAT_INVALID  = 0,
	HNS_ROCE_EQ_STAT_VALID    = 2,
	HNS_ROCE_EQ_STAT_ERR      = 3
};

struct hns_roce_aeqe {
	u32 asyn;
	union {
		struct {
			u32 qp;
			u32 rsv0;
			u32 rsv1;
		} qp_event;

		struct {
			u32 cq;
			u32 rsv0;
			u32 rsv1;
		} cq_event;

		struct {
			u32 port;
			u32 rsv0;
			u32 rsv1;
		} port_event;

		struct {
			u32 ceqe;
			u32 rsv0;
			u32 rsv1;
		} ce_event;

		struct {
			__le64  out_param;
			__le16  token;
			u8	status;
			u8	rsv0;
		} __packed cmd;
	 } event;
};
#define HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S 16
#define HNS_ROCE_AEQE_U32_4_EVENT_TYPE_M   \
	(((1UL << 8) - 1) << HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S)

#define HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S 24
#define HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M   \
	(((1UL << 7) - 1) << HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)

#define HNS_ROCE_AEQE_U32_4_OWNER_S 31

#define HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_S 0
#define HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_M   \
	(((1UL << 24) - 1) << HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_S)

#define HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_S 0
#define HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_M   \
	(((1UL << 16) - 1) << HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_S)

#define HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_S 0
#define HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_M   \
	(((1UL << 5) - 1) << HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_S)

struct hns_roce_ceqe {
	union {
		int		raw[1];
		int		comp;
	} ceqe;
};

#define HNS_ROCE_CEQE_CEQE_COMP_OWNER_S	0

#define HNS_ROCE_CEQE_CEQE_COMP_CQN_S 16
#define HNS_ROCE_CEQE_CEQE_COMP_CQN_M   \
	(((1UL << 16) - 1) << HNS_ROCE_CEQE_CEQE_COMP_CQN_S)

#endif /* _HNS_ROCE_EQ_H */
