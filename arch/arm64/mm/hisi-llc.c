/*
 * Driver for Hisilicon Last Level Cache.
 *
 * Copyright (C) 2013-2014 Hisilicon Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include <linux/io.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/platform_data/hisi-djtag.h>

#include <asm/bitops.h>
#include <asm/cacheflush.h>
#include <asm/hisi-llc.h>

#define LLC_CTRL			(0x0)	/* control register */
#define LLC_AUCTRL			(0x4)	/* auxiliary control register */
#define LLC_EVENT_BUS_EN		BIT(24)

#define LLC_STATUS			(0x8)	/* status register */
#define LLC_PREFETCH			(0xc)	/* hardware prefetch configuration register */
#define LLC_ALLOC_FREQ			(0x10)	/* allocate frequce register */
#define LLC_INVLD			(0x14)	/* initialize register */
#define LLC_INITIALIZED			BIT(1)

/* LLC DAW registers */
#define MAX_DAW				(16)
/* hip05 llc */
#define LLC_DAW(n)			(0x0800 + 0x4 * (n))
#define SYSDAW_ADDR_MASK		(0xffff0000)
#define SYSDAW_SIZE_MASK		(0xf00)
#define SYSDAW_SIZE_SHIFT		(8)

/* hi1382 llc */
#define LLC_DAW_ADDR_EX(n)		(0x0800 + 0x10 * (n))
#define LLC_DAW_SIZE_ID_EX(n)		(0x0804 + 0x10 * (n))
#define SYSDAW_SIZE_MASK_EX		(0x1f0000)
#define SYSDAW_SIZE_SHIFT_EX		(16)

/* MNT operation regs */
#define LLC_MAINT_CTRL			(0x20)	/* maintain control register */
#define MNT_RANGE_SHIFT			(0x3)
#define MNT_RANGE_GLOBAL		(0x0 << MNT_RANGE_SHIFT)	/* do global maintain */
#define MNT_RANGE_AREA			(0x1 << MNT_RANGE_SHIFT)	/* do specified address area maintain */
#define MNT_TYPE_SHIFT			(0x1)
#define MNT_EN_SHIFT			(0x0)
#define MNT_EN				(0x1 << MNT_EN_SHIFT)

#define LLC_MAINT_START_H		(0x24)
#define LLC_MAINT_START_L		(0x28)
#define LLC_MAINT_AREA_H		(0x2c)
#define LLC_MAINT_AREA_L		(0x30)

#define LLC_SIZE			SZ_16M
#define LLC_BANK_NUM			4
#define LLC_CACHE_LINE_SIZE		SZ_64
#define LLC_CACHE_LINE_MASK		(LLC_CACHE_LINE_SIZE - 1)
#define IS_CACHE_LINE_ALIGN(x)		(!(x & LLC_CACHE_LINE_MASK))
#define CACHE_LINE_ALIGN_DW(x)		(x & ~LLC_CACHE_LINE_MASK)
#define CACHE_LINE_ALIGN_UP(x)		((x + LLC_CACHE_LINE_SIZE) & ~LLC_CACHE_LINE_MASK)

/*
 * for maintain and lockdown operations, physical address is divided into
 * two parts, and written to two registers.
 * low part is 32bits[6:37], high part is 6bits[38:43]
 */
/*
 *  |43  38|              6|       0|
 *  --------------------------------
 *  | high |      low      | offset |
 *  --------------------------------
 */
#define LLC_ADDR_LEN			44
#define LLC_LOW_ADDR_LEN		32
#define LLC_HIGH_ADDR_LEN		6
#define LLC_LINE_OFFSET			6
#define LLC_HIGH_ADDR_OFFSET		(LLC_ADDR_LEN - LLC_HIGH_ADDR_LEN)
#define LLC_LOW_ADDR_MASK		((1UL << LLC_LOW_ADDR_LEN) - 1)
#define LLC_HIGH_ADDR_MASK		((1UL << LLC_HIGH_ADDR_LEN) - 1)

