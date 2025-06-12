#ifndef __USERPROG_SYSCALL_INIT_H
#define __USERPROG_SYSCALL_INIT_H

#include "../lib/stdint.h"

void syscall_init(void);
uint32_t sys_getpid(void);
uint32_t sys_write(char *str);

#endif