
/*
 * nand controller driver for Hisilicon Hip05 SoCs
 *
 * Copyright (C) 2015 Hisilicon Co., Ltd. http://www.hisilicon.com
 *
 * Author: Jukuo Zhang <zhangjukuo@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>	/* hweight64() */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/of_mtd.h>
#include <linux/platform_device.h>

#define NFC_CHIP_DELAY_25 10000
#define NFC_CHIP_SEL 8
#define NFC_MAXCHIPS 1

#define NFC_STATUS 0x20

/* NFC_CON */
#define NFC_CON 0x0
#define NFC_CON_ECC_TYPE_MASK (7U << 9)
#define NFC_CON_PAGESIZE_MASK (7U << 1)
#define NFC_CON_RANDOMIZER_EN (1U << 13)
#define NFC_CON_NF_MODE_ONFI3 (3U << 14)
#define NFC_CON_NF_MODE_TOGGLE (1U << 14)
#define NFC_CON_NF_MODE_NONE (0U << 14)
#define NFC_CON_ECCTYPE_SHIFT 9
#define NFC_CON_RANDOMIZER_SHIFT 13
#define NFC_CON_NF_CS_SHIFT 7

/* NFC_CON_PAGESIZE */
#define NFC_CON_PAGESIZE_SHIFT 1
#define NFC_CON_PAGESIZE_BITS_2K 1
#define NFC_CON_PAGESIZE_BITS_4K 2
#define NFC_CON_PAGESIZE_BITS_8K 3
#define NFC_CON_PAGESIZE_BITS_16K 4

#define NFC_CMD 0xC
#define NFC_ADDRL 0x10
#define NFC_ADDRH 0x14
#define NFC_DATA_NUM 0x18

/* NFC_OP */
#define NFC_OP  0x1C
#define NFC_OP_READ_STATUS_EN   BIT(0)
#define NFC_OP_READ_DATA_EN     BIT(1)
#define NFC_OP_WAIT_READY_EN    BIT(2)
#define NFC_OP_CMD2_EN      BIT(3)
#define NFC_OP_WRITE_DATA_EN    BIT(4)
#define NFC_OP_ADDR_EN      BIT(5)
#define NFC_OP_CMD1_EN      BIT(6)
#define NFC_OP_READ_ID_EN   BIT(14)
#define NFC_OP_RW_REG_EN    BIT(15)

/* NFC_OP_ADDRCYCLE */
#define NFC_OP_ADDR_CYCLES_MASK     (7 << 11)
#define NFC_OP_ADDR_CYCLES_SHIFT    11
#define NFC_OP_ADDR_CYCLE_1     1
#define NFC_OP_ADDR_CYCLE_2     2
#define NFC_OP_ADDR_CYCLE_3     3
#define NFC_OP_ADDR_CYCLE_4     4
#define NFC_OP_ADDR_CYCLE_5     5

/* NFC_OP_CTRL */
#define NFC_OP_CTRL     0xFC
#define NFC_OP_CTRL_OP_READY    BIT(0)
#define NFC_OP_CTRL_DMA_OP      BIT(4)
#define NFC_OP_CTRL_RD_OOB_SEL  BIT(5)

/* NFC_OP_CTRL_CMDTYPE */
#define NFC_OP_CTRL_CMD_TYPE_MASK   (0x7 << 1)
#define NFC_OP_CTRL_CMD_TYPE_SHIFT  1
#define NFC_OP_CTRL_CMD_TYPE_READ   0
#define NFC_OP_CTRL_CMD_TYPE_PROGRAM    1
#define NFC_OP_CTRL_CMD_TYPE_TWO_PLANE_PROGRAM  2
#define NFC_OP_CTRL_CMD_TYPE_ERASE  3
#define NFC_OP_CTRL_CMD_TYPE_TWO_PLANE_ERASE    4