#define MAX_DIE		8
/* each die has 2 port, but daw cfg is uncontrollable, so extend to 4*/
#define MAX_REGION	4

static LIST_HEAD(llc_list);

struct mem_region {
	phys_addr_t start;
	size_t size;
};

enum llc_type {
	HIP05_LLC,
	HI1382_LLC,
};

struct llc_of_data {
	struct list_head list;
	struct device_node *node;
	int dieid;
	struct mem_region region[MAX_REGION];
	int n_mem; /* mem_region number */
	enum llc_type type;
	const struct llc_cfg_ops *cfg_ops;
};

enum mnt_type {
	MNT_TYPE_CLEAN	= 0x1,
	MNT_TYPE_INV	= 0x2,
	MNT_TYPE_FLUSH	= 0x3,
};

static DEFINE_SPINLOCK(llc_maint_lock);

struct llc_cfg_ops {
	u32 (*read_one)(struct device_node *node, u32 offset,
				int bank);
	void (*write_one)(struct device_node *node, u32 offset,
				u32 value, int bank);
	void (*write_all)(struct device_node *node, u32 offset,
				u32 value);
	void (*setup_mem_map)(struct llc_of_data *llc_data);
};

#define for_each_mem_region(tmp, p, r)\
	list_for_each_entry_safe(tmp, p, &llc_list, list)\
		for (r = 0; r < tmp->n_mem; r++)

static inline int num_present_llcs(void)
{
	return !list_empty(&llc_list);
}

/**
 * get_addr_config - get low part and high part of given address or size,
 *		     used by maintain and lockdown/unlock
 * @a:	physical address or size
 * @l:	low part of address or size, bits[6:37]
 * @h:	high part of address or size, bits[38:43]
 */
#define get_addr_config(a, l, h) {	\
	l = ((a) >> LLC_LINE_OFFSET) & LLC_LOW_ADDR_MASK;	\
	h = ((a) >> LLC_HIGH_ADDR_OFFSET) & LLC_HIGH_ADDR_MASK; \
}

/**
 * llc_global_mnt - common help function for global maintain
 * @type:	maintian type - MNT_TYPE_{CLEAN|INV|FLUSH}
 */
static void llc_global_mnt(enum mnt_type type)
{
	struct llc_of_data *llc_data, *p;
	unsigned long flags;
	u32 val;
	int i;

	if (num_present_llcs() == 0) {
		pr_debug("No LLC present\n");
		return;
	}

	spin_lock_irqsave(&llc_maint_lock, flags);
	list_for_each_entry_safe(llc_data, p, &llc_list, list) {
		val = MNT_RANGE_GLOBAL | type << MNT_TYPE_SHIFT | MNT_EN;
		llc_data->cfg_ops->write_all(llc_data->node,
						LLC_MAINT_CTRL, val);

		/* wait LLC maintain operation to be completed */
		for (i = 0; i < LLC_BANK_NUM; i++)
			while (llc_data->cfg_ops->read_one(llc_data->node,
						LLC_MAINT_CTRL, i) & MNT_EN)
				cpu_relax();
	}
	spin_unlock_irqrestore(&llc_maint_lock, flags);
}

/**
 * llc_range_mnt - common help function for range maintain
 * @type:	maintian type-MAINT_TYPE_{CLEAN|INV|FLUSH}
 * @start:	start of maintain range
 * @size:	size of maintain range
 */
