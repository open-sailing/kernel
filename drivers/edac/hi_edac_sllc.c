/*
 * EDAC Driver for Hisilicon Skyros Link Layer Controller.
 *
 * Copyright (c) 2015, Hisilicon Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_data/hisi-djtag.h>

#include "edac_core.h"

#define SLLCC_NUM			4
#define SLLCN_NUM			2
#define SLLCC_IRQ_NUM			4
#define SLLCN_IRQ_NUM			15

#define SLLC_SUBCTRL_SIZE		0x10000

#define SLLC_INT_RAWINT			0x0014
#define SLLC_INT_MSK			0x0010
#define SLLC_INT_CLR			0x001c

#define SLLC_IO_MERR_MASK		BIT(0)
#define SLLC_PTR_ERR_MASK		BIT(1)
#define SLLC_MISC_CFG			BIT(30)

#define CRS_CTRL			0x0000
#define TX_CRDT_CNT			0x0074
#define SLLC_FIFO_STATUS		0x0048

#define HRD_SLLC_MISC_CFG_REG		0x0008
#define SLLC_MISC_CFG2			0x0044
#define SLLC_TRAN_EN_MASK		BIT(28)

#define SLLC_FIFO_STATUS_VALUE		0x95555

#define TX_CRDT_CNT_VALUE		0x820820

#define EDAC_SLLCC			"SLLC TotermC"
#define EDAC_SLLCN			"SLLC Nimbus"

/* device is 0x40b1a irq in use */
#define SLLC0_LPI3			3
#define SLLC0_LPI4			4

#define SWITCH_TIMES		100000
#define SWITCH_EACH_TIME	20

struct his_edac_sllc {
	struct device_node *base_addr0;
	struct device_node *base_addr1;
	unsigned int modsel_addr0;
	unsigned int modsel_addr1;
	unsigned int cs_modsel_addr0;
	unsigned int cs_modsel_addr1;
	unsigned int cs_mod_index0;
	unsigned int cs_mod_index1;
	unsigned int mod_index0;
	unsigned int mod_index1;
};

struct his_sllc_switch_result {
	unsigned int sllc_trans_en;
	unsigned int cs_value0;
	unsigned int cs_value1;
	unsigned int disable_port_dn;
	unsigned int disable_port_up;
	unsigned int sllc_status0;
	unsigned int sllc_status1;
	unsigned int sllc_cnt0;
	unsigned int sllc_cnt1;
	unsigned int sllc_trans_en0;
	unsigned int sllc_trans_en1;
	unsigned int misc_cfg_reg;
};

struct sllcc_edac_dev;

struct sllcc_edac {
	unsigned int		irqnum;
	struct sllcc_edac_dev	*sllccdev;
};

struct sllcc_edac_dev {
	struct device		*dev;
	unsigned int		irq[SLLCC_IRQ_NUM];
	void __iomem		*sllc_base_c;
	struct sllcc_edac	sllccisr[SLLCC_NUM];
	struct device_node	*djtag_node;
	spinlock_t		sllcc_irq_lock;
	const struct peri_sllc_handle *sllc_handle;
};

struct peri_sllc_handle {

	/*
	 * when using djtag to access sllc reg, modsel specify the
	 * location of the module on the djtag.
	 */
	unsigned int mod_sel[SLLCC_NUM][2];

	/* when using spi interrupt,the irq num must list from zero */
	unsigned int irq_shift;
};

struct sllcn_edac_dev;

struct sllcn_edac {
	unsigned int		irqnum;
	struct sllcn_edac_dev	*sllcndev;
};

struct sllcn_edac_dev {
	struct device		*dev;
	unsigned int		irq[SLLCN_IRQ_NUM];
	void __iomem		*sllc_base_n;
	struct sllcn_edac	sllcnisr[SLLCN_NUM];
	struct device_node	*nimbus_djtag_node;
	struct workqueue_struct *sllcn_wq;
	spinlock_t		sllcn_irq_lock;
};