#define NFC_PLN0_ADDRL  0x180
#define NFC_PLN0_ADDRH  0x184
#define NFC_PLN1_ADDRL  0x188
#define NFC_PLN1_ADDRH  0x18C
#define NFC_PLN0_BADDRL_D0  0x410
#define NFC_PLN0_BADDRH_D0  0x414
#define NFC_PLN0_BADDRL_OOB     0x450
#define NFC_PLN0_BADDRH_OOB     0x454
#define NFC_PLN0_BADDRL_D1  0x420
#define NFC_PLN0_BADDRH_D1  0x424
#define NFC_PLN0_BADDRL_D2  0x430
#define NFC_PLN0_BADDRH_D2  0x434
#define NFC_PLN0_BADDRL_D3  0x440
#define NFC_PLN0_BADDRH_D3  0x444

#define NFC_OP_MODE_REG     0
#define NFC_OP_MODE_DMA     1

#define PAGESIZE_CONST_2K   2048
#define PAGESIZE_CONST_4K   4096
#define PAGESIZE_CONST_8K   8192
#define PAGESIZE_CONST_16K  16384

#define MAX_ID_LEN 8
#define NFC_DATA_NUM_MASK 0x3FFF

#define to_hisi_nfc(x)	container_of(x, struct hisi_nfc, mtd)

struct hisi_nfc {
	struct nand_chip chip;
	struct mtd_info mtd;
	struct device *dev;
	void __iomem *iobase;
	void __iomem *mmio;
	u8 *buffer;
	u32 offset;
	u32 page_addr;
	u32 chip_set;
	u32 command;
	dma_addr_t dma_buffer;
};

static u32 hinfc_read(struct hisi_nfc *host, unsigned reg)
{
	return readl(host->iobase + reg);
}

static void hinfc_write(struct hisi_nfc *host, unsigned value, unsigned reg)
{
	writel(value, host->iobase + reg);
}

static void hinfc_set_pagesize(struct hisi_nfc *host, u32 pagesize)
{
	u32 val;

	val = hinfc_read(host, NFC_CON);

	val &= ~NFC_CON_PAGESIZE_MASK;
	val |= (pagesize << NFC_CON_PAGESIZE_SHIFT) & NFC_CON_PAGESIZE_MASK;

	hinfc_write(host, val, NFC_CON);
}

static void hinfc_ecc_config(struct hisi_nfc *host, unsigned ecc_mode,
			     unsigned randomizer_en)
{
	u32 reg;

	reg = hinfc_read(host, NFC_CON);

	reg &= ~(NFC_CON_ECC_TYPE_MASK | NFC_CON_RANDOMIZER_EN);
	reg |= ((NFC_CON_ECC_TYPE_MASK & (ecc_mode << NFC_CON_ECCTYPE_SHIFT))
		| (NFC_CON_RANDOMIZER_EN &
		   (randomizer_en << NFC_CON_RANDOMIZER_SHIFT)));

	hinfc_write(host, reg, NFC_CON);
}

static void hinfc_ecc_disabled(struct hisi_nfc *host)
{
	u32 val;

	val = hinfc_read(host, NFC_CON);
	val &= ~(NFC_CON_ECC_TYPE_MASK | NFC_CON_RANDOMIZER_EN);
	hinfc_write(host, val, NFC_CON);
}

static int hinfc_wait_ready(struct hisi_nfc *host)
{
	u32 timeout = 0;
	u32 status;
	u32 nf_ready_mask;

	nf_ready_mask = (1 << (host->chip_set + 1));
	udelay(1);
	status = hinfc_read(host, NFC_STATUS);

	while (((status & nf_ready_mask) == 0)
	       && (timeout <= host->chip.chip_delay)) {
		udelay(1);
		timeout++;
		status = hinfc_read(host, NFC_STATUS);
	}

	if (timeout > host->chip.chip_delay)
		return -EIO;

	return 0;
}

