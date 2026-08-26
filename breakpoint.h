#ifndef BREAKPOINT_H
#define BREAKPOINT_H

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
    REGISTER_INFO reg_info;
    struct perf_event *bp_event;
    void *write_buffer;
    size_t write_size;
    int triggered;
};

/* 打印所有寄存器 */
static void print_all_registers(struct pt_regs *regs)
{
    printk(KERN_INFO "[memtool] ========== 寄存器状态 ==========\n");
    printk(KERN_INFO "[memtool] x0  = 0x%016llx\n", (unsigned long long)regs->regs[0]);
    printk(KERN_INFO "[memtool] x1  = 0x%016llx\n", (unsigned long long)regs->regs[1]);
    printk(KERN_INFO "[memtool] x2  = 0x%016llx\n", (unsigned long long)regs->regs[2]);
    printk(KERN_INFO "[memtool] x3  = 0x%016llx\n", (unsigned long long)regs->regs[3]);
    printk(KERN_INFO "[memtool] x4  = 0x%016llx\n", (unsigned long long)regs->regs[4]);
    printk(KERN_INFO "[memtool] x5  = 0x%016llx\n", (unsigned long long)regs->regs[5]);
    printk(KERN_INFO "[memtool] x6  = 0x%016llx\n", (unsigned long long)regs->regs[6]);
    printk(KERN_INFO "[memtool] x7  = 0x%016llx\n", (unsigned long long)regs->regs[7]);
    printk(KERN_INFO "[memtool] x8  = 0x%016llx\n", (unsigned long long)regs->regs[8]);
    printk(KERN_INFO "[memtool] x9  = 0x%016llx\n", (unsigned long long)regs->regs[9]);
    printk(KERN_INFO "[memtool] x10 = 0x%016llx\n", (unsigned long long)regs->regs[10]);
    printk(KERN_INFO "[memtool] x11 = 0x%016llx\n", (unsigned long long)regs->regs[11]);
    printk(KERN_INFO "[memtool] x12 = 0x%016llx\n", (unsigned long long)regs->regs[12]);
    printk(KERN_INFO "[memtool] x13 = 0x%016llx\n", (unsigned long long)regs->regs[13]);
    printk(KERN_INFO "[memtool] x14 = 0x%016llx\n", (unsigned long long)regs->regs[14]);
    printk(KERN_INFO "[memtool] x15 = 0x%016llx\n", (unsigned long long)regs->regs[15]);
    printk(KERN_INFO "[memtool] x16 = 0x%016llx\n", (unsigned long long)regs->regs[16]);
    printk(KERN_INFO "[memtool] x17 = 0x%016llx\n", (unsigned long long)regs->regs[17]);
    printk(KERN_INFO "[memtool] x18 = 0x%016llx\n", (unsigned long long)regs->regs[18]);
    printk(KERN_INFO "[memtool] x19 = 0x%016llx\n", (unsigned long long)regs->regs[19]);
    printk(KERN_INFO "[memtool] x20 = 0x%016llx\n", (unsigned long long)regs->regs[20]);
    printk(KERN_INFO "[memtool] x21 = 0x%016llx\n", (unsigned long long)regs->regs[21]);
    printk(KERN_INFO "[memtool] x22 = 0x%016llx\n", (unsigned long long)regs->regs[22]);
    printk(KERN_INFO "[memtool] x23 = 0x%016llx\n", (unsigned long long)regs->regs[23]);
    printk(KERN_INFO "[memtool] x24 = 0x%016llx\n", (unsigned long long)regs->regs[24]);
    printk(KERN_INFO "[memtool] x25 = 0x%016llx\n", (unsigned long long)regs->regs[25]);
    printk(KERN_INFO "[memtool] x26 = 0x%016llx\n", (unsigned long long)regs->regs[26]);
    printk(KERN_INFO "[memtool] x27 = 0x%016llx\n", (unsigned long long)regs->regs[27]);
    printk(KERN_INFO "[memtool] x28 = 0x%016llx\n", (unsigned long long)regs->regs[28]);
    printk(KERN_INFO "[memtool] x29 = 0x%016llx (FP)\n", (unsigned long long)regs->regs[29]);
    printk(KERN_INFO "[memtool] x30 = 0x%016llx (LR)\n", (unsigned long long)regs->regs[30]);
    printk(KERN_INFO "[memtool] pc  = 0x%016llx\n", (unsigned long long)regs->pc);
    printk(KERN_INFO "[memtool] sp  = 0x%016llx\n", (unsigned long long)regs->sp);
    printk(KERN_INFO "[memtool] pstate = 0x%016llx\n", (unsigned long long)regs->pstate);
    printk(KERN_INFO "[memtool] ==================================\n");
}

