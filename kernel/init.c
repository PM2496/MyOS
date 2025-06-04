#include "init.h"
#include "../lib/kernel/print.h"
#include "interrupt.h"
#include "../device/timer.h"

void init_all(void)
{
    put_str("init_all\n");
    idt_init();   // Initialize the Interrupt Descriptor Table
    timer_init(); // Initialize the timer
    mem_init();   // Initialize memory management
}