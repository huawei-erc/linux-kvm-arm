#include <linux/kthread.h>
#include <linux/wait.h>

extern int cpumask_flag;
extern wait_queue_head_t kthread_wq;

int cpumask_thread(void *data);
