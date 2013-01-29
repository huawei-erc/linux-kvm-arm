#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include "vcpu_hotplug_dev.h"

#undef pr_fmt
#define pr_fmt(fmt) "cpumask_thread: " fmt ".\n"

int cpumask_flag = 0;
DECLARE_WAIT_QUEUE_HEAD(cpumask_wq);

#define MASK_SZ_MAX 0xff

static unsigned int read_mask_sz(struct vcpu_hotplug_dev *vcpu_hp_dev)
{
	return ioread8(vcpu_hp_dev->virt_base_addr + VCPU_HP_HEADER_MASK_SZ);
}

static void read_req_mask(struct vcpu_hotplug_dev *vcpu_hp_dev,
			unsigned char *mask, unsigned int mask_sz)
{
	unsigned int i;
	for (i = 0; i < mask_sz; i++)
		mask[i] = ioread8(vcpu_hp_dev->virt_base_addr
				+ VCPU_HP_HEADER_N + i);
	mask[mask_sz] = 0xAA; /* end with something noticeable for testing. */
}

static void write_resp_mask(struct vcpu_hotplug_dev *vcpu_hp_dev,
			unsigned char *mask, unsigned int mask_sz)
{
	unsigned int i; unsigned long ctrl;

	for (i = 0; i < mask_sz; i++) {
		iowrite8(mask[i], vcpu_hp_dev->virt_base_addr
			+ VCPU_HP_HEADER_N + mask_sz + i);
	}

	/* read control byte */
	ctrl = ioread8(vcpu_hp_dev->virt_base_addr + VCPU_HP_HEADER_CTRL);
	/* clear HPR */
	clear_bit(VCPU_HP_CTRL_HPR, &ctrl);
	/* rewrite ctrl byte */
	iowrite8(ctrl, vcpu_hp_dev->virt_base_addr + VCPU_HP_HEADER_CTRL);
}

static void print_mask(unsigned char *mask, unsigned int n)
{
	pr_notice("vcpu mask: %*phC\n", n, mask);
}

__cpuinit void modify_cpumask(struct vcpu_hotplug_dev *vcpu_hp_dev)
{
	unsigned char vcpu_mask[MASK_SZ_MAX + 1] = { 0 };
	unsigned int vcpu_mask_sz, i;
	unsigned int vcpu_mask_bits;

	vcpu_mask_sz = read_mask_sz(vcpu_hp_dev);
	vcpu_mask_bits = vcpu_mask_sz * 8;

	read_req_mask(vcpu_hp_dev, vcpu_mask, vcpu_mask_sz);

	/* always keep CPU0 online */
	vcpu_mask[0] |= 0x01;

	for (i = 1; i < vcpu_mask_bits; i++) {
		unsigned int off_byte, off_bit;
		off_byte = i / 8; off_bit = i % 8;

		if (i >= nr_cpumask_bits || !cpu_possible(i)) {
			vcpu_mask[off_byte] &= ~(1 << off_bit);
			continue;
		}

		/* If VCPU is requested to be online, and it's not, online it.
		   If VCPU is requested to be offline,and it's not, offline it. */

		if (vcpu_mask[off_byte] & (1 << off_bit)) {
			pr_notice("VCPU%d requested online", i);
			if (!cpu_online(i)) {
				pr_notice("VCPU%d not online: onlining..", i);
				if (cpu_up(i) != 0) {
					pr_notice("VCPU%d ...cpu_up failed", i);
					vcpu_mask[off_byte] &= ~(1 << off_bit);
				} else {
					pr_notice("VCPU%d ...cpu_up complete", i);
				}
			} else {
				pr_notice("VCPU%d already online", i);
			}
		} else {
			pr_notice("VCPU%d requested offline", i);
			if (cpu_online(i)) {
				pr_notice("VCPU%d is online: offlining..", i);
				if (cpu_down(i) != 0) {
					pr_notice("VCPU%d ...cpu_down failed", i);
					vcpu_mask[off_byte] |= (1 << off_bit);
				} else {
					pr_notice("VCPU%d ...cpu_down complete", i);
				}
			} else {
				pr_notice("VCPU%d already offline", i);
			}
		}
	}

	print_mask(vcpu_mask, vcpu_mask_sz);
	pr_notice("writing online cpumask response back to Qemu");

	write_resp_mask(vcpu_hp_dev, vcpu_mask, vcpu_mask_sz);
}

__cpuinit int cpumask_thread(void *data)
{
	struct vcpu_hotplug_dev *vcpu_hp_dev = data;

	while (!kthread_should_stop()) {
		int ret;

		allow_signal(SIGKILL);

		ret = wait_event_interruptible(cpumask_wq, cpumask_flag != 0 ||
					       kthread_should_stop());
		if (kthread_should_stop())
			break;
		if (ret < 0) {
			pr_warning("received signal, thread exiting");
			return ret;
		}

                cpumask_flag = 0;
                pr_notice("cpumask thread running");
                modify_cpumask(vcpu_hp_dev);
	}

	return 0;
}
