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

void k_thread_a(void *arg);
void k_thread_b(void *arg);
void u_prog_a(void);
void u_prog_b(void);
int test_var_a = 0, test_var_b = 0;

int main(void)
{
    put_str("I am kernel\n");
    init_all(); // Initialize all components of the kernel

    thread_start("consumer_a", 31, k_thread_a, " A_");
    thread_start("consumer_b", 31, k_thread_b, " B_");
    process_execute(u_prog_a, "u_prog_a");
    process_execute(u_prog_b, "u_prog_b");

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
        console_put_str(" v_a: 0x");
        console_put_int(test_var_a); // Output the value of test_var_b
    }
}

void k_thread_b(void *arg)
{
    char *para = arg;
    while (1)
    {
        console_put_str(" v_b: 0x");
        console_put_int(test_var_b); // New line for better readability
    }
}

void u_prog_a(void)
{
    while (1)
    {
        test_var_a++;
    }
}

void u_prog_b(void)
{
    while (1)
    {
        test_var_b++;
    }
}