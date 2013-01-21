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
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
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

static int print_masks(char *buffer)
{
	const struct {
		const char *text; const struct cpumask *mask;
	} cpumasks[] = {
		{ "cpu_possible_mask", cpu_possible_mask },
		{ "cpu_online_mask", cpu_online_mask },
		{ "cpu_present_mask", cpu_present_mask },
		{ "cpu_active_mask", cpu_active_mask },
		{ NULL, NULL }
	};
	int i;

	for (i = 0; cpumasks[i].text != NULL; i++) {
		if (cpulist_scnprintf(buffer, PAGE_SIZE, cpumasks[i].mask) != 0)
			pr_notice("%s %s", cpumasks[i].text, buffer);
		else
			pr_notice("%s print error", cpumasks[i].text);
	}
	return 0;
}

__cpuinit static int test_cpumask(void)
{
	int cpu;
	char *buffer;

	/* allocate one page size buffer */
	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	/* offline one cpu */
	pr_notice("single cpu removed from online mask");
	cpu_down(1);
	print_masks(buffer);

	/* offline all but cpu 0 */
	for_each_online_cpu(cpu) {
		if (cpu != 0)
			cpu_down(cpu);
	}

	/* online all cpus */
	for_each_possible_cpu(cpu) {
		cpu_up(cpu);
	}

	kfree(buffer);
	return 0;
}

/* fileops open function */
static int vcpu_hotplug_device_open(struct inode *inode, struct file *filp)
{
	struct vcpu_hotplug_dev *dev;
	dev = container_of(inode->i_cdev, struct vcpu_hotplug_dev, cdev);
	filp->private_data = dev;
	return 0;
}

/*
 * TESTING fileops write()
 */
__cpuinit static ssize_t vcpu_hotplug_device_write_test(struct file *filp,
			const char __user *buf, size_t count, loff_t *f_pos)
{
	/* start cpumask thread */
	cpumask_flag = 1;
	wake_up_interruptible(&kthread_wq);
	return count;
}

/* fileops write function */
__cpuinit static ssize_t vcpu_hotplug_device_write(struct file *filp,
			const char __user *buf, size_t count, loff_t *f_pos)
{
	/* struct vcpu_hotplug_dev *dev = filp->private_data; */
	char *buffer;
	ssize_t retval = 0;

	/*
	if (down_interruptible(&dev->sem)) {
		printk(KERN_WARNING "semaphore error\n");
		return -ERESTARTSYS;
	}*/

	/* allocate one page size buffer */
	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* copy from user space */
	if (copy_from_user(buffer, buf, count)) {
		pr_notice("copy_from_user failed");
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval += count;

	pr_notice("received %s from user", buffer);

	/* clean the buffer */
	memset(buffer, 0, PAGE_SIZE);

	/* print initial cpu mask setup */
	pr_notice("original masks");
	print_masks(buffer);

	/* test cpumask operations */
	test_cpumask();
out:
	kfree(buffer);
	/* up(&dev->sem); */
	pr_warn("leaving fileops write");
	return retval;
}

/* fileops read function */
static ssize_t vcpu_hotplug_device_read(struct file *filp, char __user *buf,
					size_t count, loff_t *f_pos)
{
	int value;
	struct vcpu_hotplug_dev *dev = filp->private_data;
	void __iomem *io_address = __io_address(VCPU_HOTPLUG_DEV_BASE);
	/* Request unique access
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	*/
	pr_notice("phy_base_addr: %x, virt_base_addr(io_address): %p virt_base_addr_ioremap %p\n",
		(int)dev->phy_base_addr,
		(int *)io_address,
		(int *)dev->virt_base_addr);

	value = ioread32(dev->virt_base_addr + 0x4);
	printk(KERN_WARNING "device s->level value %x", value);
	/* up(&dev->sem); */
	return 0;
}

/* fileops release function */
static int vcpu_hotplug_device_release(struct inode *inode, struct file *filp)
{
	return 0;
}

__refdata static struct file_operations vcpu_hotplug_device_fops = {
	.owner = NULL,
	.read = vcpu_hotplug_device_read,
	.write = vcpu_hotplug_device_write_test,
	.open = vcpu_hotplug_device_open,
	.release = vcpu_hotplug_device_release,
};

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

	if (vcpu_hp.cdev.dev != 0)
		cdev_del(&vcpu_hp.cdev);

	if (vcpu_hp.devid != 0)
		unregister_chrdev_region(vcpu_hp.devid, VCPU_HOTPLUG_DEV_COUNT);

	return 0;
}

/* platform device driver irq handler */
static irqreturn_t handle_vcpu_irq(int irq, void *opaque)
{
	irqreturn_t ret = IRQ_NONE;
	unsigned long ctrl;
	static int i = 0;
	pr_alert("entered IRQ handler %d", i++);
	/* read control byte */
	ctrl = ioread8(vcpu_hp.virt_base_addr + VCPU_HP_HEADER_CTRL);
	pr_alert("CTRL byte value is 0x%02lx", ctrl);
	/* clear IPR */
	clear_bit(VCPU_HP_CTRL_IPR, &ctrl);
	iowrite8(ctrl, vcpu_hp.virt_base_addr + VCPU_HP_HEADER_CTRL);
	/* start cpumask thread */
	cpumask_flag = 1;
	wake_up_interruptible(&kthread_wq);
	ret = IRQ_HANDLED;
	return ret;
}

/* platform device driver probe function */
__devinit static int vcpu_hotplug_device_probe(struct platform_device *pdev)
{
	int res;
	/* Fill the vcpu_hotplug_device region with zeros */
	memset(&vcpu_hp, 0, sizeof(struct vcpu_hotplug_dev));

	/* allocate device number */
	res = alloc_chrdev_region(&vcpu_hp.devid, VCPU_HOTPLUG_DEV_MINOR_START,
				VCPU_HOTPLUG_DEV_COUNT,
				VCPU_HOTPLUG_DEV_NAME);
	if (res != 0) {
		pr_err("could not allocate char device");
		goto fail;

	} else {
		pr_warn("registered with major number:%i",
			MAJOR(vcpu_hp.devid));
	}
	/* sema_init(&vcpu_hotplug_device.sem, 1); */
	cdev_init(&vcpu_hp.cdev, &vcpu_hotplug_device_fops);
	vcpu_hp.cdev.owner = NULL;
	vcpu_hp.cdev.ops = &vcpu_hotplug_device_fops;
	res = cdev_add(&vcpu_hp.cdev,
		MKDEV(MAJOR(vcpu_hp.devid), MINOR(vcpu_hp.devid)), 1);

	if (res != 0) {
		pr_err("error %d adding vcpu hotplug device\n",	res);
		goto fail;

	} else {
		pr_warn("vcpu hotplug device added");
	}

	/* add proc entry: TODO */
	/*
	  create_proc_read_entry("vcpu_hotplug_device", 0 // default mode ,
	  NULL // parent dir , vcpu_hotplug_device_read_procmem,
	  NULL // client data);
	*/

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
	/* create kernel thread */
	kthread_run(cpumask_thread, &vcpu_hp, "cpumask_thread");
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
