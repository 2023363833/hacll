#ifndef MEMORY_H
#define MEMORY_H

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>

/* 虚拟地址转物理地址 */
static inline phys_addr_t translate_linear_address(struct mm_struct* mm, uintptr_t va)
{
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    phys_addr_t page_addr;
    uintptr_t page_offset;

    pgd = pgd_offset(mm, va);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        return 0;
    }
    
    pud = pud_offset(pgd, va);
    if (pud_none(*pud) || pud_bad(*pud)) {
        return 0;
    }
    
    pmd = pmd_offset(pud, va);
    if (pmd_none(*pmd)) {
        return 0;
    }
    
    pte = pte_offset_kernel(pmd, va);
    if (pte_none(*pte)) {
        return 0;
    }
    
    if (!pte_present(*pte)) {
        return 0;
    }
    
    page_addr = (phys_addr_t)(pte_pfn(*pte) << PAGE_SHIFT);
    page_offset = va & (PAGE_SIZE - 1);
    
    return page_addr + page_offset;
}

/* 读取物理内存 */
static inline size_t read_physical_address(phys_addr_t pa, void* buffer, size_t size)
{
    void* mapped;
    
    if (!pfn_valid(__phys_to_pfn(pa))) {
        return 0;
    }
    
    mapped = ioremap_cache(pa, size);
    if (!mapped) {
        return 0;
    }
    
    if (copy_to_user(buffer, mapped, size)) {
        iounmap(mapped);
        return 0;
    }
    
    iounmap(mapped);
    return size;
}

/* 写入物理内存 */
static inline size_t write_physical_address(phys_addr_t pa, void* buffer, size_t size)
{
    void* mapped;
    
    if (!pfn_valid(__phys_to_pfn(pa))) {
        return 0;
    }
    
    mapped = ioremap_cache(pa, size);
    if (!mapped) {
        return 0;
    }
    
    if (copy_from_user(mapped, buffer, size)) {
        iounmap(mapped);
        return 0;
    }
    
    iounmap(mapped);
    return size;
}

/* 读取进程内存 */
static inline size_t read_process_memory(pid_t pid, uintptr_t addr, void* buffer, size_t size)
{
    struct task_struct* task;
    struct mm_struct* mm;
    size_t total_read = 0;
    
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        return 0;
    }
    
    mm = get_task_mm(task);
    if (!mm) {
        return 0;
    }
    
    down_read(&mm->mmap_sem);
    
    while (size > 0) {
        phys_addr_t pa;
        size_t chunk;
        
        pa = translate_linear_address(mm, addr);
        chunk = min(PAGE_SIZE - (addr & ~PAGE_MASK), size);
        
        if (!pa) {
            int bytes = access_process_vm(task, addr, buffer, chunk, FOLL_FORCE);
            if (bytes <= 0) {
                break;
            }
            total_read += bytes;
            buffer += bytes;
            addr += bytes;
            size -= bytes;
        } else {
            if (read_physical_address(pa, buffer, chunk) != chunk) {
                break;
            }
            total_read += chunk;
            buffer += chunk;
            addr += chunk;
            size -= chunk;
        }
    }
    
    up_read(&mm->mmap_sem);
    mmput(mm);
    return total_read;
}

/* 写入进程内存 */
static inline size_t write_process_memory(pid_t pid, uintptr_t addr, void* buffer, size_t size)
{
    struct task_struct* task;
    struct mm_struct* mm;
    size_t total_written = 0;
    
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        return 0;
    }
    
    mm = get_task_mm(task);
    if (!mm) {
        return 0;
    }
    
    down_write(&mm->mmap_sem);
    
    while (size > 0) {
        phys_addr_t pa;
        size_t chunk;
        
        pa = translate_linear_address(mm, addr);
        chunk = min(PAGE_SIZE - (addr & ~PAGE_MASK), size);
        
        if (!pa) {
            int bytes = access_process_vm(task, addr, buffer, chunk, 
                                          FOLL_FORCE | FOLL_WRITE);
            if (bytes <= 0) {
                break;
            }
            total_written += bytes;
            buffer += bytes;
            addr += bytes;
            size -= bytes;
        } else {
            if (write_physical_address(pa, buffer, chunk) != chunk) {
                break;
            }
            total_written += chunk;
            buffer += chunk;
            addr += chunk;
            size -= chunk;
        }
    }
    
    up_write(&mm->mmap_sem);
    mmput(mm);
    return total_written;
}

#endif