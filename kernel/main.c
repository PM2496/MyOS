#include "print.h"
#include "debug.h"
#include "init.h"
#include "memory.h"
int main(void)
{
    put_str("I am kernel\n");
    init_all(); // Initialize all components of the kernel
    // asm volatile("sti"); // Enable interrupts
    void *addr = get_kernel_pages(3); // Allocate one page of kernel memory
    put_str("\n get_kernel_page start vaddr is ");
    put_int((uint32_t)addr);
    put_str("\n");
    while (1)
        ;
    return 0; // This line will never be reached
}