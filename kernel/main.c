#include "print.h"
#include "debug.h"
#include "init.h"
#include "memory.h"
#include "../thread/thread.h"
#include "interrupt.h"
#include "../device/console.h"
#include "../device/ioqueue.h"
#include "../device/keyboard.h"

void k_thread_a(void *arg);
void k_thread_b(void *arg);

int main(void)
{
    put_str("I am kernel\n");
    init_all(); // Initialize all components of the kernel

    thread_start("consumer_a", 31, k_thread_a, " A_");
    thread_start("consumer_b", 31, k_thread_b, " B_");

    intr_enable(); // Enable interrupts

    while (1)
    {
        // console_put_str("Main ");
    }
    return 0; // This line will never be reached
}

void k_thread_a(void *arg)
{
    char *para = arg;
    while (1)
    {
        enum intr_status old_status = intr_disable(); // Disable interrupts
        if (!ioq_empty(&kbd_buf))                     // Check if the keyboard buffer is not empty
        {
            char byte = ioq_getchar(&kbd_buf); // Get a character from the keyboard buffer
            console_put_str(para);             // Output the parameter string
            console_put_char(byte);            // Output the character
        }
        intr_set_status(old_status); // Restore previous interrupt status
    }
}

void k_thread_b(void *arg)
{
    char *para = arg;
    while (1)
    {
        enum intr_status old_status = intr_disable(); // Disable interrupts
        if (!ioq_empty(&kbd_buf))                     // Check if the keyboard buffer is not empty
        {
            char byte = ioq_getchar(&kbd_buf); // Get a character from the keyboard buffer
            console_put_str(para);             // Output the parameter string
            console_put_char(byte);            // Output the character
        }
        intr_set_status(old_status); // Restore previous interrupt status
    }
}