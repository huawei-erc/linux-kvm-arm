/* VCPU Hotplug device driver for VExpress
 *
 * Copyright (c) 2013 Huawei Technologies Duesseldorf GmbH
 * Written by Jani Kokkonen
 *
 * This code is licensed under the GPLv2: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <mach/hardware.h>
#include "vcpu_hotplug_dev.h"
#include "cpumask_thread.h"

/* platform device driver irq handler */
static irqreturn_t handle_vcpu_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct vcpu_hotplug_dev *vcpu = platform_get_drvdata(pdev);
	unsigned long ctrl;

	/* read control byte */
	ctrl = ioread8(vcpu->virt_base_addr + VCPU_HP_HEADER_CTRL);
	dev_alert(&pdev->dev, "CTRL byte value is 0x%02lx", ctrl);
	/* clear IPR */
	clear_bit(VCPU_HP_CTRL_IPR, &ctrl);
	iowrite8(ctrl, vcpu->virt_base_addr + VCPU_HP_HEADER_CTRL);

	/* start cpumask thread */
	complete(&vcpu->complete);

	return IRQ_HANDLED;
}

/* platform device driver probe function */
static int __devinit vcpu_hotplug_device_probe(struct platform_device *pdev)
{
	struct vcpu_hotplug_dev *vcpu;
	int ret, cpu = smp_processor_id();

	vcpu = devm_kzalloc(&pdev->dev, sizeof(struct vcpu_hotplug_dev),
			    GFP_KERNEL);
	if (!vcpu)
		return -ENOMEM;

	/* start setting up platform device operations */
	/* get vcpu hotplug device base address */
	vcpu->phy_base_addr = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!vcpu->phy_base_addr) {
		dev_err(&pdev->dev, "could not get platform resource");
		return -EINVAL;
	}

	/* register io resource to kernel */
	vcpu->io_region = devm_request_mem_region(&pdev->dev,
					vcpu->phy_base_addr->start,
					resource_size(vcpu->phy_base_addr),
					"vcpu_hotplug_dev");
	if (!vcpu->io_region) {
		dev_err(&pdev->dev, "cannot register I/O memory");
		return -EBUSY;
	}

	/* XXX: size off by 1? */
	vcpu->virt_base_addr = devm_ioremap(&pdev->dev, VCPU_HOTPLUG_DEV_BASE,
					    PAGE_SIZE - 1);
	if (!vcpu->virt_base_addr) {
		dev_err(&pdev->dev, "ioremap failed");
		return -EINVAL;
	}

	/* get vcpu hotplug device irq number and register irq handler */
	vcpu->irq = platform_get_irq(pdev, 0);
	if (vcpu->irq < 0) {
		dev_err(&pdev->dev, "failed to get platform IRQ");
		return vcpu->irq;
	}

	dev_notice(&pdev->dev, "using irq %d", vcpu->irq);

	platform_set_drvdata(pdev, vcpu);

	/* registering non-shared irq line */
	ret = devm_request_irq(&pdev->dev, vcpu->irq, handle_vcpu_irq, 0,
			"vcpu_hotplug_dev", pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot register IRQ");
		goto err;
	}

	/* create and wake up kernel thread */
	vcpu->kthread = kthread_create_on_node(cpumask_thread, vcpu,
					       cpu_to_node(cpu),
					       "cpumask_thread/%d", cpu);
	if (IS_ERR(vcpu->kthread)) {
		dev_err(&pdev->dev, "cannot create cpumask kthread");
		ret = PTR_ERR(vcpu->kthread);
		goto err;
	}

	kthread_bind(vcpu->kthread, cpu);
	wake_up_process(vcpu->kthread);

	return 0;

err:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __devexit vcpu_hotplug_device_remove(struct platform_device *pdev)
{
	struct vcpu_hotplug_dev *vcpu = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	kthread_stop(vcpu->kthread);

	return 0;
}

static struct platform_driver vcpu_hotplug_driver = {
	.probe = vcpu_hotplug_device_probe,
	.remove = __devexit_p(vcpu_hotplug_device_remove),
	.driver = {
		.name = "vcpu_hotplug_device",
		.owner = THIS_MODULE,
	},
};

static int __init vcpu_hotplug_driver_init(void)
{
	return platform_driver_register(&vcpu_hotplug_driver);
}

static void __exit vcpu_hotplug_driver_exit(void)
{
	platform_driver_unregister(&vcpu_hotplug_driver);
}

subsys_initcall(vcpu_hotplug_driver_init);
module_exit(vcpu_hotplug_driver_exit);
