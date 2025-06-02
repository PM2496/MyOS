#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H

typedef void *intr_handler;
void idt_init(void);

enum intr_status
{
    INTR_OFF, // Interrupts are disabled
    INTR_ON   // Interrupts are enabled
};

enum intr_status intr_get_status(void);
enum intr_status intr_set_status(enum intr_status);
enum intr_status intr_enable(void);
enum intr_status intr_disable(void);
#endif