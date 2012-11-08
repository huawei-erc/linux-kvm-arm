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

static struct vcpu_hotplug_dev *vcpu_hotplug_device;
static dev_t dev;

/*******************************************************
* print all cpu masks
*******************************************************/

int print_masks(char *buffer)
{
	char *cpumasks_text[] = {
		"cpu_possible_mask",
		"cpu_online_mask",
		"cpu_present_mask",
		"cpu_active_mask",
		NULL
	};
	const struct cpumask *cpumasks[] = {
		cpu_possible_mask,
		cpu_online_mask,
		cpu_present_mask,
		cpu_active_mask
	};
	int i = 0;

	while (cpumasks_text[i] != NULL) {
		if (cpulist_scnprintf(buffer, PAGE_SIZE, cpumasks[i]) != 0)
			printk(KERN_NOTICE "%s %s\n", cpumasks_text[i], buffer);
		else
			printk(KERN_NOTICE "%s print error\n",
			       cpumasks_text[i]);
		i++;
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
* fileops write()
*********************************************************************/
__cpuinit ssize_t vcpu_hotplug_device_write(struct file * filp,
			const char __user * buf, size_t count, loff_t * f_pos)
{
	struct vcpu_hotplug_dev *dev = filp->private_data;
	char *buffer;
	int retval = 0;

	if (down_interruptible(&dev->sem)) {
		printk(KERN_WARNING "semaphore error\n");
		return -ERESTARTSYS;
	}

	/* allocate one page size buffer */
	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);

	/* copy from user space */
	if (copy_from_user(buffer, buf, count)) {
		retval = -EFAULT;
	}

	*f_pos += count;

	printk(KERN_NOTICE "received %s from user\n", buffer);

	/* clean the buffer */
	memset(buffer, 0, PAGE_SIZE);

	/* print initial cpu mask setup */
	printk(KERN_NOTICE "original masks\n");
	print_masks(buffer);

	/* test cpumask operations */
	test_cpumask();

	kfree(buffer);
	up(&dev->sem);
	printk(KERN_WARNING "leaving fileops write\n");
	return retval;
}

/********************************************************************************************
 *  fileops read() function
 ********************************************************************************************/
ssize_t vcpu_hotplug_device_read(struct file * filp, char __user * buf,
				 size_t count, loff_t * f_pos)
{
	struct vcpu_hotplug_dev *dev = filp->private_data;
	int value;
	void __iomem *io_address = __io_address(VCPU_HOTPLUG_DEVICE_BASE);

	/* Request unique access */
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	printk(KERN_NOTICE
		"phy_base_addr: %x, virt_base_addr(io_address): %p virt_base_addr_ioremap %p\n",
		(int)dev->phy_base_addr,
		(int *)io_address,
		(int *)dev->virt_base_addr);

	value = ioread32(dev->virt_base_addr + 0x4);
	printk(KERN_WARNING "device s->level value %x", value);

	up(&dev->sem);
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
	.write = vcpu_hotplug_device_write,
	.open = vcpu_hotplug_device_open,
	.release = vcpu_hotplug_device_release,
};

/*********************************************************
 * platform device remove()
 *********************************************************/

static int __devexit vcpu_hotplug_device_remove(struct platform_device *pdev)
{
	/* release IO mem reservation */
	printk(KERN_NOTICE "releasing I/O memory\n");
	release_mem_region(vcpu_hotplug_device->phy_base_addr->start,
			resource_size(vcpu_hotplug_device->phy_base_addr));

	/* free vcpu_hotplug device */
	if (vcpu_hotplug_device)
		cdev_del(&vcpu_hotplug_device->cdev);

	/* free vcpu_hotplug_device menory */
	kfree(vcpu_hotplug_device);

	unregister_chrdev_region(dev, VCPU_HOTPLUG_DEVICE_COUNT);

	/* free irq line */
	free_irq(platform_get_irq(pdev, 0), NULL);

	return 0;
}

/*******************************************************
* handle_vcpu_irq();
*******************************************************/
static irqreturn_t handle_vcpu_irq(int irq, void *opaque)
{
	irqreturn_t ret = IRQ_NONE;
	static int i = 0;
	//NOTE: remove this, not safe
	printk(KERN_ALERT "Entered to IRQ handler %d\n", i++);
	ret = IRQ_HANDLED;

	return ret;
}

/********************************************************
 * platform driver probe() function
 *******************************************************/
static int __devinit vcpu_hotplug_device_probe(struct platform_device *pdev)
{
	/* start of setting up file operations */
	/* allocate device number */
	int res = alloc_chrdev_region(&dev, VCPU_HOTPLUG_DEVICE_MINOR_START,
				VCPU_HOTPLUG_DEVICE_COUNT,
				VCPU_HOTPLUG_DEVICE_NAME);
	if (res) {
		printk(KERN_WARNING "could not allocate device\n");
		return res;

	} else {
		printk(KERN_WARNING "registered with major number:%i\n",
			MAJOR(dev));
	}

	/* allocate memory for vcpu_hotplug_device */

	vcpu_hotplug_device =
		kmalloc(sizeof(struct vcpu_hotplug_dev), GFP_KERNEL);

	if (vcpu_hotplug_device == NULL) {
		res = -ENOMEM;
		goto fail;
	}

	/* Fill the vcpu_hotplug_device region with zeros */
	memset(vcpu_hotplug_device, 0, sizeof(struct vcpu_hotplug_dev));
	sema_init(&vcpu_hotplug_device->sem, 1);
	cdev_init(&vcpu_hotplug_device->cdev, &vcpu_hotplug_device_fops);
	vcpu_hotplug_device->cdev.owner = NULL;
	vcpu_hotplug_device->cdev.ops = &vcpu_hotplug_device_fops;
	res = cdev_add(&vcpu_hotplug_device->cdev, MKDEV(MAJOR(dev), MINOR(dev)), 1);

	if (res) {
		printk(KERN_WARNING "Error %d adding vcpu_hotplug_device\n",
			res);
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
	if (!vcpu_hotplug_device->phy_base_addr)
		return -EINVAL;

	/* register io resource to kernel */
	if (!request_mem_region
	    (vcpu_hotplug_device->phy_base_addr->start,
	     resource_size(vcpu_hotplug_device->phy_base_addr),
	     "vcpu_hotplug_dev")) {
		printk(KERN_ERR
		       "vcpu_hotplug_dev: cannot register I/O memory\n");
		return -EBUSY;
	}

	vcpu_hotplug_device->virt_base_addr =
	    ioremap(VCPU_HOTPLUG_DEVICE_BASE, PAGE_SIZE - 1);

	/* get vcpu hotplug device irq number and register irq handler */
	vcpu_hotplug_device->irq = platform_get_irq(pdev, 0);
	printk(KERN_NOTICE "vcpu hotplug dev irq %d\n",
	       vcpu_hotplug_device->irq);

	/* registering non-shared irq line */
	if (request_irq(vcpu_hotplug_device->irq, handle_vcpu_irq, 0,
			"VPCU_IRQ_HANDLER", NULL)) {
		printk(KERN_ERR "vcpu_hotplug_dev: cannot register IRQ %d\n",
			vcpu_hotplug_device->irq);
		return -EIO;
	}

	return 0;

fail:
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
