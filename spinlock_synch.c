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

struct thread_node
{
	struct list_head node;
	struct task_struct* thread;
};

static LIST_HEAD(threads_list);
static struct thread_node * kthread_arr;

/* Global resource */
static u64 global_var = 0;
static DEFINE_MUTEX(gl_var_mutex);


/* Array to save if thread has finished */
static u8 *threads_status;

int thread_func(void *data)
{
	int i;
	int N = 10;
	u32 thr_num = (typeof(N))data;
	struct task_struct* cur = current;

	if (thr_num < N)
		N = thr_num;

	msleep(10);

	char * name = cur->comm;

	for (i = 0; i < N; i++) {
		pr_debug(	"%s, GV: %llu\n",
				name,
				(unsigned long long) global_var);

		mutex_lock(&gl_var_mutex);
		WRITE_ONCE(global_var, READ_ONCE(global_var) + 1);
		mutex_unlock(&gl_var_mutex);

		if (unlikely(kthread_should_stop()))
			return 1;

		msleep(1000);
	}

	threads_status[thr_num] = 1;
	return 0;
}

static int init_thread_list(s32 N)
{
	typeof(N) i;

	kthread_arr = kmalloc_array(N, sizeof(*kthread_arr), GFP_KERNEL);

	if (unlikely(!kthread_arr))
		return -ENOMEM;

	for (i = 0; i < N; i++) {
		list_add(&kthread_arr[i].node, &threads_list);

		kthread_arr[i].thread = kthread_create(	&thread_func,
							(void *) i,
							"Thread %lu",
							(unsigned long)i);

		if (unlikely(IS_ERR(kthread_arr[i].thread)))
			goto err_kthread_create;
	}

	return 0;

err_kthread_create:
	i--;
	for (;i >= 0; i--)
		 kthread_stop(kthread_arr[i].thread);

	kfree(kthread_arr);

	return -ENOMEM;
}


static int __init mut_syn_init(void)
{
	int rc;
	struct thread_node *node;

	threads_status = kzalloc(sizeof(*threads_status) * N_thr, GFP_KERNEL);
	if (unlikely(!threads_status))
		return -ENOMEM;

	/* Init threads */
	rc = init_thread_list(N_thr);
	if (unlikely(rc))
		goto err_init_thread;


	/* Run threads */
	list_for_each_entry(node, &threads_list, node) {
		wake_up_process(node->thread);
	}

	return 0;

err_init_thread:
	kfree(threads_status);
	return rc;
}

static void __exit mut_syn_exit(void)
{
	struct thread_node *node;
	int i = 0;

	list_for_each_entry_reverse(node, &threads_list, node) {
		if (threads_status[i++] == 0)
			kthread_stop(node->thread);
	}
	pr_debug("Finished\n");
	kfree(kthread_arr);
}

module_init(mut_syn_init);
module_exit(mut_syn_exit);
