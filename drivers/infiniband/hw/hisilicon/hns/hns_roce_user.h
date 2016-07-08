/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_ROCE_USER_H
#define _HNS_ROCE_USER_H

struct hns_roce_ib_create_cq {
	__u64   buf_addr;
	__u64   db_addr;
};

struct hns_roce_ib_create_qp {
	__u64	buf_addr;
	__u64	db_addr;
	__u8    log_sq_bb_count;
	__u8    log_sq_stride;
	__u8    sq_no_prefetch;
	__u8    reserved[5];
};

struct hns_roce_ib_alloc_ucontext_resp {
	__u32	qp_tab_size;
};

#endif /*_HNS_ROCE_USER_H */
