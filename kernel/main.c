#include "print.h"
#include "debug.h"
#include "init.h"
#include "memory.h"
#include "../thread/thread.h"
#include "interrupt.h"
#include "../device/console.h"
#include "../device/ioqueue.h"
#include "../device/keyboard.h"
#include "../userprog/process.h"
#include "../userprog/syscall_init.h"
#include "../lib/user/syscall.h"
#include "../lib/stdio.h"
#include "../fs/fs.h"
#include "../lib/string.h"
#include "../fs/dir.h"
#include "../shell/shell.h"
#include "../lib/user/assert.h"

extern void cls_screen(void);

void init(void);

int main(void)
{
    put_str("I am kernel\n");
    init_all();

    /********  测试代码  ********/
    cls_screen(); // 清屏
    console_put_str("[user@localhost /]$ ");
    /********  测试代码  ********/
    while (1)
        ;
    return 0;
}

/* init进程 */
void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid)
    { // 父进程
        while (1)
            ;
    }
    else
    { // 子进程
        my_shell();
    }
    panic("init: should not be here");
}