static void hinfc_readID(struct hisi_nfc *host)
{
	u32 op;
	u32 con_offset;
	u32 nf_mode_mask;
	u32 nf_mode;
	u32 reg;
	u32 addr_cycle;

	hinfc_ecc_disabled(host);

	/* Send the command for reading device ID */
	hinfc_write(host, 0, NFC_ADDRL);
	hinfc_write(host, 0, NFC_ADDRH);
	hinfc_write(host, MAX_ID_LEN, NFC_DATA_NUM);
	hinfc_write(host, NAND_CMD_READID, NFC_CMD);
	con_offset = NFC_CON;
	nf_mode_mask = NFC_CON_NF_MODE_ONFI3;
	nf_mode = NFC_CON_NF_MODE_TOGGLE;
	addr_cycle = NFC_OP_ADDR_CYCLE_1;

	reg = hinfc_read(host, con_offset);

	if ((reg & nf_mode_mask) == nf_mode) {
		op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
		    | NFC_OP_RW_REG_EN
		    | NFC_OP_READ_ID_EN
		    | NFC_OP_ADDR_EN
		    | NFC_OP_CMD1_EN
		    | NFC_OP_WAIT_READY_EN
		    | NFC_OP_READ_DATA_EN
		    | (addr_cycle << NFC_OP_ADDR_CYCLES_SHIFT);
	} else {
		op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
		    | NFC_OP_ADDR_EN
		    | NFC_OP_CMD1_EN
		    | NFC_OP_READ_DATA_EN
		    | NFC_OP_WAIT_READY_EN
		    | (addr_cycle << NFC_OP_ADDR_CYCLES_SHIFT);
	}

	hinfc_write(host, op, NFC_OP);

	if (hinfc_wait_ready(host))
		dev_err(host->dev, "wait failed!\n");
}

static void hinfc_reset_flash(struct hisi_nfc *host)
{
	u32 op;

	hinfc_write(host, NAND_CMD_RESET, NFC_CMD);

	op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
	    | NFC_OP_WAIT_READY_EN | NFC_OP_CMD1_EN;

	hinfc_write(host, op, NFC_OP);

	if (hinfc_wait_ready(host))
		dev_err(host->dev, "wait failed!\n");
}

static int hinfc_read_flash_status(struct hisi_nfc *host)
{
	u32 op;

	/* disable ecc when reading id,reading status and sending chip cmd */
	hinfc_ecc_disabled(host);

	/* write cmd with 0x70 */
	hinfc_write(host, NAND_CMD_STATUS, NFC_CMD);

	op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
	    | NFC_OP_CMD1_EN | NFC_OP_WAIT_READY_EN | NFC_OP_READ_DATA_EN;

	hinfc_write(host, op, NFC_OP);

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait failed!\n");
		return -EIO;
	}

	hinfc_ecc_config(host, host->chip.ecc.mode, 1);

	return 0;
}

static
u64 hinfc_get_erase_op_addr(struct hisi_nfc *host, u64 block_num, u64 page_num)
{
	u64 lun;
	u64 op_addr;
	u64 op_pageaddr, op_blockaddr, op_lunaddr;
	u64 block_num_per_lun, chip_lun_num, pages_per_block;

    /* here I just use one lune, so chip_lun_num setted to 1 directly */
	chip_lun_num = 1;

	block_num_per_lun = host->mtd.size / host->mtd.erasesize;
	pages_per_block = host->mtd.erasesize / host->mtd.writesize;
	lun = (block_num / block_num_per_lun) & (chip_lun_num - 1);

	if (lun == 1)
		block_num = block_num % block_num_per_lun;

	op_pageaddr = page_num & (pages_per_block - 1);
	op_blockaddr =
		(block_num & (block_num_per_lun - 1)) <<
			hweight64(pages_per_block - 1);
	op_lunaddr =
		lun << (hweight64(pages_per_block - 1) +
			hweight64(block_num_per_lun - 1));

	op_addr = op_pageaddr | op_blockaddr | op_lunaddr;

	return op_addr;
}

