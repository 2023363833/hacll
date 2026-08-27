#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include "comm.h"
#include "memory.h"
#include "breakpoint.h"
#include "process.h"

DECLARE_WAIT_QUEUE_HEAD(breakpoint_wait_queue);
atomic_t breakpoint_triggered = ATOMIC_INIT(0);
atomic_t breakpoint_count = ATOMIC_INIT(0);

long dispatch_ioctl(struct file* const file, unsigned int const cmd, unsigned long const arg)
{
    static COPY_MEMORY cm;
    static MODULE_BASE mb;
    static BREAKPOINT_INFO bp_info;
    static REGISTER_INFO reg_info;
    static char name[0x100] = {0};

    switch (cmd) {
        case OP_READ_MEM:
            if (copy_from_user(&cm, (void __user*)arg, sizeof(cm)) != 0) {
                return -EFAULT;
            }
            if (read_process_memory(cm.pid, cm.addr, cm.buffer, cm.size) == 0) {
                return -EINVAL;
            }
            break;
            
        case OP_WRITE_MEM:
            if (copy_from_user(&cm, (void __user*)arg, sizeof(cm)) != 0) {
                return -EFAULT;
            }
            if (write_process_memory(cm.pid, cm.addr, cm.buffer, cm.size) == 0) {
                return -EINVAL;
            }
            break;
            
        case OP_MODULE_BASE:
            if (copy_from_user(&mb, (void __user*)arg, sizeof(mb)) != 0 ||
                copy_from_user(name, (void __user*)mb.name, sizeof(name)-1) != 0) {
                return -EFAULT;
            }
            mb.base = get_module_base(mb.pid, name);
            if (copy_to_user((void __user*)arg, &mb, sizeof(mb)) != 0) {
                return -EFAULT;
            }
            break;
            
        case OP_SET_BREAKPOINT:
            if (copy_from_user(&bp_info, (void __user*)arg, sizeof(bp_info)) != 0) {
                return -EFAULT;
            }
            if (register_breakpoint(&bp_info, NULL) != 0) {
                return -EINVAL;
            }
            break;
            
        case OP_SET_BREAKPOINT_REG:
            if (copy_from_user(&reg_info, (void __user*)arg, sizeof(reg_info)) != 0) {
                return -EFAULT;
            }
            
            bp_info.pid = reg_info.pid;
            bp_info.addr = reg_info.addr;
            bp_info.type = reg_info.type;
            bp_info.len = reg_info.len;
            bp_info.buffer = NULL;
            bp_info.size = 0;
            bp_info.print_regs = reg_info.print_regs;
            
            if (register_breakpoint(&bp_info, &reg_info) != 0) {
                return -EINVAL;
            }
            break;
            
        default:
            return -ENOTTY;
    }
    return 0;
}

static unsigned int dispatch_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    poll_wait(file, &breakpoint_wait_queue, wait);
    if (atomic_read(&breakpoint_triggered)) {
        mask |= POLLIN | POLLRDNORM;
        atomic_set(&breakpoint_triggered, 0);
    }
    return mask;
}

static ssize_t dispatch_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    char status[64];
    int len;
    if (atomic_read(&breakpoint_triggered)) {
        atomic_set(&breakpoint_triggered, 0);
        len = snprintf(status, sizeof(status), "breakpoint triggered\n");
        if (copy_to_user(buf, status, len)) {
            return -EFAULT;
        }
        return len;
    }
    return 0;
}

struct file_operations dispatch_functions = {
    .owner = THIS_MODULE,
    .open = dispatch_open,
    .release = dispatch_close,
    .unlocked_ioctl = dispatch_ioctl,
    .poll = dispatch_poll,
    .read = dispatch_read,
};

struct mem_tool_device {
    struct cdev cdev;
    struct device *dev;
};

static struct mem_tool_device *memdev;
static dev_t mem_tool_dev_t;
static struct class *mem_tool_class;
const char *devicename;

int dispatch_open(struct inode *node, struct file *file)
{
    file->private_data = memdev;
    device_destroy(mem_tool_class, mem_tool_dev_t);
    class_destroy(mem_tool_class);
    printk(KERN_INFO "[memtool] 设备打开成功\n");
    return 0;
}

int dispatch_close(struct inode *node, struct file *file)
{
    mem_tool_class = class_create(THIS_MODULE, devicename);
    memdev->dev = device_create(mem_tool_class, NULL, mem_tool_dev_t, NULL, "%s", devicename);
    printk(KERN_INFO "[memtool] 设备关闭成功\n");
    return 0;
}

static int __init driver_entry(void)
{
    int ret;
    
    devicename = DEVICE_NAME;
    devicename = get_rand_str();

    ret = alloc_chrdev_region(&mem_tool_dev_t, 0, 1, devicename);
    if (ret < 0) {
        printk(KERN_ERR "[memtool] 设备编号分配失败: %d\n", ret);
        return ret;
    }

    memdev = kmalloc(sizeof(struct mem_tool_device), GFP_KERNEL);
    if (!memdev) {
        ret = -ENOMEM;
        goto err_alloc;
    }
    memset(memdev, 0, sizeof(struct mem_tool_device));

    cdev_init(&memdev->cdev, &dispatch_functions);
    memdev->cdev.owner = THIS_MODULE;
    memdev->cdev.ops = &dispatch_functions;

    ret = cdev_add(&memdev->cdev, mem_tool_dev_t, 1);
    if (ret) {
        goto err_cdev;
    }

    mem_tool_class = class_create(THIS_MODULE, devicename);
    if (IS_ERR(mem_tool_class)) {
        ret = PTR_ERR(mem_tool_class);
        goto err_class;
    }
    
    memdev->dev = device_create(mem_tool_class, NULL, mem_tool_dev_t, NULL, "%s", devicename);
    if (IS_ERR(memdev->dev)) {
        ret = PTR_ERR(memdev->dev);
        goto err_device;
    }

    if (!IS_ERR(filp_open("/proc/sched_debug", O_RDONLY, 0))) {
        remove_proc_subtree("sched_debug", NULL);
    }
    
    unregister_chrdev_region(mem_tool_dev_t, 1);

    printk(KERN_INFO "[memtool] 设备创建成功: %s\n", devicename);
    return 0;

err_device:
    class_destroy(mem_tool_class);
err_class:
    cdev_del(&memdev->cdev);
err_cdev:
    kfree(memdev);
err_alloc:
    unregister_chrdev_region(mem_tool_dev_t, 1);
    return ret;
}

static void __exit driver_unload(void)
{
    device_destroy(mem_tool_class, mem_tool_dev_t);
    class_destroy(mem_tool_class);
    cdev_del(&memdev->cdev);
    kfree(memdev);
    unregister_chrdev_region(mem_tool_dev_t, 1);
    printk(KERN_INFO "[memtool] 设备删除成功: %s\n", devicename);
}

module_init(driver_entry);
module_exit(driver_unload);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("散人驱动");
MODULE_DESCRIPTION("Memory tool for Kernel 4.14.186 MT6893");
