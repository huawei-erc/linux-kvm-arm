/* VCPU Hotplug device driver for VExpress
 *
 * Copyright (c) 2012 Huawei Technologies Duesseldorf GmbH
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

struct vcpu_hotplug_dev *vcpu_hotplug_device;
struct resource *mem_region;
static dev_t dev;

/*******************************************************
* print all cpu masks
*******************************************************/

int print_masks(char *buffer)
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
			printk(KERN_NOTICE "%s %s\n", cpumasks[i].text, buffer);
		else
			printk(KERN_NOTICE "%s print error\n", cpumasks[i].text);
	}
	return 0;
}

/***********************************************************
* test cpumask
***********************************************************/

__cpuinit int test_cpumask(void)
{
	int cpu;
	char *buffer;

	/* allocate one page size buffer */
	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	/* offline one cpu */
	printk(KERN_NOTICE "single cpu removed from online mask\n");
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

/********************************************************************
* TESTING fileops write()
*********************************************************************/
__cpuinit ssize_t vcpu_hotplug_device_write_test(struct file *filp,
			const char __user *buf, size_t count, loff_t *f_pos)
{
	/* start cpumask thread */
	cpumask_flag = 1;
	wake_up_interruptible(&kthread_wq);
	return count;
}

/********************************************************************
* fileops write()
*********************************************************************/
__cpuinit ssize_t vcpu_hotplug_device_write(struct file *filp,
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
		printk(KERN_NOTICE "copy_from_user failed.\n");
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval += count;

	printk(KERN_NOTICE "received %s from user\n", buffer);

	/* clean the buffer */
	memset(buffer, 0, PAGE_SIZE);

	/* print initial cpu mask setup */
	printk(KERN_NOTICE "original masks\n");
	print_masks(buffer);

	/* test cpumask operations */
	test_cpumask();
out:
	kfree(buffer);
	//up(&dev->sem);
	printk(KERN_WARNING "leaving fileops write\n");
	return retval;
}

/********************************************************************************************
 *  fileops read() function
 ********************************************************************************************/
ssize_t vcpu_hotplug_device_read(struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct vcpu_hotplug_dev *dev = filp->private_data;
	int value;
	void __iomem *io_address = __io_address(VCPU_HOTPLUG_DEVICE_BASE);
	/* Request unique access
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	*/

	printk(KERN_NOTICE
		"phy_base_addr: %x, virt_base_addr(io_address): %p virt_base_addr_ioremap %p\n",
		(int)dev->phy_base_addr,
		(int *)io_address,
		(int *)dev->virt_base_addr);

	value = ioread32(dev->virt_base_addr + 0x4);
	printk(KERN_WARNING "device s->level value %x", value);

	//up(&dev->sem);
	return 0;
}

/******************************************************************
 * fileops open() function
 ******************************************************************/
int vcpu_hotplug_device_open(struct inode *inode, struct file *filp)
{
	struct vcpu_hotplug_dev *dev;

	dev = container_of(inode->i_cdev, struct vcpu_hotplug_dev, cdev);
	filp->private_data = dev;

	return 0;
}

/******************************************************************
 * fileops release()
 *****************************************************************/
int vcpu_hotplug_device_release(struct inode *inode, struct file *filp)
{
	return 0;
}

__refdata struct file_operations vcpu_hotplug_device_fops = {
	.owner = NULL,
	.read = vcpu_hotplug_device_read,
	.write = vcpu_hotplug_device_write_test,
	.open = vcpu_hotplug_device_open,
	.release = vcpu_hotplug_device_release,
};

/*********************************************************
 * platform device remove()
 *********************************************************/

static __devexit int vcpu_hotplug_device_remove(struct platform_device *pdev)
{
	if (vcpu_hotplug_device->irq > 0)
		free_irq(vcpu_hotplug_device->irq, NULL);

	if (vcpu_hotplug_device->virt_base_addr)
		iounmap(vcpu_hotplug_device->virt_base_addr);

	if (mem_region)
		release_mem_region(vcpu_hotplug_device->phy_base_addr->start,
				resource_size(vcpu_hotplug_device->phy_base_addr));

	if (vcpu_hotplug_device->cdev.dev != 0)
		cdev_del(&vcpu_hotplug_device->cdev);

	if (dev != 0)
		unregister_chrdev_region(dev, VCPU_HOTPLUG_DEVICE_COUNT);

	kfree(vcpu_hotplug_device);
	return 0;
}

/*******************************************************
* handle_vcpu_irq();
*******************************************************/
static irqreturn_t handle_vcpu_irq(int irq, void *opaque)
{
	irqreturn_t ret = IRQ_NONE;
	unsigned long value;
	static int i = 0;
	printk(KERN_ALERT "Entered IRQ handler %d\n", i++);
	/*read control byte */
	value = ioread8(vcpu_hotplug_device->virt_base_addr + VCPU_HP_HEADER_CTRL);
	printk(KERN_ALERT "CTRL byte value is %lu.\n", value);
	/*disable IRQ */
	clear_bit(0,&value);
	iowrite8(value, vcpu_hotplug_device->virt_base_addr + VCPU_HP_HEADER_CTRL);
	/* start cpumask thread */
	cpumask_flag = 1;
	wake_up_interruptible(&kthread_wq);
	ret = IRQ_HANDLED;
	return ret;
}

/********************************************************
 * platform driver probe() function
 *******************************************************/
static __devinit int vcpu_hotplug_device_probe(struct platform_device *pdev)
{
	int res;
	/* allocate memory for vcpu_hotplug_device */
	vcpu_hotplug_device =
		kmalloc(sizeof(struct vcpu_hotplug_dev), GFP_KERNEL);

	if (vcpu_hotplug_device == NULL) {
		return -ENOMEM;
	}
	/* Fill the vcpu_hotplug_device region with zeros */
	memset(vcpu_hotplug_device, 0, sizeof(struct vcpu_hotplug_dev));

	/* allocate device number */
	res = alloc_chrdev_region(&dev, VCPU_HOTPLUG_DEVICE_MINOR_START,
				VCPU_HOTPLUG_DEVICE_COUNT,
				VCPU_HOTPLUG_DEVICE_NAME);
	if (res != 0) {
		printk(KERN_ERR "could not allocate device\n");
		goto fail;

	} else {
		printk(KERN_WARNING "registered with major number:%i\n",
			MAJOR(dev));
	}

	//sema_init(&vcpu_hotplug_device->sem, 1);
	cdev_init(&vcpu_hotplug_device->cdev, &vcpu_hotplug_device_fops);
	vcpu_hotplug_device->cdev.owner = NULL;
	vcpu_hotplug_device->cdev.ops = &vcpu_hotplug_device_fops;
	res = cdev_add(&vcpu_hotplug_device->cdev, MKDEV(MAJOR(dev), MINOR(dev)), 1);

	if (res != 0) {
		printk(KERN_ERR "Error %d adding vcpu_hotplug_device\n",
			res);
		goto fail;

	} else {
		printk(KERN_WARNING "vcpu_hotplug_device added\n");
	}

	/* add proc entry: TODO */
	/*
	  create_proc_read_entry("vcpu_hotplug_device", 0 // default mode ,
	  NULL // parent dir , vcpu_hotplug_device_read_procmem,
	  NULL // client data);
	*/

	/* start setting up platform device operations */
	/* get vcpu hotplug device base address */
	vcpu_hotplug_device->phy_base_addr =
		platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!vcpu_hotplug_device->phy_base_addr) {
		printk(KERN_ERR "vcpu_hotplug_device: could not get platform resource.\n");
		res = -EINVAL;
		goto fail;
	}

	/* register io resource to kernel */
	mem_region = request_mem_region(vcpu_hotplug_device->phy_base_addr->start,
					resource_size(vcpu_hotplug_device->phy_base_addr),
					"vcpu_hotplug_dev");
	if (!mem_region) {
		printk(KERN_ERR "vcpu_hotplug_dev: cannot register I/O memory\n");
		res = -EBUSY;
		goto fail;
	}

	vcpu_hotplug_device->virt_base_addr =
		ioremap(VCPU_HOTPLUG_DEVICE_BASE, PAGE_SIZE - 1);
	if (!vcpu_hotplug_device->virt_base_addr) {
		printk(KERN_ERR "vcpu_hotplug_device: ioremap failed.\n");
		res = -EINVAL;
		goto fail;
	}

	/* get vcpu hotplug device irq number and register irq handler */
	vcpu_hotplug_device->irq = platform_get_irq(pdev, 0);
	if (vcpu_hotplug_device->irq < 0) {
		printk(KERN_ERR "vcpu_hotplug_device: failed to get platform IRQ.\n");
		res = vcpu_hotplug_device->irq;
		goto fail;
	}

	printk(KERN_NOTICE "vcpu hotplug dev irq %d\n", vcpu_hotplug_device->irq);

	/* registering non-shared irq line */
	res = request_irq(vcpu_hotplug_device->irq, handle_vcpu_irq, 0,
			"VPCU_IRQ_HANDLER", NULL);
	if (res < 0) {
		printk(KERN_ERR "vcpu_hotplug_dev: cannot register IRQ %d\n",
			vcpu_hotplug_device->irq);
		goto fail;
	}
	/*create kernel thread */
	kthread_run(cpumask_thread, vcpu_hotplug_device, "cpumask_thread");
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

static int __init vcpu_hotplug_driver_init(void)
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

static void __exit vcpu_hotplug_driver_exit(void)
{
	platform_driver_unregister(&vcpu_hotplug_driver);
}

subsys_initcall(vcpu_hotplug_driver_init);
module_exit(vcpu_hotplug_driver_exit);
