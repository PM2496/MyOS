#include "stdio_kernel.h"
#include "print.h"
#include "../stdio.h"
#include "../../device/console.h"
#include "../../kernel/global.h"

#define va_start(args, first_fix) args = (va_list) & first_fix // Set ap to point to the first fixed argument v
#define va_end(args) args = NULL

void printk(const char *format, ...)
{
    va_list args;           // Declare a variable to hold the variable arguments
    va_start(args, format); // Initialize args to point to the first fixed argument

    char buf[1024] = {0};        // Buffer to hold the formatted string
    vsprintf(buf, format, args); // Format the string into buf

    va_end(args); // Clean up the variable argument list

    console_put_str(buf); // Output the formatted string to the console
}