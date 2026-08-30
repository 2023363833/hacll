#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/atomic.h>

#include "comm.h"

extern wait_queue_head_t breakpoint_wait_queue;
extern atomic_t breakpoint_triggered;
extern atomic_t breakpoint_count;

struct breakpoint_data {
    BREAKPOINT_INFO info;
    struct perf_event *bp_event;
    int triggered;
};

/*
 * ARM64 寄存器诊断输出。
 * 仅用于调试观察，不修改寄存器。
 */
static void print_all_registers(struct pt_regs *regs)
{
    int i;

    if (!regs) {
        printk(KERN_ERR "[memtool] register dump failed: regs is NULL\n");
        return;
    }

    printk(KERN_INFO "[memtool] ===== REGISTER DUMP =====\n");

    for (i = 0; i <= 28; i++) {
        printk(KERN_INFO "[memtool] x%-2d = 0x%016llx\n",
               i,
               (unsigned long long)regs->regs[i]);
    }

    printk(KERN_INFO "[memtool] x29 = 0x%016llx (FP)\n",
           (unsigned long long)regs->regs[29]);

    printk(KERN_INFO "[memtool] x30 = 0x%016llx (LR)\n",
           (unsigned long long)regs->regs[30]);

    printk(KERN_INFO "[memtool] pc  = 0x%016llx\n",
           (unsigned long long)regs->pc);

    printk(KERN_INFO "[memtool] sp  = 0x%016llx\n",
           (unsigned long long)regs->sp);

    printk(KERN_INFO "[memtool] pstate = 0x%016llx\n",
           (unsigned long long)regs->pstate);

    printk(KERN_INFO "[memtool] =========================\n");
}


/*
 * 硬件断点回调。
 *
 * 仅执行：
 * 1. 输出诊断信息
 * 2. 可选输出寄存器
 * 3. 通知用户态有事件
 */
static void breakpoint_handler(
    struct perf_event *bp_event,
    struct perf_sample_data *data,
    struct pt_regs *regs
)
{
    struct breakpoint_data *bp_data;

    if (!bp_event) {
        printk(KERN_ERR
               "[memtool] breakpoint callback: bp_event is NULL\n");
        return;
    }

    bp_data =
        (struct breakpoint_data *)
        bp_event->overflow_handler_context;

    if (!bp_data) {
        printk(KERN_ERR
               "[memtool] breakpoint callback: context is NULL\n");
        return;
    }

    printk(KERN_INFO
           "[memtool] ===== BREAKPOINT TRIGGERED =====\n");

    printk(KERN_INFO
           "[memtool] callback entered\n");

    printk(KERN_INFO
           "[memtool] pid=%d\n",
           bp_data->info.pid);

    printk(KERN_INFO
           "[memtool] breakpoint addr=0x%lx\n",
           (unsigned long)bp_data->info.addr);

    if (regs) {
        printk(KERN_INFO
               "[memtool] current pc=0x%016llx\n",
               (unsigned long long)regs->pc);

        printk(KERN_INFO
               "[memtool] current sp=0x%016llx\n",
               (unsigned long long)regs->sp);
    } else {
        printk(KERN_ERR
               "[memtool] pt_regs is NULL\n");
    }

    printk(KERN_INFO
           "[memtool] print_regs=%d\n",
           bp_data->info.print_regs);

    /*
     * 仅在明确请求时打印寄存器。
     */
    if (bp_data->info.print_regs) {

        printk(KERN_INFO
               "[memtool] register dump requested\n");

        print_all_registers(regs);

    } else {

        printk(KERN_INFO
               "[memtool] register dump disabled\n");
    }


    bp_data->triggered = 1;


    /*
     * 增加事件计数。
     * poll() 只检测，
     * read() 负责消费。
     */
    atomic_set(
        &breakpoint_triggered,
        1
    );

    atomic_inc(
        &breakpoint_count
    );


    printk(KERN_INFO
           "[memtool] pending events=%d\n",
           atomic_read(&breakpoint_count));


    wake_up_interruptible(
        &breakpoint_wait_queue
    );


    printk(KERN_INFO
           "[memtool] user event notified\n");

    printk(KERN_INFO
           "[memtool] ===== BREAKPOINT END =====\n");
}


/*
 * 注册硬件断点。
 *
 * 仅用于调试和观察。
 */
static inline int register_breakpoint(
    BREAKPOINT_INFO *bp_info,
    REGISTER_INFO *reg_info
)
{
    struct task_struct *task;
    struct breakpoint_data *bp_data;
    struct perf_event_attr attr;
    struct perf_event *bp_event;
    int ret;

    if (!bp_info) {

        printk(KERN_ERR
               "[memtool] breakpoint registration: bp_info is NULL\n");

        return -EINVAL;
    }


    printk(KERN_INFO
           "[memtool] ===== BREAKPOINT REGISTER =====\n");

    printk(KERN_INFO
           "[memtool] pid=%d\n",
           bp_info->pid);

    printk(KERN_INFO
           "[memtool] addr=0x%lx\n",
           (unsigned long)bp_info->addr);

    printk(KERN_INFO
           "[memtool] type=%d\n",
           bp_info->type);

    printk(KERN_INFO
           "[memtool] len=%d\n",
           bp_info->len);

    printk(KERN_INFO
           "[memtool] print_regs=%d\n",
           bp_info->print_regs);


    task =
        pid_task(
            find_vpid(bp_info->pid),
            PIDTYPE_PID
        );


    if (!task) {

        printk(KERN_ERR
               "[memtool] process not found: %d\n",
               bp_info->pid);

        return -ESRCH;
    }


    bp_data =
        kzalloc(
            sizeof(*bp_data),
            GFP_KERNEL
        );


    if (!bp_data) {

        printk(KERN_ERR
               "[memtool] breakpoint data allocation failed\n");

        return -ENOMEM;
    }


    memcpy(
        &bp_data->info,
        bp_info,
        sizeof(BREAKPOINT_INFO)
    );


    bp_data->triggered =
        0;


    memset(
        &attr,
        0,
        sizeof(attr)
    );


    attr.type =
        PERF_TYPE_BREAKPOINT;

    attr.size =
        sizeof(attr);

    attr.bp_type =
        bp_info->type;

    attr.bp_addr =
        bp_info->addr;

    attr.bp_len =
        bp_info->len;

    attr.disabled =
        0;


    printk(KERN_INFO
           "[memtool] calling register_user_hw_breakpoint\n");


    bp_event =
        register_user_hw_breakpoint(
            &attr,
            breakpoint_handler,
            bp_data,
            task
        );


    if (IS_ERR(bp_event)) {

        ret =
            PTR_ERR(bp_event);


        printk(KERN_ERR
               "[memtool] breakpoint registration failed ret=%d\n",
               ret);


        kfree(
            bp_data
        );


        return ret;
    }


    bp_data->bp_event =
        bp_event;


    printk(KERN_INFO
           "[memtool] breakpoint registered successfully\n");

    printk(KERN_INFO
           "[memtool] event=%px\n",
           bp_event);

    printk(KERN_INFO
           "[memtool] address=0x%lx\n",
           (unsigned long)bp_info->addr);

    printk(KERN_INFO
           "[memtool] ===== REGISTER END =====\n");


    return 0;
}

#endif