static void llc_range_mnt(enum mnt_type type, phys_addr_t start, size_t size)
{
	phys_addr_t rstart, rend, end;
	u32 saddr_h, saddr_l, size_h, size_l, val;
	unsigned long flags;
	struct llc_of_data *llc_data, *p;
	int r, i;

	if (num_present_llcs() == 0) {
		pr_debug("No LLC present.\n");
		return;
	}

	if (!size)
		return;

	spin_lock_irqsave(&llc_maint_lock, flags);

	end = start + size;

	for_each_mem_region(llc_data, p, r) {
		rstart = llc_data->region[r].start;
		rend = rstart + llc_data->region[r].size;

		/* not overlap */
		if (start >= rend || end <= rstart)
			continue;

		rstart = max(start, rstart);
		rend = min(rend, end);

		/* get low and high part of start address */
		get_addr_config(rstart, saddr_l, saddr_h);
		/* get low and high part of size */
		get_addr_config(rend - rstart - 1, size_l, size_h);

		/* write address and size to the correspond register */
		llc_data->cfg_ops->write_all(llc_data->node,
					LLC_MAINT_START_L, saddr_l);
		llc_data->cfg_ops->write_all(llc_data->node,
					LLC_MAINT_START_H, saddr_h);
		llc_data->cfg_ops->write_all(llc_data->node,
					LLC_MAINT_AREA_L, size_l);
		llc_data->cfg_ops->write_all(llc_data->node,
					LLC_MAINT_AREA_H, size_h);

		val = MNT_RANGE_AREA | type << MNT_TYPE_SHIFT | MNT_EN;

		llc_data->cfg_ops->write_all(llc_data->node,
						LLC_MAINT_CTRL, val);

		/* wait LLC maintain operation to be completed */
		for (i = 0; i < LLC_BANK_NUM; i++)
			while (llc_data->cfg_ops->read_one(llc_data->node,
						LLC_MAINT_CTRL, i) & MNT_EN)
				cpu_relax();

		if (rend == end)
			break;

		/* given range covers more than one region */
		start = rend;
	}

	spin_unlock_irqrestore(&llc_maint_lock, flags);
}

/**
 * llc_clean_all - write all cachelines to main memory if the line
 * 		   is valid and dirty
 */
void llc_clean_all(void)
{
	llc_global_mnt(MNT_TYPE_CLEAN);
}
EXPORT_SYMBOL_GPL(llc_clean_all);

/**
 * llc_inv_all - invalidate all cachelines including dirty line.
 */
void llc_inv_all(void)
{
	llc_global_mnt(MNT_TYPE_INV);
}
EXPORT_SYMBOL_GPL(llc_inv_all);

/**
 * llc_flush_all - write all cachelines to main memory if the line is
 * 		   valid and dirty, then mark all cachelines as not valid.
 */
void llc_flush_all(void)
{
	llc_global_mnt(MNT_TYPE_FLUSH);
}
EXPORT_SYMBOL_GPL(llc_flush_all);

/**
 * llc_clean_range - write cachelines in [start, start + size] to main memory
 * 		     if the line is valid and dirty
 * @start:	range's start physical address
 * @size:	range's size, if size is 0, will clean the cacheline which
 *        	@start belongs
 */
void llc_clean_range(phys_addr_t start, size_t size)
{
	llc_range_mnt(MNT_TYPE_CLEAN, start, size);
}
EXPORT_SYMBOL_GPL(llc_clean_range);

/**
 * llc_inv_range - mark cachelines in [start, start + size] not valid,
 * 		   including dirty lines
 * @start:	range's start physical address
 * @size:	range's size, if size is 0, will invalidate the cacheline which
 *		@start belongs
 */
void llc_inv_range(phys_addr_t start, size_t size)
{
	llc_range_mnt(MNT_TYPE_INV, start, size);
}
EXPORT_SYMBOL_GPL(llc_inv_range);

/**
 * llc_flush_range - write cachelines in [start, start + size] to main memory
 * 		     if the line is valid and dirty, and mark them as not valid
 * @start:	range's start physical address
 * @size:	range's size, if size is 0, will flush the cacheline which
 *		@start belongs
 */
void llc_flush_range(phys_addr_t start, size_t size)
{
	llc_range_mnt(MNT_TYPE_FLUSH, start, size);
}
EXPORT_SYMBOL_GPL(llc_flush_range);

/**
 * memory_region_insert - insert new memory region
 * @n:		die id
 * @idx:	index for the insertion point
 * @start:	start address of the new region
 * @size:	size of the new region
 *
 * Insert new memory region [@start,@start+@size) at @idx.
 */