static int edac_djtag_write(struct device_node *node, unsigned int reg,
			    unsigned int modaddr, unsigned int value,
			    unsigned int mod_index)
{
	return djtag_writel(node, reg, modaddr, 1 << mod_index, value);
}

static int edac_djtag_read(struct device_node *node, unsigned int reg,
			unsigned int modaddr, unsigned int *value,
			unsigned int reg_index)
{
	return djtag_readl(node, reg, modaddr, reg_index, value);
}

static void hi_edac_sllcc_init(struct sllcc_edac_dev *sllc_dev)
{
	unsigned int i;

	for (i = 0; i < SLLCC_NUM; i++) {
		/* clear interruption */
		edac_djtag_write(sllc_dev->djtag_node, SLLC_INT_CLR,
				 sllc_dev->sllc_handle->mod_sel[i][0], 0xffffff,
				 sllc_dev->sllc_handle->mod_sel[i][1]);
		/* clear mask res */
		edac_djtag_write(sllc_dev->djtag_node, SLLC_INT_MSK,
				 sllc_dev->sllc_handle->mod_sel[i][0], 0xfffffc,
				 sllc_dev->sllc_handle->mod_sel[i][1]);
	}
}

static irqreturn_t hi_edac_sllcc_irq(int irq, void *dev_id)
{
	struct sllcc_edac_dev *sllc_dev;
	struct sllcc_edac *sllc_edac = (struct sllcc_edac *)dev_id;
	unsigned int mod_addr;
	unsigned int mod_index;
	unsigned int inter;
	unsigned long flags;

	sllc_dev = sllc_edac->sllccdev;
	spin_lock_irqsave(&sllc_dev->sllcc_irq_lock, flags);

	mod_addr = sllc_dev->sllc_handle->mod_sel[sllc_edac->irqnum][0];
	mod_index = sllc_dev->sllc_handle->mod_sel[sllc_edac->irqnum][1];

	/* open mask */
	edac_djtag_write(sllc_dev->djtag_node, SLLC_INT_MSK, mod_addr, 0xffffff,
			 mod_index);
	/* read interruption */
	edac_djtag_read(sllc_dev->djtag_node, SLLC_INT_RAWINT, mod_addr,
			&inter, mod_index);
	/* small io multiple bits error */
	if (inter & SLLC_IO_MERR_MASK)
		edac_printk(KERN_CRIT, EDAC_SLLCC,
			    "%s: SLLC_IO_MERR Error,inter:0x%x,mod_addr:0x%x,mod_index:0x%x\n",
			    dev_name(sllc_dev->dev), inter,
			    mod_addr, mod_index);

	/* clear interruption */
	edac_djtag_write(sllc_dev->djtag_node, SLLC_INT_CLR, mod_addr, inter,
			 mod_index);
	/* clear mask */
	edac_djtag_write(sllc_dev->djtag_node, SLLC_INT_MSK, mod_addr, 0x0,
			 mod_index);

	spin_unlock_irqrestore(&sllc_dev->sllcc_irq_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t hi_edac_sllcn_irq(int irq, void *dev_id)
{
	struct sllcn_edac_dev *sllc_dev;
	struct sllcn_edac *sllc_edac = (struct sllcn_edac *)dev_id;
	unsigned int array[SLLCN_NUM][2] = { {2, 0 }, {3, 0 } };
	unsigned int mod_addr;
	unsigned int mod_index;
	unsigned int inter;
	unsigned long flags;

	sllc_dev = sllc_edac->sllcndev;
	spin_lock_irqsave(&sllc_dev->sllcn_irq_lock, flags);

	mod_addr = array[sllc_edac->irqnum][0];
	mod_index = array[sllc_edac->irqnum][1];

	/* open mask register */
	edac_djtag_write(sllc_dev->nimbus_djtag_node, SLLC_INT_MSK,
				mod_addr, 0x03, mod_index);
	/* get interrupt */
	edac_djtag_read(sllc_dev->nimbus_djtag_node, SLLC_INT_RAWINT,
				mod_addr, &inter, mod_index);
	if (inter & SLLC_IO_MERR_MASK)
		edac_printk(KERN_CRIT, EDAC_SLLCN,
			    "%s: SLLC_IO_MERR Error,inter:0x%x,mod_addr:0x%x,mod_index:0x%x\n",
			    dev_name(sllc_dev->dev), inter,
			    mod_addr, mod_index);
	if (inter & SLLC_PTR_ERR_MASK)
		edac_printk(KERN_CRIT, EDAC_SLLCC,
			    "%s: SLLC_IO_MERR Error,inter:0x%x,mod_addr:0x%x,mod_index:0x%x\n",
			    dev_name(sllc_dev->dev), inter,
			    mod_addr, mod_index);

	/* clear interrupt */
	edac_djtag_read(sllc_dev->nimbus_djtag_node, SLLC_INT_CLR, mod_addr,
			&inter, mod_index);
	/* clear mask */
	edac_djtag_write(sllc_dev->nimbus_djtag_node, SLLC_INT_MSK, mod_addr,
			 0x0, mod_index);

	spin_unlock_irqrestore(&sllc_dev->sllcn_irq_lock, flags);

	return IRQ_HANDLED;
}

static const struct peri_sllc_handle hip05_peri_sllc_handle = {
	.mod_sel = { { 2, 0 }, { 2, 1 }, { 3, 0 }, { 3, 1 } },
	.irq_shift = 0,
};

static const struct peri_sllc_handle hi1382_peri_sllc_handle = {
	.mod_sel = { { 7, 0 }, { 8, 0 }, { 9, 0 }, { 10, 0 } },
	.irq_shift = 7,
};

static const struct of_device_id sllcc_edac_of_match[] = {
	/* for 1610 and 1612 toterm sllc */
	{
		.compatible = "hisilicon,sllcc-edac",
		.data = &hip05_peri_sllc_handle
	},
	/* for 1616 and 1382 toterm sllc */
	{
		.compatible = "hisilicon,hi1382-sllcc-edac",
		.data = &hi1382_peri_sllc_handle
	},
	{},
};

static int hi_sllcc_probe(struct platform_device *pdev)
{
	struct sllcc_edac_dev *sllc_dev;
	const struct of_device_id *of_id;
	unsigned int count;
	int rc, i;

	of_id = of_match_device(sllcc_edac_of_match, &pdev->dev);
	if (!of_id)
		return -EINVAL;

	sllc_dev = devm_kzalloc(&pdev->dev, sizeof(*sllc_dev), GFP_KERNEL);
	if (!sllc_dev)
		return -ENOMEM;
	sllc_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, sllc_dev);

	sllc_dev->sllc_handle = of_id->data;

	/*
	 *for writing or reading djtag on nimbus side
	 * whem getting the point of djtag
	 */

	sllc_dev->djtag_node = of_parse_phandle(pdev->dev.of_node, "djtag", 0);
	spin_lock_init(&sllc_dev->sllcc_irq_lock);

	hi_edac_sllcc_init(sllc_dev);

	for (count = 0; count < SLLCC_IRQ_NUM; count++) {
		sllc_dev->irq[count] = platform_get_irq(pdev,
			count + sllc_dev->sllc_handle->irq_shift);
		sllc_dev->sllccisr[count].irqnum = count;
		sllc_dev->sllccisr[count].sllccdev = sllc_dev;

		if (sllc_dev->irq[count] < 0) {
			dev_err(&pdev->dev, "No IRQ resource\n");
			rc = -EINVAL;
			goto out_err;
		}

		rc = devm_request_irq(&pdev->dev, sllc_dev->irq[count],
				      hi_edac_sllcc_irq, 0,
				      dev_name(&pdev->dev),
				      &sllc_dev->sllccisr[count]);
		if (rc) {
			dev_err(&pdev->dev, "irq count:%d req failed\n", count);
			goto out_err;
		}
	}

	return 0;

out_err:
	for (i = 0; i < count; i++)
		devm_free_irq(&pdev->dev, sllc_dev->irq[i],
			      &sllc_dev->sllccisr[i]);
	return rc;
}

static int hi_sllcn_probe(struct platform_device *pdev)
{
	struct sllcn_edac_dev *sllc_dev;
	unsigned int count;
	unsigned int realcount;
	int rc, i;

	sllc_dev = devm_kzalloc(&pdev->dev, sizeof(*sllc_dev), GFP_KERNEL);
	if (!sllc_dev)
		return -ENOMEM;
	sllc_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, sllc_dev);

	/*
	 *for writing or reading djtag on nimbus side
	 * whem getting the point of djtag
	 */
	sllc_dev->nimbus_djtag_node = of_parse_phandle(
					pdev->dev.of_node, "djtag", 0);
	if (!sllc_dev->nimbus_djtag_node) {
		dev_err(&pdev->dev, "nimbus djtag node is null\n");
		return -EFAULT;
	}

	sllc_dev->sllcn_wq = create_singlethread_workqueue("sllcn_event");
	if (!sllc_dev->sllcn_wq) {
		dev_err(&pdev->dev, "create sllcn event workqueue failed\n");
		return -ENOMEM;
	}

	for (count = 0; count < SLLCN_IRQ_NUM; count++) {
		sllc_dev->irq[count] = platform_get_irq(pdev, count);
		if (sllc_dev->irq[count] < 0) {
			dev_err(&pdev->dev, "No IRQ resource\n");
			rc = -EINVAL;
			goto out_err;
		}

		if (SLLC0_LPI3 ==  count || SLLC0_LPI4 == count) {
			realcount = count - SLLC0_LPI3;
			sllc_dev->sllcnisr[realcount].irqnum = realcount;
			sllc_dev->sllcnisr[realcount].sllcndev = sllc_dev;
			rc = devm_request_irq(&pdev->dev, sllc_dev->irq[count],
					      hi_edac_sllcn_irq, 0,
					      dev_name(&pdev->dev),
					      &sllc_dev->sllcnisr[realcount]);
		}
		if (rc) {
			dev_err(&pdev->dev, "irq count:%d req failed\n", count);
			goto out_err;
		}
	}

	return 0;

out_err:
	for (i = 0; i < realcount; i++) {
		count = realcount + SLLC0_LPI3;
		devm_free_irq(&pdev->dev, sllc_dev->irq[count],
			      &sllc_dev->sllcnisr[i]);
	}

	return rc;
}

