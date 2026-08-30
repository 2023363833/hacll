
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#include "comm.h"
#include "memory.h"
#include "breakpoint.h"
#include "process.h"

/*
 * 断点事件通知。
 *
 * breakpoint_triggered 保留给旧代码兼容使用，
 * breakpoint_count 用于可靠地记录待处理事件数量。
 */
DECLARE_WAIT_QUEUE_HEAD(breakpoint_wait_queue);

atomic_t breakpoint_triggered = ATOMIC_INIT(0);
atomic_t breakpoint_count = ATOMIC_INIT(0);

struct mem_tool_device {
    struct cdev cdev;
    struct device *dev;
};

static struct mem_tool_device *memdev;
static dev_t mem_tool_dev_t;
static struct class *mem_tool_class;
const char *devicename;

/* 函数前置声明 */
int dispatch_open(struct inode *node, struct file *file);
int dispatch_close(struct inode *node, struct file *file);


/*
 * ioctl 请求处理
 */
long dispatch_ioctl(
    struct file * const file,
    unsigned int const cmd,
    unsigned long const arg
)
{
    COPY_MEMORY cm;
    MODULE_BASE mb;
    BREAKPOINT_INFO bp_info;
    REGISTER_INFO reg_info;
    char name[0x100];

    memset(&cm, 0, sizeof(cm));
    memset(&mb, 0, sizeof(mb));
    memset(&bp_info, 0, sizeof(bp_info));
    memset(&reg_info, 0, sizeof(reg_info));
    memset(name, 0, sizeof(name));

    switch (cmd) {

    case OP_READ_MEM:

        if (copy_from_user(
                &cm,
                (void __user *)arg,
                sizeof(cm)
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] OP_READ_MEM copy_from_user failed\n");

            return -EFAULT;
        }

        if (read_process_memory(
                cm.pid,
                cm.addr,
                cm.buffer,
                cm.size
            ) == 0) {

            printk(KERN_ERR
                   "[memtool] OP_READ_MEM failed pid=%d addr=0x%lx\n",
                   cm.pid,
                   (unsigned long)cm.addr);

            return -EINVAL;
        }

        break;


    case OP_WRITE_MEM:

        if (copy_from_user(
                &cm,
                (void __user *)arg,
                sizeof(cm)
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] OP_WRITE_MEM copy_from_user failed\n");

            return -EFAULT;
        }

        if (write_process_memory(
                cm.pid,
                cm.addr,
                cm.buffer,
                cm.size
            ) == 0) {

            printk(KERN_ERR
                   "[memtool] OP_WRITE_MEM failed pid=%d addr=0x%lx\n",
                   cm.pid,
                   (unsigned long)cm.addr);

            return -EINVAL;
        }

        break;


    case OP_MODULE_BASE:

        if (copy_from_user(
                &mb,
                (void __user *)arg,
                sizeof(mb)
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] OP_MODULE_BASE structure copy failed\n");

            return -EFAULT;
        }

        if (copy_from_user(
                name,
                (void __user *)mb.name,
                sizeof(name) - 1
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] OP_MODULE_BASE name copy failed\n");

            return -EFAULT;
        }

        name[sizeof(name) - 1] = '\0';

        mb.base = get_module_base(
            mb.pid,
            name
        );

        if (copy_to_user(
                (void __user *)arg,
                &mb,
                sizeof(mb)
            ) != 0) {

            return -EFAULT;
        }

        break;


    case OP_SET_BREAKPOINT:

        if (copy_from_user(
                &bp_info,
                (void __user *)arg,
                sizeof(bp_info)
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] breakpoint info copy failed\n");

            return -EFAULT;
        }

        printk(KERN_INFO
               "[memtool] breakpoint request pid=%d addr=0x%lx type=%d len=%d print_regs=%d\n",
               bp_info.pid,
               (unsigned long)bp_info.addr,
               bp_info.type,
               bp_info.len,
               bp_info.print_regs);

        if (register_breakpoint(
                &bp_info,
                NULL
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] breakpoint registration failed\n");

            return -EINVAL;
        }

        printk(KERN_INFO
               "[memtool] breakpoint registration success addr=0x%lx\n",
               (unsigned long)bp_info.addr);

        break;


    case OP_SET_BREAKPOINT_REG:

        if (copy_from_user(
                &reg_info,
                (void __user *)arg,
                sizeof(reg_info)
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] register breakpoint info copy failed\n");

            return -EFAULT;
        }

        memset(
            &bp_info,
            0,
            sizeof(bp_info)
        );

        bp_info.pid =
            reg_info.pid;

        bp_info.addr =
            reg_info.addr;

        bp_info.type =
            reg_info.type;

        bp_info.len =
            reg_info.len;

        bp_info.buffer =
            NULL;

        bp_info.size =
            0;

        bp_info.print_regs =
            reg_info.print_regs;

        printk(KERN_INFO
               "[memtool] register breakpoint request pid=%d addr=0x%lx type=%d len=%d print_regs=%d\n",
               bp_info.pid,
               (unsigned long)bp_info.addr,
               bp_info.type,
               bp_info.len,
               bp_info.print_regs);

        if (register_breakpoint(
                &bp_info,
                &reg_info
            ) != 0) {

            printk(KERN_ERR
                   "[memtool] register breakpoint failed\n");

            return -EINVAL;
        }

        printk(KERN_INFO
               "[memtool] register breakpoint success\n");

        break;


    default:

        printk(KERN_WARNING
               "[memtool] unknown ioctl cmd=0x%x\n",
               cmd);

        return -ENOTTY;
    }

    return 0;
}