static void memory_region_insert(struct llc_of_data *llc_data,
					int idx, phys_addr_t start,
						size_t size)
{
	struct mem_region *rgn = &llc_data->region[idx];

	memmove(rgn + 1, rgn, (llc_data->n_mem - idx) * sizeof(*rgn));
	rgn->start = start;
	rgn->size = size;
	llc_data->n_mem++;
}

/**
 * memory_region_add - add new memory region
 * @n:		CPU DIE ID the memory region belong to
 * @start:	start address of the new region
 * @size:	size of the new region
 */
static void mem_region_add(struct llc_of_data *llc_data,
				phys_addr_t start, size_t size)
{
	phys_addr_t end = start + size;
	int i;

	if (end <= start)
		return;

	pr_info("mem add: Die%d start:0x%llx, size:0x%lx\n",
				llc_data->dieid, start, size);
	/* special case for first region */
	if (llc_data->n_mem == 0) {
		llc_data->region[0].start = start;
		llc_data->region[0].size = size;
		llc_data->n_mem++;
		return;
	}

	for (i = 0; i < llc_data->n_mem; i++) {
		struct mem_region *rgn = &llc_data->region[i];
		phys_addr_t rstart = rgn->start;
		phys_addr_t rend = rstart + rgn->size;

		if (rstart >= end)
			break;
		if (rend <= start)
			continue;

		if (rstart > start)
			memory_region_insert(llc_data, i++,
						start, rstart - start);

		start = min(rend, end);
	}

	if (start < end)
		memory_region_insert(llc_data, i, start, end - start);

	/* merge neighboring compatible regions */
	i = 0;
	while (i < llc_data->n_mem - 1) {
		struct mem_region *this = &llc_data->region[i];
		struct mem_region *next = &llc_data->region[i + 1];

		if (this->start + this->size != next->start) {
			i++;
			continue;
		}

		this->size += next->size;
		memmove(next, next + 1, ((llc_data->n_mem
				- (i + 2)) * sizeof(*next)));
		llc_data->n_mem--;
	}
}

static void llc_mem_map_show(struct llc_of_data *llc_data)
{
	phys_addr_t start;
	size_t size;
	int r;

	for (r = 0; r < llc_data->n_mem; r++) {
		start = llc_data->region[r].start;
		size = llc_data->region[r].size;
		pr_info("LLC: mem [0x%llx, 0x%llx) @Die%d @region%d\n",
				start, start + size, llc_data->dieid, r);
	}
}

/**
 * one die one LLC, each LLC has 16 DAWs. all die should have same DAW.
 * so, find the first existing and available LLC, get mem region from it.
 */
static void hip05_setup_mem_map(struct llc_of_data *llc_data)
{
	u32 val;
	int i, die_id;
	phys_addr_t start;
	size_t	sz;

	for (i = 0; i < MAX_DAW; i++) {
		val = llc_data->cfg_ops->read_one(llc_data->node,
							LLC_DAW(i), 0);
		pr_debug("DAW %d\n", i);
		pr_debug("value:0x%x, sysdawid: 0x%x, start:0x%x, size:0x%x\n",
				val, val & 0x7f, (val & SYSDAW_ADDR_MASK) >> 16,
				(val & SYSDAW_SIZE_MASK) >> SYSDAW_SIZE_SHIFT);
		/*
		 * sysdaw_id: {SocketID, DieID, PortID}={[6],[5:4],[3:0]}
		 * final DIE ID = {SocketID,DieID};
		 */
		switch (val & 0x3f) {
			case 0x1C:
			case 0x1D:
			case 0x2C:
			case 0x2D:
			case 0x3C:
			case 0x3D:
				die_id = (val & 0x70) >> 4;
				break;
			default:
				continue;
		}

		if (die_id == llc_data->dieid) {
			/*fill [43:28] bit of 44bit physical addr*/
			start = ((phys_addr_t)val & SYSDAW_ADDR_MASK) << 12;
			/*begin at 256M, MAX 8TB*/
			val = (val & SYSDAW_SIZE_MASK) >> SYSDAW_SIZE_SHIFT;
			sz = (size_t)SZ_256M << val;
			pr_debug("mem: Die%d start:0x%llx, size:0x%lx\n",
					die_id, start, sz);

			mem_region_add(llc_data, start, sz);
		}
	}
	llc_mem_map_show(llc_data);
}