static
u64 hinfc_get_op_addr(struct hisi_nfc *host, u64 block_num, u64 page_num)
{
	u64 op_ca;
	u64 lun;
	u64 op_addr;
	u64 op_pa, op_ba, op_la;
	u64 block_num_per_lun, chip_lun_num, pages_per_block;

    /* here I just use one lune, so chip_lun_num setted to 1 directly */
	chip_lun_num = 1;

	block_num_per_lun = host->mtd.size / host->mtd.erasesize;
	pages_per_block = host->mtd.erasesize / host->mtd.writesize;
	lun = (block_num / block_num_per_lun) & (chip_lun_num - 1);

	if (lun == 1)
		block_num = block_num % block_num_per_lun;

	op_ca = 0;
	op_pa = (page_num & (pages_per_block - 1)) << 16;
	op_ba = (block_num % block_num_per_lun) <<
		(16 + hweight64(pages_per_block - 1));

	op_la = lun << (16 + hweight64(pages_per_block - 1)
			+ hweight64(block_num_per_lun - 1));

	op_addr = op_ca | op_pa | op_ba | op_la;

	return op_addr;
}

static
void hinfc_set_op_addr(struct hisi_nfc *host, u64 op_addr, u32 mode, u32 len)
{
	u32 addrl, addrh;

	if (mode == NFC_OP_MODE_REG) {
		addrl = NFC_ADDRL;
		addrh = NFC_ADDRH;
	} else {
		addrl = NFC_PLN0_ADDRL;
		addrh = NFC_PLN0_ADDRH;
	}

	hinfc_write(host, op_addr & 0xffffffff, addrl);
	hinfc_write(host, (op_addr >> 32) & 0xff, addrh);

	if (mode == NFC_OP_MODE_REG)
		hinfc_write(host, len & NFC_DATA_NUM_MASK, NFC_DATA_NUM);
}

int hinfc_erase(struct hisi_nfc *host, u32 block_num, u32 mode)
{
	u32 reg_offset;
	u32 op;
	u64 op_addr;
	u32 addr_cycle;

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait ready failed!\n");
		return -EIO;
	}

	op_addr = hinfc_get_erase_op_addr(host, block_num, 0);
	hinfc_set_op_addr(host, op_addr, mode, host->mtd.writesize);
	addr_cycle = NFC_OP_ADDR_CYCLE_3;

	if (mode == NFC_OP_MODE_REG) {
		op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
		    | NFC_OP_READ_STATUS_EN
		    | NFC_OP_WAIT_READY_EN
		    | NFC_OP_CMD2_EN
		    | NFC_OP_CMD1_EN
		    | NFC_OP_ADDR_EN | (addr_cycle << NFC_OP_ADDR_CYCLES_SHIFT);
		reg_offset = NFC_OP;
		hinfc_write(host,
			    (NAND_CMD_ERASE2 << 8) | (NAND_CMD_STATUS << 16) |
			    NAND_CMD_ERASE1, NFC_CMD);
	} else {
		op = NFC_OP_CTRL_OP_READY
		    | (NFC_OP_CTRL_CMD_TYPE_ERASE << NFC_OP_CTRL_CMD_TYPE_SHIFT)
		    | NFC_OP_CTRL_DMA_OP;
		reg_offset = NFC_OP_CTRL;
	}

	hinfc_write(host, op, reg_offset);

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait ready failed!\n");
		return -EIO;
	}

	return 0;
}

