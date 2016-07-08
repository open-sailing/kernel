/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_ROCE_ICM_H
#define _HNS_ROCE_ICM_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>

enum {
	/* MAP ICM */
	ICM_TYPE_QPC = 0,
	ICM_TYPE_MTPT,
	ICM_TYPE_CQC,
	ICM_TYPE_SRQC,

	/* UNMAP ICM */
	ICM_TYPE_MTT,
	ICM_TYPE_IRRL,
};

#define HNS_ROCE_ICM_CHUNK_LEN	\
	((256 - sizeof(struct list_head) - 2 * sizeof(int)) /	\
	(sizeof(struct scatterlist)))

enum {
	HNS_ROCE_ICM_PAGE_SHIFT	= 12,
	HNS_ROCE_ICM_PAGE_SIZE	= 1 << HNS_ROCE_ICM_PAGE_SHIFT,
};

struct hns_roce_icm_chunk {
	struct list_head	list;
	int			npages;
	int			nsg;
	struct scatterlist	mem[HNS_ROCE_ICM_CHUNK_LEN];
};

struct hns_roce_icm {
	struct list_head	chunk_list;
	int			refcount;
};

struct hns_roce_icm_iter {
	struct hns_roce_icm	        *icm;
	struct hns_roce_icm_chunk	*chunk;
	int				page_idx;
};

void hns_roce_free_icm(struct hns_roce_dev *hr_dev,
		       struct hns_roce_icm *icm, int coherent);
int hns_roce_table_get(struct hns_roce_dev *hr_dev,
		       struct hns_roce_icm_table *table, int obj);
void hns_roce_table_put(struct hns_roce_dev *hr_dev,
			struct hns_roce_icm_table *table, int obj);
void *hns_roce_table_find(struct hns_roce_icm_table *table, int obj,
			  dma_addr_t *dma_handle);
int hns_roce_table_get_range(struct hns_roce_dev *hr_dev,
			     struct hns_roce_icm_table *table, int start,
			     int end);
void hns_roce_table_put_range(struct hns_roce_dev *hr_dev,
			      struct hns_roce_icm_table *table, int start,
			      int end);
int hns_roce_init_icm_table(struct hns_roce_dev *hr_dev,
			    struct hns_roce_icm_table *table, u32 type,
			    int obj_size, int nobj, int reserved,
			    int use_lowmem, int use_coherent);
void hns_roce_cleanup_icm_table(struct hns_roce_dev *hr_dev,
				struct hns_roce_icm_table *table);
void hns_roce_cleanup_icm(struct hns_roce_dev *hr_dev);

static inline void hns_roce_icm_first(struct hns_roce_icm *icm,
				      struct hns_roce_icm_iter *iter)
{
	iter->icm = icm;
	iter->chunk = list_empty(&icm->chunk_list) ? NULL :
				list_entry(icm->chunk_list.next,
					   struct hns_roce_icm_chunk, list);
	iter->page_idx = 0;
}

static inline int hns_roce_icm_last(struct hns_roce_icm_iter *iter)
{
	return !iter->chunk;
}

static inline void hns_roce_icm_next(struct hns_roce_icm_iter *iter)
{
	if (++iter->page_idx >= iter->chunk->nsg) {
		if (iter->chunk->list.next == &iter->icm->chunk_list) {
			iter->chunk = NULL;
			return;
		}

		iter->chunk = list_entry(iter->chunk->list.next,
					 struct hns_roce_icm_chunk, list);
		iter->page_idx = 0;
	}
}

static inline dma_addr_t hns_roce_icm_addr(struct hns_roce_icm_iter *iter)
{
	return sg_dma_address(&iter->chunk->mem[iter->page_idx]);
}

#endif /*_HNS_ROCE_ICM_H*/
