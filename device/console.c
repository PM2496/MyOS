#include "console.h"
#include "../lib/kernel/print.h"
#include "../lib/stdint.h"
#include "../thread/sync.h"
#include "../thread/thread.h"

static struct lock console_lock;

void console_init(void)
{
    lock_init(&console_lock); // Initi
}

void console_acquire(void)
{
    lock_acquire(&console_lock); // Acquire the console lock
}

void console_release(void)
{
    lock_release(&console_lock); // Release the console lock
}

void console_put_char(uint8_t char_asci)
{
    console_acquire();   // Acquire the console lock
    put_char(char_asci); // Output the character to the console
    console_release();   // Release the console lock
}

void console_put_str(char *str)
{
    console_acquire(); // Acquire the console lock
    put_str(str);      // Output the string to the console
    console_release(); // Release the console lock
}

void console_put_int(uint32_t num)
{
    console_acquire(); // Acquire the console lock
    put_int(num);      // Output the integer to the console
    console_release(); // Release the console lock
}