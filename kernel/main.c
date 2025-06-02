#include "print.h"
#include "debug.h"
int main(void)
{
    put_str("I am kernel\n");
    init_all(); // Initialize all components of the kernel
    // asm volatile("sti"); // Enable interrupts
    ASSERT(1 == 2);

    while (1)
        ;
    return 0; // This line will never be reached
}