/*
 * poll
 *
 * 注意：
 * poll 只报告事件，不消费事件。
 * 真正消费事件由 read() 完成。
 */
static unsigned int dispatch_poll(
    struct file *file,
    poll_table *wait
)
{
    poll_wait(
        file,
        &breakpoint_wait_queue,
        wait
    );

    if (atomic_read(&breakpoint_count) > 0) {

        return POLLIN | POLLRDNORM;
    }

    return 0;
}


/*
 * read
 *
 * 每次 read 消费一个断点事件。
 */
static ssize_t dispatch_read(
    struct file *file,
    char __user *buf,
    size_t size,
    loff_t *offset
)
{
    char status[128];
    int len;
    int count;

    count =
        atomic_read(&breakpoint_count);

    if (count <= 0) {

        return 0;
    }

    len = scnprintf(
        status,
        sizeof(status),
        "breakpoint triggered pending=%d\n",
        count
    );

    if (size < len) {

        return -EINVAL;
    }

    if (copy_to_user(
            buf,
            status,
            len
        )) {

        return -EFAULT;
    }

    atomic_dec_if_positive(
        &breakpoint_count
    );

    if (atomic_read(
            &breakpoint_count
        ) <= 0) {

        atomic_set(
            &breakpoint_triggered,
            0
        );
    }

    printk(KERN_INFO
           "[memtool] breakpoint event delivered remaining=%d\n",
           atomic_read(&breakpoint_count));

    return len;
}


/*
 * 打开设备。
 *
 * 不销毁 class/device。
 */
int dispatch_open(
    struct inode *node,
    struct file *file
)
{
    file->private_data =
        memdev;

    printk(KERN_INFO
           "[memtool] device opened: %s\n",
           devicename);

    return 0;
}


/*
 * 关闭设备。
 *
 * 不重新创建 class/device。
 */
int dispatch_close(
    struct inode *node,
    struct file *file
)
{
    printk(KERN_INFO
           "[memtool] device closed: %s\n",
           devicename);

    return 0;
}


/*
 * file_operations
 */
struct file_operations dispatch_functions = {

    .owner =
        THIS_MODULE,

    .open =
        dispatch_open,

    .release =
        dispatch_close,

    .unlocked_ioctl =
        dispatch_ioctl,

    .poll =
        dispatch_poll,