static int hi_sllcc_remove(struct platform_device *pdev)
{
	return 0;
}

static int hi_sllcn_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sllcn_edac_of_match[] = {
	{ .compatible = "hisilicon,sllcn-edac" },
	{},
};

MODULE_DEVICE_TABLE(of, sllcc_edac_of_match);
MODULE_DEVICE_TABLE(of, sllcn_edac_of_match);

static struct platform_driver sllcc_edac_driver = {
	.probe = hi_sllcc_probe,
	.remove = hi_sllcc_remove,
	.driver = {
		.name = "sllcc-edac",
		.owner = THIS_MODULE,
		.of_match_table = sllcc_edac_of_match,
	},
};

static struct platform_driver sllcn_edac_driver = {
	.probe = hi_sllcn_probe,
	.remove = hi_sllcn_remove,
	.driver = {
		.name = "sllcn-edac",
		.owner = THIS_MODULE,
		.of_match_table = sllcn_edac_of_match,
	},
};

static int __init hi_sllc_edac_init(void)
{
	int ret;

	ret = platform_driver_register(&sllcc_edac_driver);
	if (ret)
		return ret;
	ret = platform_driver_register(&sllcn_edac_driver);
	if (ret)
		platform_driver_unregister(&sllcc_edac_driver);

	return ret;
}
module_init(hi_sllc_edac_init);

static void __exit hi_sllc_edac_exit(void)
{
	platform_driver_unregister(&sllcc_edac_driver);
	platform_driver_unregister(&sllcn_edac_driver);
}
module_exit(hi_sllc_edac_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("HISILICON EDAC driver");
MODULE_AUTHOR("Peter Chen <luck.chen@huawei.com>");
MODULE_AUTHOR("Fengying Wang <fy.wang@huawei.com>");
