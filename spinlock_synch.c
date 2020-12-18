// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/wait.h>

MODULE_AUTHOR("Viaheslav Lykhohub <viacheslav.lykhohub@globallogic.com>");
MODULE_DESCRIPTION("Demonstration of synchronization using mutexes");
MODULE_LICENSE("GPL");

static u32 N_thr = 5;
module_param(N_thr, int, 0444);

enum thread_status_enum {
	THREAD_INIT = 0,
	THREAD_RUNNING,
	THREAD_STOPPED,
};

struct thread_node
{
	struct list_head node;
	struct task_struct* thread;
	u32 num;
	u8 status;
};

static LIST_HEAD(threads_list);
static struct thread_node * kthread_arr;

/* Global resource */
static u64 global_var = 0;
static DEFINE_MUTEX(gl_var_mutex);

int thread_func(void *data)
{
	s32 i;
	struct task_struct* cur = current;
	struct thread_node *self_node = data;
	char * name = cur->comm;

	msleep(10);


	for (i = 0; i < self_node->num; i++) {
		pr_debug("%s, GV: %llu\n", name,
			 (unsigned long long) global_var);

		mutex_lock(&gl_var_mutex);
		WRITE_ONCE(global_var, READ_ONCE(global_var) + 1);
		mutex_unlock(&gl_var_mutex);

		if (unlikely(kthread_should_stop()))
			return 1;

		msleep(1000);
	}

	self_node->status = THREAD_STOPPED;
	return 0;
}

static int __init init_thread_list(s32 N)
{
	typeof(N) i;

	kthread_arr = kmalloc_array(N, sizeof(*kthread_arr), GFP_KERNEL);

	if (unlikely(!kthread_arr))
		return -ENOMEM;

	for (i = 0; i < N; i++) {
		kthread_arr[i].thread = kthread_create(&thread_func,
						       &kthread_arr[i],
						       "Thread %d", (int)i);
		if (unlikely(IS_ERR(kthread_arr[i].thread)))
			goto err_kthread_create;

		list_add(&kthread_arr[i].node, &threads_list);
		kthread_arr[i].num = i;
		kthread_arr[i].status = THREAD_INIT;
	}

	return 0;

err_kthread_create:
	i--;
	for (;i >= 0; i--)
		 kthread_stop(kthread_arr[i].thread);

	kfree(kthread_arr);

	return -ENOMEM;
}

static int __init mutex_synchronization_init(void)
{
	int rc;
	struct thread_node *node_i;

	/* Init threads */
	rc = init_thread_list(N_thr);
	if (unlikely(rc))
		goto err_init_thread;


	/* Run threads */
	list_for_each_entry(node_i, &threads_list, node) {
		node_i->status = THREAD_RUNNING;
		wake_up_process(node_i->thread);
	}

	return 0;

err_init_thread:
	return rc;
}

static void __exit mutex_synchronization_exit(void)
{
	struct thread_node *node_i;

	list_for_each_entry_reverse(node_i, &threads_list, node) {
		if (node_i->status == 0)
			kthread_stop(node_i->thread);
	}
	pr_debug("Finished\n");
}

module_init(mutex_synchronization_init);
module_exit(mutex_synchronization_exit);