int hinfc_read_page(struct hisi_nfc *host, u32 block_num, u32 page_num,
		    u32 mode, void *read_buffer)
{
	u32 reg_offset;
	u32 op;
	u32 addr_cycle;
	u64 nfc_dma_phy_addr;
	u64 op_addr;
	u32 addrl;
	u32 addrh;
	u32 oob_low;
	u32 oob_high;
	u32 oob_addr_offset;

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait ready failed!\n");
		return -EIO;
	}
	nfc_dma_phy_addr = (u64)read_buffer;

	op_addr = hinfc_get_op_addr(host, block_num, page_num);
	hinfc_set_op_addr(host, op_addr, mode, host->mtd.writesize);

	addr_cycle = NFC_OP_ADDR_CYCLE_5;

	if (mode == NFC_OP_MODE_REG) {
		op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
		    | NFC_OP_READ_DATA_EN
		    | NFC_OP_WAIT_READY_EN
		    | NFC_OP_CMD2_EN
		    | NFC_OP_CMD1_EN
		    | NFC_OP_ADDR_EN
		    | (addr_cycle << NFC_OP_ADDR_CYCLES_SHIFT);
		reg_offset = NFC_OP;
		hinfc_write(host, (NAND_CMD_READSTART << 8) | NAND_CMD_READ0,
							 NFC_CMD);
	} else {
		op = NFC_OP_CTRL_OP_READY
		    | (NFC_OP_CTRL_CMD_TYPE_READ << NFC_OP_CTRL_CMD_TYPE_SHIFT)
		    | NFC_OP_CTRL_DMA_OP;

		reg_offset = NFC_OP_CTRL;
		addrl = (nfc_dma_phy_addr & 0xffffffff);
		addrh = (nfc_dma_phy_addr >> 32) & 0xffffffff;
		hinfc_write(host, addrl, NFC_PLN0_BADDRL_D0);
		hinfc_write(host, addrh, NFC_PLN0_BADDRH_D0);
		oob_addr_offset = host->mtd.writesize;
		oob_low =
		    ((u64)((u8 *)nfc_dma_phy_addr + oob_addr_offset)) &
								 0xffffffff;
		oob_high =
		    ((u64)((u8 *)nfc_dma_phy_addr + oob_addr_offset) >> 32) &
		    0xffffffff;
		hinfc_write(host, oob_low, NFC_PLN0_BADDRL_OOB);
		hinfc_write(host, oob_high, NFC_PLN0_BADDRH_OOB);
	}

	hinfc_write(host, op, reg_offset);

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait ready failed!\n");
		return -EIO;
	}

	return 0;
}

