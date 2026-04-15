#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include "monitor_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author");
MODULE_DESCRIPTION("Memory Monitor Kernel Module");

struct container_node {
    int pid;
    unsigned long soft_limit_mb;
    unsigned long hard_limit_mb;
    int warned;
    struct list_head list;
};

static LIST_HEAD(container_list);
static DEFINE_MUTEX(list_mutex);
static struct task_struct *monitor_thread;

static long monitor_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct container_config cfg;
    struct container_node *node, *tmp;
    pid_t unreg_pid;
    int found = 0;

    switch (cmd) {
        case MONITOR_REGISTER:
            if (copy_from_user(&cfg, (struct container_config __user *)arg, sizeof(cfg))) {
                return -EFAULT;
            }
            node = kmalloc(sizeof(*node), GFP_KERNEL);
            if (!node) return -ENOMEM;
            node->pid = cfg.pid;
            node->soft_limit_mb = cfg.soft_limit_mb;
            node->hard_limit_mb = cfg.hard_limit_mb;
            node->warned = 0;
            
            mutex_lock(&list_mutex);
            list_add(&node->list, &container_list);
            mutex_unlock(&list_mutex);
            pr_info("monitor: registered pid %d\n", node->pid);
            break;

        case MONITOR_UNREGISTER:
            if (copy_from_user(&unreg_pid, (pid_t __user *)arg, sizeof(unreg_pid))) {
                return -EFAULT;
            }
            mutex_lock(&list_mutex);
            list_for_each_entry_safe(node, tmp, &container_list, list) {
                if (node->pid == unreg_pid) {
                    list_del(&node->list);
                    kfree(node);
                    found = 1;
                    break;
                }
            }
            mutex_unlock(&list_mutex);
            if (found) {
                pr_info("monitor: unregistered pid %d\n", unreg_pid);
            }
            break;

        default:
            return -ENOTTY;
    }
    return 0;
}

static const struct file_operations monitor_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = monitor_ioctl,
};

static struct miscdevice monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "container_monitor",
    .fops = &monitor_fops,
    .mode = 0666,
};

static int monitor_kthread_fn(void *data) {
    struct container_node *node, *tmp;
    struct task_struct *task;
    struct pid *pid_struct;
    unsigned long rss_mb;

    while (!kthread_should_stop()) {
        msleep(1000);

        mutex_lock(&list_mutex);
        list_for_each_entry_safe(node, tmp, &container_list, list) {
            pid_struct = find_get_pid(node->pid);
            if (!pid_struct) {
                list_del(&node->list);
                kfree(node);
                continue;
            }
            
            task = get_pid_task(pid_struct, PIDTYPE_PID);
            put_pid(pid_struct);
            
            if (!task) {
                list_del(&node->list);
                kfree(node);
                continue;
            }

            if (task->mm) {
                rss_mb = (get_mm_rss(task->mm) * PAGE_SIZE) / 1048576;
                
                if (rss_mb > node->hard_limit_mb) {
                    pr_alert("monitor: PID %d exceeded hard limit %lu MB (RSS: %lu MB). Sending SIGKILL.\n", node->pid, node->hard_limit_mb, rss_mb);
                    send_sig(SIGKILL, task, 0);
                } else if (rss_mb > node->soft_limit_mb && !node->warned) {
                    pr_warn("monitor: PID %d exceeded soft limit %lu MB (RSS: %lu MB).\n", node->pid, node->soft_limit_mb, rss_mb);
                    node->warned = 1;
                }
            }
            put_task_struct(task);
        }
        mutex_unlock(&list_mutex);
    }
    return 0;
}

static int __init monitor_init(void) {
    int ret;
    ret = misc_register(&monitor_dev);
    if (ret) {
        pr_err("monitor: failed to register misc device\n");
        return ret;
    }
    
    monitor_thread = kthread_run(monitor_kthread_fn, NULL, "monitor_thread");
    if (IS_ERR(monitor_thread)) {
        misc_deregister(&monitor_dev);
        pr_err("monitor: failed to start kthread\n");
        return PTR_ERR(monitor_thread);
    }
    
    pr_info("monitor: module loaded\n");
    return 0;
}

static void __exit monitor_exit(void) {
    struct container_node *node, *tmp;

    if (monitor_thread) {
        kthread_stop(monitor_thread);
    }

    mutex_lock(&list_mutex);
    list_for_each_entry_safe(node, tmp, &container_list, list) {
        list_del(&node->list);
        kfree(node);
    }
    mutex_unlock(&list_mutex);

    misc_deregister(&monitor_dev);
    pr_info("monitor: module unloaded\n");
}

module_init(monitor_init);
module_exit(monitor_exit);
