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
#include "../lib/kernel/stdio_kernel.h"

extern void cls_screen(void);

void init(void);

int main(void)
{
    put_str("I am kernel\n");
    init_all();

    /*************    写入应用程序    *************/
    uint32_t file_size = 11692;
    uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    struct disk *sda = &channels[0].devices[0];
    void *prog_buf = sys_malloc(file_size);
    ide_read(sda, 300, prog_buf, sec_cnt);
    int32_t fd = sys_open("/prog_no_arg", O_CREAT | O_RDWR);
    if (fd != -1)
    {
        if (sys_write(fd, prog_buf, file_size) == -1)
        {
            printk("file write error!\n");
            while (1)
                ;
        }
    }
    /*************    写入应用程序结束   *************/

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