int hinfc_write_page(struct hisi_nfc *host, u32 block_num, u32 page_num,
		     u32 mode, void *write_buffer)
{
	u32 reg_offset;
	u32 op;
	u32 addr_cycle;
	u64 op_addr;
	u64 nfc_dma_phy_addr;
	u32 page_size;
	u32 addrl, addrh;
	u32 oob_low, oob_high;
	u32 oob_addr_offset;

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait ready failed\n");
		return -EIO;
	}

	nfc_dma_phy_addr = (u64)write_buffer;

	op_addr = hinfc_get_op_addr(host, block_num, page_num);
	hinfc_set_op_addr(host, op_addr, mode, host->mtd.writesize);

	addr_cycle = NFC_OP_ADDR_CYCLE_5;

	if (mode == NFC_OP_MODE_REG) {
		op = (host->chip_set << NFC_CON_NF_CS_SHIFT)
		    | NFC_OP_READ_STATUS_EN
		    | NFC_OP_WAIT_READY_EN
		    | NFC_OP_CMD2_EN
		    | NFC_OP_CMD1_EN
		    | NFC_OP_ADDR_EN
		    | NFC_OP_WRITE_DATA_EN
		    | (addr_cycle << NFC_OP_ADDR_CYCLES_SHIFT);
		reg_offset = NFC_OP;
		hinfc_write(host, NAND_CMD_SEQIN << 8 | NAND_CMD_PAGEPROG,
			    NFC_CMD);
	} else {
		op = NFC_OP_CTRL_OP_READY
		    | (NFC_OP_CTRL_CMD_TYPE_PROGRAM <<
		       NFC_OP_CTRL_CMD_TYPE_SHIFT) | NFC_OP_CTRL_DMA_OP;
		reg_offset = NFC_OP_CTRL;
		addrl = (nfc_dma_phy_addr & 0xffffffff);
		addrh = (nfc_dma_phy_addr >> 32) & 0xffffffff;
		page_size = host->mtd.writesize;
		oob_addr_offset = page_size;
		hinfc_write(host, addrl, NFC_PLN0_BADDRL_D0);
		hinfc_write(host, addrh, NFC_PLN0_BADDRH_D0);

		if ((page_size == PAGESIZE_CONST_8K) ||
		    (page_size == PAGESIZE_CONST_16K)) {
			addrl =
			    ((u64)((u8 *)nfc_dma_phy_addr + PAGESIZE_CONST_4K))
			    & 0xffffffff;
			addrh =
			    ((u64)((u8 *)nfc_dma_phy_addr + PAGESIZE_CONST_4K)
			     >> 32) & 0xffffffff;
			hinfc_write(host, addrl, NFC_PLN0_BADDRL_D1);
			hinfc_write(host, addrh, NFC_PLN0_BADDRH_D1);
		}

		if (page_size == PAGESIZE_CONST_16K) {
			addrl =
			    ((u64)((u8 *)nfc_dma_phy_addr + PAGESIZE_CONST_8K))
			    & 0xffffffff;
			addrh =
			    ((u64)((u8 *)nfc_dma_phy_addr + PAGESIZE_CONST_8K)
			     >> 32) & 0xffffffff;
			hinfc_write(host, addrl, NFC_PLN0_BADDRL_D2);
			hinfc_write(host, addrh, NFC_PLN0_BADDRH_D2);
			addrl = ((u64)
				  ((u8 *)nfc_dma_phy_addr +
				   PAGESIZE_CONST_4K * 3))
			    & 0xffffffff;
			addrh = ((u64)
				  ((u8 *)nfc_dma_phy_addr +
				   PAGESIZE_CONST_4K * 3)
				  >> 32) & 0xffffffff;
			hinfc_write(host, addrl, NFC_PLN0_BADDRL_D3);
			hinfc_write(host, addrh, NFC_PLN0_BADDRH_D3);
		}

		oob_low = ((u64)((u8 *)nfc_dma_phy_addr + oob_addr_offset))
		    & 0xffffffff;
		oob_high =
		    ((u64)((u8 *)nfc_dma_phy_addr + oob_addr_offset) >> 32)
		    & 0xffffffff;

		hinfc_write(host, oob_low, NFC_PLN0_BADDRL_OOB);
		hinfc_write(host, oob_high, NFC_PLN0_BADDRH_OOB);
	}

	hinfc_write(host, op, reg_offset);

	if (hinfc_wait_ready(host)) {
		dev_err(host->dev, "wait ready failed\n");
		return -EIO;
	}

	return 0;
}