/* 断点回调函数 */
static void breakpoint_handler(struct perf_event *bp_event,
                               struct perf_sample_data *data,
                               struct pt_regs *regs)
{
    struct breakpoint_data *bp_data = 
        (struct breakpoint_data *)bp_event->overflow_handler_context;
    
    if (!bp_data) return;
    
    printk(KERN_INFO "[memtool] ========================================\n");
    printk(KERN_INFO "[memtool] 断点触发！\n");
    printk(KERN_INFO "[memtool] 断点地址: 0x%lx\n", (unsigned long)bp_data->info.addr);
    printk(KERN_INFO "[memtool] 当前PC: 0x%llx\n", (unsigned long long)regs->pc);
    
    if (bp_data->info.print_regs == 1) {
        print_all_registers(regs);
    }
    
    bp_data->triggered = 1;
    
    atomic_set(&breakpoint_triggered, 1);
    atomic_inc(&breakpoint_count);
    wake_up_interruptible(&breakpoint_wait_queue);
    
    if (bp_data->reg_info.reg_num >= 0 && bp_data->reg_info.reg_num <= 30) {
        uint64_t old_value = regs->regs[bp_data->reg_info.reg_num];
        regs->regs[bp_data->reg_info.reg_num] = bp_data->reg_info.value;
        
        printk(KERN_INFO "[memtool] 修改寄存器 x%d: 0x%llx -> 0x%llx\n",
               bp_data->reg_info.reg_num,
               (unsigned long long)old_value,
               (unsigned long long)bp_data->reg_info.value);
    }
    
    if (bp_data->write_buffer && bp_data->write_size > 0) {
        struct task_struct *task = 
            pid_task(find_vpid(bp_data->info.pid), PIDTYPE_PID);
        
        if (task) {
            int ret = access_process_vm(task,
                                       bp_data->info.addr,
                                       bp_data->write_buffer,
                                       bp_data->write_size,
                                       FOLL_FORCE | FOLL_WRITE);
            
            if (ret > 0) {
                printk(KERN_INFO "[memtool] 内存写入成功: %d 字节\n", ret);
            }
        }
        
        kfree(bp_data->write_buffer);
        bp_data->write_buffer = NULL;
        bp_data->write_size = 0;
    }
    
    printk(KERN_INFO "[memtool] ========================================\n");
}

/* 注册断点 */
static inline int register_breakpoint(BREAKPOINT_INFO *bp_info, REGISTER_INFO *reg_info)
{
    struct task_struct *task;
    struct breakpoint_data *bp_data;
    struct perf_event_attr attr;
    struct perf_event *bp_event;
    int ret = 0;
    
    if (!bp_info) {
        return -EINVAL;
    }
    
    task = pid_task(find_vpid(bp_info->pid), PIDTYPE_PID);
    if (!task) {
        printk(KERN_ERR "[memtool] 进程不存在: %d\n", bp_info->pid);
        return -ESRCH;
    }
    
    bp_data = kmalloc(sizeof(struct breakpoint_data), GFP_KERNEL);
    if (!bp_data) {
        return -ENOMEM;
    }
    memset(bp_data, 0, sizeof(struct breakpoint_data));
    
    memcpy(&bp_data->info, bp_info, sizeof(BREAKPOINT_INFO));
    
    if (reg_info) {
        memcpy(&bp_data->reg_info, reg_info, sizeof(REGISTER_INFO));
    } else {
        bp_data->reg_info.reg_num = -1;
    }
    
    if (bp_info->buffer && bp_info->size > 0) {
        bp_data->write_buffer = kmalloc(bp_info->size, GFP_KERNEL);
        if (!bp_data->write_buffer) {
            kfree(bp_data);
            return -ENOMEM;
        }
        
        if (copy_from_user(bp_data->write_buffer, 
                          bp_info->buffer, 
                          bp_info->size)) {
            kfree(bp_data->write_buffer);
            kfree(bp_data);
            return -EFAULT;
        }
        bp_data->write_size = bp_info->size;
    }
    
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.size = sizeof(attr);
    attr.bp_type = bp_info->type;
    attr.bp_addr = bp_info->addr;
    attr.bp_len = bp_info->len;
    attr.disabled = 0;
    
    bp_event = register_user_hw_breakpoint(&attr,
                                          breakpoint_handler,
                                          bp_data,
                                          task);
    
    if (IS_ERR(bp_event)) {
        ret = PTR_ERR(bp_event);
        printk(KERN_ERR "[memtool] 断点注册失败: %d\n", ret);
        
        if (bp_data->write_buffer) {
            kfree(bp_data->write_buffer);
        }
        kfree(bp_data);
        return ret;
    }
    
    bp_data->bp_event = bp_event;
    
    printk(KERN_INFO "[memtool] 断点注册成功: 0x%lx\n",
           (unsigned long)bp_info->addr);
    
    return 0;
}

#endif