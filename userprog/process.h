#ifndef __USER_PROCESS_H
#define __USER_PROCESS_H

#include "../thread/thread.h"
#include "../lib/stdint.h"

#define default_prio 31                         // 默认优先级
#define USER_STACK3_VADDR (0xc0000000 - 0x1000) // 用户栈的虚拟地址
#define USER_VADDR_START 0x8048000              // 用户程序的虚拟地址起始位置

void process_execute(void *filename, char *name);
void start_process(void *filename_);
void process_activate(struct task_struct *p_thread);
void page_dir_activate(struct task_struct *p_thread);
uint32_t *create_page_dir(void);
void create_user_vaddr_bitmap(struct task_struct *user_prog);

#endif