    .read =
        dispatch_read,
};


/*
 * 驱动初始化
 */
static int __init driver_entry(void)
{
    int ret;

    devicename =
        get_rand_str();

    if (!devicename) {

        printk(KERN_ERR
               "[memtool] random device name failed\n");

        return -ENOMEM;
    }


    /*
     * 分配字符设备号。
     *
     * 注意：
     * 成功后不能在这里 unregister。
     * 必须等模块卸载时释放。
     */
    ret =
        alloc_chrdev_region(
            &mem_tool_dev_t,
            0,
            1,
            devicename
        );

    if (ret < 0) {

        printk(KERN_ERR
               "[memtool] alloc_chrdev_region failed: %d\n",
               ret);

        return ret;
    }


    memdev =
        kzalloc(
            sizeof(*memdev),
            GFP_KERNEL
        );

    if (!memdev) {

        ret =
            -ENOMEM;

        goto err_unregister;
    }


    cdev_init(
        &memdev->cdev,
        &dispatch_functions
    );

    memdev->cdev.owner =
        THIS_MODULE;


    ret =
        cdev_add(
            &memdev->cdev,
            mem_tool_dev_t,
            1
        );

    if (ret) {

        printk(KERN_ERR
               "[memtool] cdev_add failed: %d\n",
               ret);

        goto err_free_memdev;
    }


    mem_tool_class =
        class_create(
            THIS_MODULE,
            devicename
        );

    if (IS_ERR(
            mem_tool_class
        )) {

        ret =
            PTR_ERR(
                mem_tool_class
            );

        mem_tool_class =
            NULL;

        printk(KERN_ERR
               "[memtool] class_create failed: %d\n",
               ret);

        goto err_cdev_del;
    }


    memdev->dev =
        device_create(
            mem_tool_class,
            NULL,
            mem_tool_dev_t,
            NULL,
            "%s",
            devicename
        );

    if (IS_ERR(
            memdev->dev
        )) {

        ret =
            PTR_ERR(
                memdev->dev
            );

        memdev->dev =
            NULL;

        printk(KERN_ERR
               "[memtool] device_create failed: %d\n",
               ret);

        goto err_class_destroy;
    }


    printk(KERN_INFO
           "[memtool] device created successfully\n");

    printk(KERN_INFO
           "[memtool] device=%s major=%d minor=%d\n",
           devicename,
           MAJOR(mem_tool_dev_t),
           MINOR(mem_tool_dev_t));


    return 0;


/* 错误清理 */

err_class_destroy:

    class_destroy(
        mem_tool_class
    );

    mem_tool_class =
        NULL;


err_cdev_del:

    cdev_del(
        &memdev->cdev
    );


err_free_memdev:

    kfree(
        memdev
    );

    memdev =
        NULL;


err_unregister:

    unregister_chrdev_region(
        mem_tool_dev_t,
        1
    );

    return ret;
}


/*
 * 驱动卸载
 */
static void __exit driver_unload(void)
{
    printk(KERN_INFO
           "[memtool] driver unloading\n");


    if (
        memdev &&
        memdev->dev &&
        mem_tool_class
    ) {

        device_destroy(
            mem_tool_class,
            mem_tool_dev_t
        );

        memdev->dev =
            NULL;
    }


    if (mem_tool_class) {

        class_destroy(
            mem_tool_class
        );

        mem_tool_class =
            NULL;
    }


    if (memdev) {

        cdev_del(
            &memdev->cdev
        );

        kfree(
            memdev
        );

        memdev =
            NULL;
    }


    unregister_chrdev_region(
        mem_tool_dev_t,
        1
    );


    printk(KERN_INFO
           "[memtool] device removed: %s\n",
           devicename);
}


module_init(driver_entry);
module_exit(driver_unload);


MODULE_LICENSE("GPL");

MODULE_AUTHOR(
    "散人驱动"
);

MODULE_DESCRIPTION(
    "Kernel device debugging module"
);