static void hinfc_cmdfunc(struct mtd_info *mtd, unsigned command, int column,
			  int page_addr)
{
	struct hisi_nfc *host = to_hisi_nfc(mtd);
	u32 block_num;
	u32 page_num;
	u32 pages_per_block;

	pages_per_block = mtd->erasesize / mtd->writesize;

	if ((host->command == NAND_CMD_STATUS)
	    && (command == NAND_CMD_STATUS))
		return;

	/* store the last command */
	host->command = command;

	switch (command) {
	case NAND_CMD_READID:
		host->offset = (column >= 0) ? column : 0;
		hinfc_readID(host);
		memcpy(host->buffer, (u8 *)(host->chip.IO_ADDR_R), MAX_ID_LEN);
		break;
	case NAND_CMD_RESET:
		hinfc_reset_flash(host);
		break;
	case NAND_CMD_STATUS:
		host->offset = ((column >= 0) ? column : 0);
		(void)hinfc_read_flash_status(host);
		memcpy(host->buffer, (u8 *)(host->chip.IO_ADDR_R), 1);
		break;
	case NAND_CMD_READOOB:
		column = ((column >= 0) ? column : 0);
		host->offset = column + host->mtd.writesize;
		host->page_addr = ((page_addr >= 0) ? page_addr : 0);
		block_num = host->page_addr / pages_per_block;
		page_num = host->page_addr % pages_per_block;
		(void)hinfc_read_page(host, block_num, page_num,
				      NFC_OP_MODE_DMA, host->buffer);
		break;
	case NAND_CMD_READ0:
		host->page_addr = ((page_addr >= 0) ? page_addr : 0);
		host->offset = ((column >= 0) ? column : 0);
		block_num = host->page_addr / pages_per_block;
		page_num = host->page_addr % pages_per_block;
		(void)hinfc_read_page(host, block_num, page_num,
				      NFC_OP_MODE_DMA,
				      (void *)host->dma_buffer);
		break;
	case NAND_CMD_SEQIN:
		host->offset = ((column >= 0) ? column : 0);
		host->page_addr = ((page_addr >= 0) ? page_addr : 0);
		break;
	case NAND_CMD_PAGEPROG:
		block_num = host->page_addr / pages_per_block;
		page_num = host->page_addr % pages_per_block;
		(void)hinfc_write_page(host, block_num, page_num,
				       NFC_OP_MODE_DMA,
				       (void *)host->dma_buffer);
		break;
	/* ERASE1 stores the block and page address */
	case NAND_CMD_ERASE1:
		host->offset = ((column >= 0) ? column : 0);
		host->page_addr = ((page_addr >= 0) ? page_addr : 0);
		break;

	/* ERASE2 uses the block and page address from ERASE1 */
	case NAND_CMD_ERASE2:
		block_num = host->page_addr / pages_per_block;
		(void)hinfc_erase(host, block_num, NFC_OP_MODE_DMA);
		break;
	default:
		break;
	}
}

static u8 hinfc_read_byte(struct mtd_info *mtd)
{
	struct hisi_nfc *host = to_hisi_nfc(mtd);

	if (host->command == NAND_CMD_STATUS)
		return *(u8 *)(host->mmio);

	host->offset++;

	if (host->command == NAND_CMD_READID)
		return *(u8 *)(host->mmio + host->offset - 1);

	return *(u8 *)(host->buffer + host->offset - 1);
}

static u16 hinfc_read_word(struct mtd_info *mtd)
{
	u16 value;
	struct hisi_nfc *host = to_hisi_nfc(mtd);

	value = readw(host->buffer + host->offset);
	host->offset += 2;

	return value;
}

static void hinfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct hisi_nfc *host = to_hisi_nfc(mtd);

	memcpy(buf, host->buffer + host->offset, len);
	host->offset += len;
}

static void hinfc_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct hisi_nfc *host = to_hisi_nfc(mtd);

	memcpy(host->buffer + host->offset, buf, len);
	host->offset += len;
}

static void hinfc_select_chip(struct mtd_info *mtd, int chip)
{
	struct hisi_nfc *host = to_hisi_nfc(mtd);

	host->chip_set = NFC_CHIP_SEL;
}

/*
 * FIX ME: ECC don't supported, so don't ipmlement this
 * interface now.
 */
static int hinfc_ecc_calculate(struct mtd_info *mtd, const u8 *dat,
			       u8 *ecc_code)
{
	return -EINVAL;
}

/*
 * FIX ME: ECC don't supported, so don't ipmlement this
 * interface now.
 */
static void hinfc_ecc_hwctl(struct mtd_info *mtd, s32 mode)
{
}

/*
 * FIX ME: ECC don't supported, so don't ipmlement this
 * interface now.
 */
static int hinfc_ecc_correct(struct mtd_info *mtd, u8 *dat, u8 *read_ecc,
			     u8 *calc_ecc)
{
	return -EINVAL;
}

