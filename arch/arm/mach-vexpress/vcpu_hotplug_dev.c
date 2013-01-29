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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/uaccess.h>
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
#include <asm-generic/sizes.h>
#include <mach/hardware.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include "vcpu_hotplug_dev.h"
#include "cpumask_thread.h"

static struct vcpu_hotplug_dev vcpu_hp;

/* platform device driver irq handler */
static irqreturn_t handle_vcpu_irq(int irq, void *opaque)
{
	irqreturn_t ret = IRQ_NONE;
	unsigned long ctrl;
	/* read control byte */
	ctrl = ioread8(vcpu_hp.virt_base_addr + VCPU_HP_HEADER_CTRL);
	pr_alert("CTRL byte value is 0x%02lx", ctrl);
	/* clear IPR */
	clear_bit(VCPU_HP_CTRL_IPR, &ctrl);
	iowrite8(ctrl, vcpu_hp.virt_base_addr + VCPU_HP_HEADER_CTRL);
	/* start cpumask thread */
	cpumask_flag = 1;
	wake_up_interruptible(&cpumask_wq);
	ret = IRQ_HANDLED;
	return ret;
}

/* platform device driver probe function */
__devinit static int vcpu_hotplug_device_probe(struct platform_device *pdev)
{
	int res;
	/* start setting up platform device operations */
	/* get vcpu hotplug device base address */
	vcpu_hp.phy_base_addr = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!vcpu_hp.phy_base_addr) {
		dev_err(&pdev->dev, "could not get platform resource");
		return -EINVAL;
	}

	/* register io resource to kernel */
	vcpu_hp.io_region = devm_request_mem_region(&pdev->dev,
					vcpu_hp.phy_base_addr->start,
					resource_size(vcpu_hp.phy_base_addr),
					"vcpu_hotplug_dev");
	if (!vcpu_hp.io_region) {
		dev_err(&pdev->dev, "cannot register I/O memory");
		return -EBUSY;
	}

	/* XXX: size off by 1? */
	vcpu_hp.virt_base_addr = devm_ioremap(&pdev->dev, VCPU_HOTPLUG_DEV_BASE,
					      PAGE_SIZE - 1);
	if (!vcpu_hp.virt_base_addr) {
		dev_err(&pdev->dev, "ioremap failed");
		return -EINVAL;
	}

	/* get vcpu hotplug device irq number and register irq handler */
	vcpu_hp.irq = platform_get_irq(pdev, 0);
	if (vcpu_hp.irq < 0) {
		dev_err(&pdev->dev, "failed to get platform IRQ");
		return vcpu_hp.irq;
	}

	dev_notice(&pdev->dev, "using irq %d", vcpu_hp.irq);

	/* registering non-shared irq line */
	res = devm_request_irq(&pdev->dev, vcpu_hp.irq, handle_vcpu_irq, 0,
			"vcpu_hotplug_dev", NULL);
	if (res < 0) {
		dev_err(&pdev->dev, "cannot register IRQ %d", vcpu_hp.irq);
		return res;
	}

	/* create and wake up kernel thread */
	{
		struct task_struct *p;
		p = kthread_create_on_node(cpumask_thread, &vcpu_hp,
					   cpu_to_node(0),
					   "cpumask_thread/%d", 0);
		if (IS_ERR(p)) {
			dev_err(&pdev->dev, "cannot create cpumask kthread");
			return PTR_ERR(p);
		}
		kthread_bind(p, 0);
		wake_up_process(p);
	}

	return 0;
}

static struct platform_driver vcpu_hotplug_driver = {
	.probe = vcpu_hotplug_device_probe,
	.driver = {
		.name = "vcpu_hotplug_device",
		.owner = NULL,
	},
};

__init static int vcpu_hotplug_driver_init(void)
{
	int ret;
	ret = platform_driver_register(&vcpu_hotplug_driver);
	if (ret) {
		printk(KERN_ALERT "vcpu_hotplug_driver_init: "
			"failed to register driver!\n");
	} else {
		printk(KERN_ALERT "vcpu_hotplug_driver_init: "
			"registered driver successfully.\n");
	}

	return ret;
}

__exit static void vcpu_hotplug_driver_exit(void)
{
	platform_driver_unregister(&vcpu_hotplug_driver);
}

subsys_initcall(vcpu_hotplug_driver_init);
module_exit(vcpu_hotplug_driver_exit);
