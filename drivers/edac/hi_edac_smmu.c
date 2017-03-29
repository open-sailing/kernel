/*
 * EDAC Driver for Hisilicon System Memory Management Unit.
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
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/time.h>

#include "edac_core.h"

#define SMMU_TOTERMC_NUM	2
#define SMMU_NIMBUS_NUM		3
#define DISP_NIMBUS_NUM		3
#define SMMU_EDAC		"SMMU"

#define EDAC_TIMEOUT_JIFFIES	(20 * HZ)
#define SMMU_HIS_FAULT_NUM	0x0e94
#define SMMU_GERROR		0x0060
#define SSMMU_IRQ_CTRL		0x0050
#define SMMU_GERRORN		0x0064

#define SMMU_EVENTQ		BIT(2)
#define SMMU_VA_FAULT_FLAG	BIT(28)
#define SMMU_DFX_INSTALL_IRPT	BIT(31)

#define SMMU_IRQ_NUM		3
#define SMMU_IRQ_MASK		0x07
#define SMMU_IRQ_ALL		0x05

struct smmu_edac_dev {
	struct device		*dev;
	unsigned int		irq[SMMU_IRQ_NUM];
	void __iomem		*smmu_base_n;
	struct timer_list	smmu_timer;
	spinlock_t		smmu_irq_lock;
};

static irqreturn_t hi_edac_smmu_nimbus_irq(int irq, void *dev_id)
{
	struct smmu_edac_dev *smmu_dev = dev_id;
	unsigned int smmu_fault;
	unsigned int smmu_gerror;
	unsigned long flags;

	spin_lock_irqsave(&smmu_dev->smmu_irq_lock, flags);

	/* open mask */
	writel(0x0, smmu_dev->smmu_base_n + SSMMU_IRQ_CTRL);

	smmu_fault = readl(smmu_dev->smmu_base_n + SMMU_HIS_FAULT_NUM);
	smmu_gerror = readl(smmu_dev->smmu_base_n + SMMU_GERROR);

	if (smmu_gerror)
		edac_printk(KERN_CRIT, SMMU_EDAC,
			    "%s had global error,smmu_gerror:%x,smmu_fault:%x\n",
			    dev_name(smmu_dev->dev),
			    smmu_gerror, smmu_fault);

	writel(smmu_gerror & SMMU_IRQ_ALL,
	       smmu_dev->smmu_base_n + SMMU_GERRORN);

	/* clear mask */
	writel(SMMU_IRQ_MASK, smmu_dev->smmu_base_n + SSMMU_IRQ_CTRL);

	spin_unlock_irqrestore(&smmu_dev->smmu_irq_lock, flags);

	return IRQ_HANDLED;
}

static void edac_smmu_nimbus_scan_reg(unsigned long data)
{
	struct smmu_edac_dev *smmu_dev = (struct smmu_edac_dev *)data;
	unsigned int smmu_status = 0;

	smmu_status = readl(smmu_dev->smmu_base_n + SMMU_HIS_FAULT_NUM);

	if (smmu_status & (SMMU_VA_FAULT_FLAG | SMMU_DFX_INSTALL_IRPT))
		edac_printk(KERN_CRIT, SMMU_EDAC,
			    "%s had stall or va addr error:%d\n",
			    dev_name(smmu_dev->dev), smmu_status);

	mod_timer(&(smmu_dev->smmu_timer), jiffies + EDAC_TIMEOUT_JIFFIES);
}

static int hi_smmu_probe(struct platform_device *pdev)
{
	struct smmu_edac_dev *smmu_dev;
	unsigned int count;
	struct resource *edac_res;
	int rc, i;

	smmu_dev = devm_kzalloc(&pdev->dev, sizeof(*smmu_dev), GFP_KERNEL);
	if (!smmu_dev)
		return -ENOMEM;

	smmu_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, smmu_dev);

	edac_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!edac_res) {
		dev_err(&pdev->dev, "Can not find this resource\n");
		return -ENOENT;
	}

	smmu_dev->smmu_base_n = devm_ioremap_resource(&pdev->dev, edac_res);
	if (IS_ERR(smmu_dev->smmu_base_n)) {
		dev_err(&pdev->dev, "no resource address\n");
		return -ENOENT;
	}

	setup_timer(&smmu_dev->smmu_timer, edac_smmu_nimbus_scan_reg,
		    (unsigned long)smmu_dev);

	mod_timer(&smmu_dev->smmu_timer, jiffies + EDAC_TIMEOUT_JIFFIES);

	spin_lock_init(&smmu_dev->smmu_irq_lock);

	for (count = 0; count < SMMU_IRQ_NUM; count++) {
		smmu_dev->irq[count] = platform_get_irq(pdev, count);
		if (smmu_dev->irq[count] < 0) {
			dev_err(&pdev->dev, "No IRQ resource\n");
			rc = -EINVAL;
			goto out_err;
		}

		rc = devm_request_irq(&pdev->dev, smmu_dev->irq[count],
				      hi_edac_smmu_nimbus_irq, 0,
				      dev_name(&pdev->dev), smmu_dev);
		if (rc) {
			dev_err(&pdev->dev, "irq count:%d req failed\n", count);
			goto out_err;
		}
	}

	/* clear mask */
	writel(SMMU_IRQ_MASK, smmu_dev->smmu_base_n + SSMMU_IRQ_CTRL);

	/* clear wrong interruption */
	writel(SMMU_IRQ_ALL, smmu_dev->smmu_base_n + SMMU_GERRORN);

	return 0;

out_err:
	for (i = 0; i < count; i++)
		devm_free_irq(&pdev->dev, smmu_dev->irq[i], smmu_dev);

	return rc;
}

static int hi_smmu_remove(struct platform_device *pdev)
{
	struct smmu_edac_dev *smmu_dev = platform_get_drvdata(pdev);

	del_timer(&smmu_dev->smmu_timer);

	return 0;
}

static const struct of_device_id smmu_edac_of_match[] = {
	{ .compatible = "hisilicon,smmu-edac" },
	{},
};

MODULE_DEVICE_TABLE(of, smmu_edac_of_match);

static struct platform_driver smmu_edac_driver = {
	.probe = hi_smmu_probe,
	.remove = hi_smmu_remove,
	.driver = {
		.name = "smmu-edac",
		.owner = THIS_MODULE,
		.of_match_table = smmu_edac_of_match,
	},
};

module_platform_driver(smmu_edac_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("HISILICON EDAC driver");
MODULE_AUTHOR("Fengying Wang <fy.wang@huawei.com>");
MODULE_AUTHOR("Peter Chen <luck.chen@huawei.com>");