static void hinfc_ecc_probe(struct hisi_nfc *host)
{
	host->chip.ecc.calculate = hinfc_ecc_calculate;
	host->chip.ecc.hwctl = hinfc_ecc_hwctl;
	host->chip.ecc.correct = hinfc_ecc_correct;
	host->chip.ecc.strength = 1;
	host->chip.ecc.size = 1024;
}

static int hinfc_probe(struct platform_device *pdev)
{
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct hisi_nfc *host;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *np = dev->of_node;
	struct mtd_part_parser_data ppdata;
	int ret;

	host = devm_kzalloc(&pdev->dev, sizeof(struct hisi_nfc), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	platform_set_drvdata(pdev, host);

	host->dev = dev;
	chip = &host->chip;
	mtd = &host->mtd;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	host->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->iobase)) {
		ret = PTR_ERR(host->iobase);
		goto err_res;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	host->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->mmio)) {
		ret = PTR_ERR(host->mmio);
		dev_err(dev, "devm_ioremap_resource[1] failed!\n");
		goto err_res;
	}

	/* Link the private data with the MTD structure */
	mtd->priv = chip;
	mtd->owner = THIS_MODULE;
	mtd->name = (char *)pdev->name;
	mtd->dev.parent = dev;
	chip->IO_ADDR_R = host->mmio;
	chip->IO_ADDR_W = chip->IO_ADDR_R;
	chip->priv = host;
	chip->cmdfunc = hinfc_cmdfunc;
	chip->select_chip = hinfc_select_chip;
	chip->read_byte = hinfc_read_byte;
	chip->read_word = hinfc_read_word;
	chip->write_buf = hinfc_write_buf;
	chip->read_buf = hinfc_read_buf;
	chip->chip_delay = NFC_CHIP_DELAY_25;
	chip->options = NAND_SKIP_BBTSCAN;
	chip->ecc.mode = NAND_ECC_HW;

	host->buffer =
	    (u8 *)dmam_alloc_coherent(dev, PAGESIZE_CONST_16K,
				      &host->dma_buffer, GFP_KERNEL);
	if (!host->buffer) {
		ret = -ENOMEM;
		goto err_res;
	}

	/*
	 * FIX ME: If the pagesize bit region is zero, the nandc chip can't work
	 * because of safe protecting machine of our chip. So here we just give
	 * it a init value of 2k, but this value means nothing, the real value
	 * will be setted later.
	 */
	hinfc_set_pagesize(host, NFC_CON_PAGESIZE_BITS_2K);
	ret = nand_scan_ident(mtd, NFC_MAXCHIPS, NULL);
	if (ret) {
		ret = -ENODEV;
		goto err_res;
	}

	if (chip->ecc.mode == NAND_ECC_HW)
		hinfc_ecc_probe(host);

	ret = nand_scan_tail(mtd);
	if (ret) {
		dev_err(dev, "nand_scan_tail failed: %d\n", ret);
		goto err_res;
	}

	ppdata.of_node = np;

	ret = mtd_device_parse_register(mtd, NULL, &ppdata, NULL, 0);
	if (ret) {
		dev_err(dev, "Err MTD partition=%d\n", ret);
		goto err_mtd;
	}

	return 0;

err_mtd:
	nand_release(mtd);
err_res:
	return ret;
}

static const struct of_device_id host_of_match[] = {
	{.compatible = "hisilicon,hip05-nfc",}, {},
};

MODULE_DEVICE_TABLE(of, host_of_match);
static struct platform_driver host_driver = {
		.probe = hinfc_probe,
		.driver = {
		.name = "hisi-nand",
		.of_match_table = host_of_match,
		},
};

module_platform_driver(host_driver);
MODULE_AUTHOR("Jukuo Zhang <zhangjukuo@huawei.com>");
MODULE_LICENSE("GPL v2");
