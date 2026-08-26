#ifndef COMM_H
#define COMM_H

#include <linux/slab.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/types.h>

#define DEVICE_NAME "wanbai"

typedef struct _COPY_MEMORY {
    pid_t pid;
    uintptr_t addr;
    void* buffer;
    size_t size;
} COPY_MEMORY, *PCOPY_MEMORY;

typedef struct _MODULE_BASE {
    pid_t pid;
    char* name;
    uintptr_t base;
} MODULE_BASE, *PMODULE_BASE;

typedef struct _BREAKPOINT_INFO {
    pid_t pid;
    uintptr_t addr;
    int type;
    int len;
    void* buffer;
    size_t size;
    int print_regs;
} BREAKPOINT_INFO, *PBREAKPOINT_INFO;

typedef struct _REGISTER_INFO {
    pid_t pid;
    uintptr_t addr;
    int type;
    int len;
    int reg_num;
    uint64_t value;
    int print_regs;
} REGISTER_INFO, *PREGISTER_INFO;

enum OPERATIONS {
    OP_READ_MEM = 0x801,
    OP_WRITE_MEM = 0x802,
    OP_MODULE_BASE = 0x803,
    OP_SET_BREAKPOINT = 0x804,
    OP_SET_BREAKPOINT_REG = 0x805,
};

#define BP_TYPE_READ  1
#define BP_TYPE_WRITE 2
#define BP_TYPE_RW    3
#define BP_TYPE_EXEC  4

#define BP_LEN_1  1
#define BP_LEN_2  2
#define BP_LEN_4  4
#define BP_LEN_8  8

#define REG_X0  0
#define REG_X1  1
#define REG_X2  2
#define REG_X3  3
#define REG_X4  4
#define REG_X5  5
#define REG_X6  6
#define REG_X7  7
#define REG_X8  8
#define REG_X9  9
#define REG_X10 10
#define REG_X11 11
#define REG_X12 12
#define REG_X13 13
#define REG_X14 14
#define REG_X15 15
#define REG_X16 16
#define REG_X17 17
#define REG_X18 18
#define REG_X19 19
#define REG_X20 20
#define REG_X21 21
#define REG_X22 22
#define REG_X23 23
#define REG_X24 24
#define REG_X25 25
#define REG_X26 26
#define REG_X27 27
#define REG_X28 28
#define REG_X29 29
#define REG_X30 30

static inline char* get_rand_str(void)
{
    static char string[10];
    int lstr, seed, flag, i;
    char *str = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    
    lstr = strlen(str);
    for (i = 0; i < 6; i++)
    {
        get_random_bytes(&seed, sizeof(int));
        flag = seed % lstr;
        if (flag < 0)
            flag = flag * -1;
        string[i] = str[flag];
    }
    string[6] = '\0';
    return string;
}

int dispatch_open(struct inode *node, struct file *file);
int dispatch_close(struct inode *node, struct file *file);

#endif