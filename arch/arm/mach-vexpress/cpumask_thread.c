#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include "vcpu_hotplug_dev.h"

int cpumask_flag = 0;
DECLARE_WAIT_QUEUE_HEAD(kthread_wq);

unsigned int read_req_cpumask_sz(struct vcpu_hotplug_dev *vcpu_hp_dev) {
	unsigned int req_mask;
	req_mask = ioread8(vcpu_hp_dev->virt_base_addr + VCPU_HP_HEADER_MASK_SZ);
	return req_mask;
}

char* read_req_cpumask(struct vcpu_hotplug_dev *vcpu_hp_dev) {
	int req_cpumask_sz;
	char *buffer;
	int i = 0;
	req_cpumask_sz = read_req_cpumask_sz(vcpu_hp_dev);
	buffer =
		kmalloc(req_cpumask_sz + 1, GFP_KERNEL);
	if (buffer == NULL) {
		return NULL;
	}
	while(i < req_cpumask_sz) {
		buffer[i] = ioread8(vcpu_hp_dev->virt_base_addr + VCPU_HP_HEADER_N + i);
		i++;
	}
	buffer[i] = '\0'; 
	return buffer;
}

int print_requested_cpumask(char *print_buf) {
	int i = 0;
	printk(KERN_NOTICE "requested cpumask: ");
	while(print_buf[i] != '\0')  {	
		printk(KERN_NOTICE "%02X", (int)print_buf[i]);
		i++;
	}
	printk(KERN_NOTICE "\n");
	return 0;
}


int modify_cpumask(struct vcpu_hotplug_dev *vcpu_hp_dev) {
	char *req_cpumask;
	unsigned int req_cpumask_sz;
	struct cpumask *online_mask = NULL;
	struct cpumask *possible_mask = NULL;
	unsigned char *bytes = NULL;
	unsigned int *online_cpumask_bits;
	unsigned int *possible_cpumask_bits;
	unsigned int req_mask = 0;
	unsigned int byte_mask = 0;
	unsigned int byte_counter = 0;
	unsigned int request = 0;
	unsigned int online = 0;
	unsigned int i = 0;
	unsigned int max_iter = 0;

	
	req_cpumask = read_req_cpumask(vcpu_hp_dev);
	if(req_cpumask == NULL)
		return  -ENOMEM;
	req_cpumask_sz = read_req_cpumask_sz(vcpu_hp_dev); 
	online_mask = kmalloc(sizeof(struct cpumask), GFP_KERNEL);
        possible_mask = kmalloc(sizeof(struct cpumask), GFP_KERNEL);
	cpumask_copy(online_mask,cpu_online_mask);
	cpumask_copy(possible_mask, cpu_possible_mask);
 	online_cpumask_bits = (unsigned int *)cpumask_bits(online_mask);
	possible_cpumask_bits = (unsigned int *)cpumask_bits(possible_mask);

	max_iter = min((unsigned int)nr_cpumask_bits, req_cpumask_sz * 8);
	printk(KERN_NOTICE "max iter %d\n", max_iter);
	bytes = kmalloc(max_iter, GFP_KERNEL);
	while(i < max_iter) {
		if(!cpumask_test_cpu(i, cpu_possible_mask))
			continue;
		req_mask = 1 << i;
		byte_mask = (1 << (i % 8));
		online = *online_cpumask_bits & req_mask ? 1 : 0;
		if((i > 0) && ((i % 8) == 0)) {
			byte_counter++;
			byte_mask = 1;
		}
		request = req_cpumask[byte_counter] & byte_mask ? 1 : 0;
		if(!online && request) {
			cpu_up(i);
		} else if (online && !request) {
			cpu_down(i);
		}
	i++;
	} 
	printk(KERN_NOTICE "Writing online cpumask back to Qemu\n");
	cpumask_copy(online_mask,cpu_online_mask);
	online_cpumask_bits = (unsigned int *)cpumask_bits(online_mask);
	printk(KERN_NOTICE "cpumask online bits %u\n", *online_cpumask_bits);
	for(i = 0; i < max_iter; i++) {
		bytes[i] = (*online_cpumask_bits >> (i * 8)) & 0xFF;
  		printk(KERN_NOTICE "bytes %d %u", i, bytes[i]);
	}
	bytes[i] = *online_cpumask_bits & 0xFF;
	printk(KERN_NOTICE "bytes %d %x", i, (unsigned char)bytes[i]);
	

	kfree(req_cpumask);
	kfree(online_mask);
	kfree(possible_mask);
	kfree(bytes);
	return 0;
	
}	



int cpumask_thread(void *data) {

	struct vcpu_hotplug_dev *vcpu_hp_dev = (struct vcpu_hotplug_dev *)data;
	wait_queue_t thread_wait;

	while(1) {

	/* creates and init wait queue entry
	* attach current thread into entry */
	init_wait(&thread_wait);

	/* allow devivery of SIGKILL */
	allow_signal(SIGKILL);

	/* adds current thread to thread_wq and set process state
	* TSK_INTERRUPTIBLE */
	/* exclusive flag start just one thread (others sleep) in time
	* in orderly manner */
	prepare_to_wait_exclusive(&kthread_wq, &thread_wait, TASK_INTERRUPTIBLE);
	if(!cpumask_flag)
		schedule();   /*in schedule() return change thread state TASK_RUNNING */

	/*removes current thread from thread wait_wq */
	finish_wait(&kthread_wq, &thread_wait);
	if(signal_pending(current))
		return -ERESTARTSYS;
	cpumask_flag = 0;
	printk(KERN_NOTICE "cpumask thread running\n");
	modify_cpumask(vcpu_hp_dev);
	}
	return 0;
}



