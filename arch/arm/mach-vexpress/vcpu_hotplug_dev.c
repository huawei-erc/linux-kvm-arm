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
#include <asm-generic/sizes.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include "vcpu_hotplug_dev.h"
#include "cpumask_thread.h"

static struct vcpu_hotplug_dev vcpu_hp;

#undef pr_fmt
#define pr_fmt(fmt) "vcpu_hotplug_dev: " fmt ".\n"

/* platform device driver remove function */
__devexit static int vcpu_hotplug_device_remove(struct platform_device *pdev)
{
	if (vcpu_hp.irq > 0)
		free_irq(vcpu_hp.irq, NULL);

	if (vcpu_hp.virt_base_addr != NULL)
		iounmap(vcpu_hp.virt_base_addr);

	if (vcpu_hp.io_region != NULL)
		release_mem_region(vcpu_hp.phy_base_addr->start,
				resource_size(vcpu_hp.phy_base_addr));

	return 0;
}

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
		pr_err("could not get platform resource");
		res = -EINVAL;
		goto fail;
	}

	/* register io resource to kernel */
	vcpu_hp.io_region = request_mem_region(vcpu_hp.phy_base_addr->start,
					resource_size(vcpu_hp.phy_base_addr),
					"vcpu_hotplug_dev");
	if (!vcpu_hp.io_region) {
		pr_err("cannot register I/O memory");
		res = -EBUSY;
		goto fail;
	}

	vcpu_hp.virt_base_addr = ioremap(VCPU_HOTPLUG_DEV_BASE, PAGE_SIZE - 1);
	if (!vcpu_hp.virt_base_addr) {
		pr_err("ioremap failed");
		res = -EINVAL;
		goto fail;
	}

	/* get vcpu hotplug device irq number and register irq handler */
	vcpu_hp.irq = platform_get_irq(pdev, 0);
	if (vcpu_hp.irq < 0) {
		pr_err("failed to get platform IRQ");
		res = vcpu_hp.irq;
		goto fail;
	}

	pr_notice("using irq %d", vcpu_hp.irq);

	/* registering non-shared irq line */
	res = request_irq(vcpu_hp.irq, handle_vcpu_irq, 0,
			"vcpu_hotplug_dev", NULL);
	if (res < 0) {
		pr_err("cannot register IRQ %d", vcpu_hp.irq);
		goto fail;
	}
	/* create and wake up kernel thread */
	{
		struct task_struct *p;
		p = kthread_create_on_node(cpumask_thread, &vcpu_hp,
					   cpu_to_node(0),
					   "cpumask_thread/%d", 0);
		if (IS_ERR(p)) {
			pr_err("cannot create cpumask kthread");
			goto fail;
		}
		kthread_bind(p, 0);
		wake_up_process(p);
	}
	return 0;

fail:
	vcpu_hotplug_device_remove(pdev);
	return res;
}

static struct platform_driver vcpu_hotplug_driver = {
	.probe = vcpu_hotplug_device_probe,
	.remove = vcpu_hotplug_device_remove,
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
