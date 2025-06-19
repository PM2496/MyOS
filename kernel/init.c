#include "init.h"
#include "../lib/kernel/print.h"
#include "interrupt.h"
#include "../device/timer.h"
#include "memory.h"
#include "../thread/thread.h"
#include "../device/console.h"
#include "../device/keyboard.h"
#include "../userprog/tss.h"
#include "../userprog/syscall_init.h"
#include "../device/ide.h"
#include "../fs/fs.h"

void init_all(void)
{
    put_str("init_all\n");
    idt_init();      // Initialize the Interrupt Descriptor Table
    mem_init();      // Initialize memory management
    thread_init();   // Initialize thread management
    timer_init();    // Initialize the timer
    console_init();  // Initialize the console
    keyboard_init(); // Initialize the keyboard
    tss_init();      // Initialize Task State Segment
    syscall_init();  // Initialize system calls
    intr_enable();   // Enable interrupts
    ide_init();      // Initialize IDE (if applicable)
    filesys_init();  // Initialize the file system
}