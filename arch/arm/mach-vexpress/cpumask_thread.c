#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/cdev.h>
#include "vcpu_hotplug_dev.h"

int cpumask_flag = 0;
DECLARE_WAIT_QUEUE_HEAD(kthread_wq);

int print_requested_cpumask(struct vcpu_hotplug_dev *vcpu_hp_dev) {

	unsigned long *request_cpumask;
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
	print_requested_cpumask(vcpu_hp_dev);
	}
	return 0;
}