static void hi1382_setup_mem_map(struct llc_of_data *llc_data)
{
	u32 v_addr, v_size;
	int i, die_id;
	phys_addr_t start;
	size_t	sz;

	for (i = 0; i < MAX_DAW; i++) {
		v_addr = llc_data->cfg_ops->read_one(llc_data->node,
				LLC_DAW_ADDR_EX(i), 0);
		v_size = llc_data->cfg_ops->read_one(llc_data->node,
				LLC_DAW_SIZE_ID_EX(i), 0);
		pr_debug("DAW %d\n", i);
		pr_debug("value(addr/size):0x%x/0x%x, sysdawid: 0x%x, \
				start:0x%x, size:0x%x\n", v_addr, v_size,
				v_size & 0xff, v_addr,
				(v_size & SYSDAW_SIZE_MASK_EX)
				>> SYSDAW_SIZE_SHIFT_EX);

		/*
		 * sysdaw_id: {SocketID, DieID, PortID}={[7:6],[5:4],[3:0]}
		 * final DIE ID = {SocketID,DieID};
		 */
		switch (v_size & 0xff) {
			case 0x2C:
			case 0x2D:
				die_id = (v_size & 0xf0) >> 4;
				break;
			default:
				continue;
		}

		if (die_id == llc_data->dieid) {
			/*fill [43:24] bit of 44bit physical addr*/
			start = (phys_addr_t)v_addr << 24;
			/*begin at 16M, MAX 8TB*/
			v_size = (v_size & SYSDAW_SIZE_MASK_EX)
					>> SYSDAW_SIZE_SHIFT_EX;
			sz = (size_t)SZ_16M << v_size;
			pr_debug("mem: Die%d start:0x%llx, size:0x%lx\n",
					die_id, start, sz);

			mem_region_add(llc_data, start, sz);
		}
	}
	llc_mem_map_show(llc_data);
}

static const int hip05_llc_id_convert[LLC_BANK_NUM] = {1, 2, 0, 3};
static const int hi1382_llc_id_convert[LLC_BANK_NUM] = {1, 2, 3, 4};

static u32 hip05_read_one(struct device_node *node, u32 offset, int bank)
{
	u32 val;
	int ret, chain_id;

	chain_id = hip05_llc_id_convert[bank];

	ret = djtag_readl(node, offset, 0x4, chain_id, &val);
	if (ret) {
		pr_info("%s is not correct!\n", node->full_name);
		return 0;
	} else
		return val;

};

static void hip05_write_one(struct device_node *node, u32 offset, u32 val, int bank)
{
	int ret;
	u32 mod_mask;

	mod_mask = 0x1 << hip05_llc_id_convert[bank];
	ret = djtag_writel(node, offset, 0x4, mod_mask, val);

	if (ret)
		pr_info("%s is not correct!\n", node->full_name);
};

static void hip05_write_all(struct device_node *node, u32 offset, u32 val)
{
	int ret;

	ret = djtag_writel(node, offset, 0x4, 0, val);

	if (ret)
		pr_info("%s is not correct!\n", node->full_name);
};

static u32 hi1382_read_one(struct device_node *node, u32 offset, int bank)
{
	u32 val;
	int ret, mod_id;

	mod_id = hi1382_llc_id_convert[bank];
	ret = djtag_readl(node, offset, mod_id, 0, &val);
	if (ret) {
		pr_info("%s is not correct!\n", node->full_name);
		return 0;
	} else
		return val;

};

static void hi1382_write_one(struct device_node *node, u32 offset, u32 val, int bank)
{
	int ret, mod_id;

	mod_id = hi1382_llc_id_convert[bank];
	ret = djtag_writel(node, offset, mod_id, 0, val);

	if (ret)
		pr_info("%s is not correct!\n", node->full_name);
};

