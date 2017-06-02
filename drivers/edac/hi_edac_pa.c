/*
 * EDAC Driver for Hisilicon Protocol Adapter.
 *
 * Copyright (c) 2015, Hisilicon Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_data/hisi-djtag.h>

#include "edac_core.h"

#define PA_INT_MSK		0x0020
#define PA_RAWINT		0x0024
#define PA_INT_CLR		0x002c

#define PA_RX_MEM_ECC		BIT(0)
#define PA_H2RX_REQ_DAW_ERR	BIT(1)
#define PA_H1RX_REQ_DAW_ERR	BIT(2)
#define PA_H0RX_REQ_DAW_ERR	BIT(3)
#define PA_TX_MEM_ECC		BIT(7)
#define PA_TX_RSP_ERR		BIT(8)

#define EDAC_PA		"PA"
#define PA_IRQ_NUM	15
#define PA_IRQ8		8
#define PA_MOD_ADDR	0x26
#define PA_MOD_INDEX	0x0

struct pa_edac_dev {
	struct device		*dev;
	unsigned int		irq;
	void __iomem		*pa_base;
	struct device_node	*djtag_node;
	spinlock_t		pa_irq_lock;
};

static irqreturn_t hi_edac_pa_irq(int irq, void *dev_id)
{
	struct pa_edac_dev *pa_dev = (struct pa_edac_dev *)dev_id;
	unsigned int inter;
	unsigned long flags;

	spin_lock_irqsave(&pa_dev->pa_irq_lock, flags);

	/* open mask */
	djtag_writel(pa_dev->djtag_node, PA_INT_MSK,
		     PA_MOD_ADDR, 1 << PA_MOD_INDEX, 0xff);

	/* read interruption */
	djtag_readl(pa_dev->djtag_node, PA_RAWINT,
		    PA_MOD_ADDR, PA_MOD_INDEX, &inter);

	if (inter & (PA_H0RX_REQ_DAW_ERR | PA_H0RX_REQ_DAW_ERR |
	    PA_H0RX_REQ_DAW_ERR))
		edac_printk(KERN_CRIT, EDAC_PA,
			    "%s daw error,inter:0x%x\n",
			    dev_name(pa_dev->dev), inter);

	if (inter & PA_TX_RSP_ERR)
		edac_printk(KERN_CRIT, EDAC_PA,
			    "%s comprsp error,inter:0x%x\n",
			    dev_name(pa_dev->dev), inter);

	if (inter & (PA_TX_MEM_ECC | PA_RX_MEM_ECC))
		edac_printk(KERN_CRIT, EDAC_PA,
			    "%s multi ECC error,inter:0x%x\n",
			    dev_name(pa_dev->dev), inter);

	/* clear interruption */
	djtag_writel(pa_dev->djtag_node, PA_INT_CLR,
		     PA_MOD_ADDR, 1 << PA_MOD_INDEX, inter);

	/* clear mask */
	djtag_writel(pa_dev->djtag_node, PA_INT_MSK,
		     PA_MOD_ADDR, 1 << PA_MOD_INDEX, 0x0);

	spin_unlock_irqrestore(&pa_dev->pa_irq_lock, flags);

	return IRQ_HANDLED;
}

static int hi_pa_probe(struct platform_device *pdev)
{
	struct pa_edac_dev *pa_dev;
	int rc;

	pa_dev = devm_kzalloc(&pdev->dev, sizeof(*pa_dev), GFP_KERNEL);
	if (!pa_dev)
		return -ENOMEM;

	pa_dev->dev = &pdev->dev;

	/*
	 * for writing or reading djtag on nimbus side
	 * whem getting the point of djtag
	 */
	pa_dev->djtag_node = of_parse_phandle(pdev->dev.of_node, "djtag", 0);
	if (!pa_dev->djtag_node) {
		dev_err(&pdev->dev, "pa_dev djtag node is null\n");
		return -EFAULT;
	}

	spin_lock_init(&pa_dev->pa_irq_lock);

	pa_dev->irq = platform_get_irq(pdev, PA_IRQ8);
	if (pa_dev->irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		return -EINVAL;
	}

	rc = devm_request_irq(&pdev->dev, pa_dev->irq,
			      hi_edac_pa_irq, 0,
			      dev_name(&pdev->dev), pa_dev);
	if (rc) {
		dev_err(&pdev->dev, "irq req failed\n");
		goto out_err;
	}

	/* clear mask */
	djtag_writel(pa_dev->djtag_node, PA_INT_MSK,
		     PA_MOD_ADDR, PA_MOD_INDEX, 0);

	/* clear wrong interruption */
	djtag_writel(pa_dev->djtag_node, PA_INT_CLR,
		     PA_MOD_ADDR, 1 << PA_MOD_INDEX, 0x3ff);

	return 0;

out_err:
	devm_free_irq(&pdev->dev, pa_dev->irq, pa_dev);

	return rc;
}

static int hi_pa_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id pa_edac_of_match[] = {
	{ .compatible = "hisilicon,pa-edac" },
	{},
};
MODULE_DEVICE_TABLE(of, pa_edac_of_match);

static struct platform_driver pa_edac_driver = {
	.probe = hi_pa_probe,
	.remove = hi_pa_remove,
	.driver = {
		.name = "pa-edac",
		.owner = THIS_MODULE,
		.of_match_table = pa_edac_of_match,
	},
};

module_platform_driver(pa_edac_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("HISILICON EDAC driver");
MODULE_AUTHOR("Peter Chen <luck.chen@huawei.com>");
MODULE_AUTHOR("Fengying Wang <fy.wang@huawei.com>");