static void hi1382_write_all(struct device_node *node, u32 offset, u32 val)
{
	int ret, mod_id, i;

	ret = 0;
	for (i = 0; i < LLC_BANK_NUM; i++) {
		mod_id = hi1382_llc_id_convert[i];
		ret += djtag_writel(node, offset, mod_id, 0, val);
	}
	if (ret)
		pr_info("%s is not correct!\n", node->full_name);
};

static const struct llc_cfg_ops hip05_cfg_ops = {
	.read_one	= hip05_read_one,
	.write_one	= hip05_write_one,
	.write_all	= hip05_write_all,
	.setup_mem_map	= hip05_setup_mem_map,
};

static const struct llc_cfg_ops hi1382_cfg_ops = {
	.read_one	= hi1382_read_one,
	.write_one	= hi1382_write_one,
	.write_all	= hi1382_write_all,
	.setup_mem_map	= hi1382_setup_mem_map,
};

static struct of_device_id llc_of_match[] = {
	{ .compatible = "hisilicon,hip05-llc", .data = (void *)HIP05_LLC },
	{ .compatible = "hisilicon,hi1382-llc", .data = (void *)HI1382_LLC },
	{},
};

MODULE_DEVICE_TABLE(of, llc_of_match);

static int llc_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct of_phandle_args arg;
	struct llc_of_data *llc_data;
	const struct of_device_id *of_id;
	int ret;
	u32 val;

	of_id = of_match_device(llc_of_match, dev);
	if (!of_id)
		return -EINVAL;

	ret = of_parse_phandle_with_fixed_args(node,
			"djtag", 1, 0, &arg);
	if (ret) {
		pr_err("LLC: %s set error.\n", node->full_name);
		return -EINVAL;
	}

	llc_data = kzalloc(sizeof(struct llc_of_data),
			GFP_KERNEL);
	if (llc_data == NULL)
		return -ENOMEM;

	if (arg.args[0] > 0 && arg.args[0] < 9)
		llc_data->dieid = arg.args[0];
	llc_data->node = arg.np;
	llc_data->type = (enum llc_type)of_id->data;
	if (llc_data->type == HIP05_LLC)
		llc_data->cfg_ops = &hip05_cfg_ops;
	else if (llc_data->type == HI1382_LLC)
		llc_data->cfg_ops = &hi1382_cfg_ops;
	else
		return -EINVAL;

	INIT_LIST_HEAD(&llc_data->list);
	list_add_tail(&llc_data->list, &llc_list);

	val = llc_data->cfg_ops->read_one(llc_data->node,
						LLC_INVLD, 0);
	if (val & LLC_INITIALIZED) {
		pr_info("LLC: %s is initialized.\n",
					node->full_name);
	} else {
		list_del(&llc_data->list);
		pr_err("LLC: %s isn't initialized.\n",
					node->full_name);
		kfree(llc_data);
		llc_data = NULL;
		return -EINVAL;
	}

	llc_data->cfg_ops->setup_mem_map(llc_data);
	return 0;
}

static int llc_dev_remove(struct platform_device *pdev)
{
	struct llc_of_data *tmp, *p;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	list_for_each_entry_safe(tmp, p, &llc_list, list) {
		list_del(&tmp->list);
		pr_info("LLC: %s remove successfully.\n",
					node->full_name);
		kfree(tmp);
	}

	return 0;
}

static struct platform_driver llc_dev_driver = {
	.driver = {
		.name = "hip05-llc",
		.of_match_table = llc_of_match,
	},
	.probe = llc_dev_probe,
	.remove = llc_dev_remove,
};

module_platform_driver(llc_dev_driver);

MODULE_DESCRIPTION("Hisilicon Hip05 LLC driver");
MODULE_AUTHOR("Kefeng Wang & Xiaojun Tan");
MODULE_LICENSE("GPL");
MODULE_VERSION("V1R1");
