#include "interrupt.h"
#include "global.h"
#include "../lib/stdint.h"
#include "../lib/kernel/print.h"
#include "../lib/kernel/io.h"

#define PIC_M_CTRL 0x20
#define PIC_M_DATA 0x21
#define PIC_S_CTRL 0xa0
#define PIC_S_DATA 0xa1

#define IDT_DESC_CNT 0x21

#define EFLAGS_IF 0x00000200                                                     // Interrupt Flag in EFLAGS register
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0" : "=g"(EFLAG_VAR)); // Get EFLAGS value

struct gate_desc
{
    uint16_t func_offset_low_word;  // Offset bits 0..15
    uint16_t selector;              // Selector in GDT or LDT
    uint8_t dcount;                 // Number of parameters
    uint8_t attribute;              // Attributes and type
    uint16_t func_offset_high_word; // Offset bits 16..31
};

static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];

char *intr_name[IDT_DESC_CNT];
intr_handler idt_table[IDT_DESC_CNT];

extern intr_handler intr_entry_table[IDT_DESC_CNT];

static void pic_init(void)
{
    outb(PIC_M_CTRL, 0x11); // Initialize Master PIC
    outb(PIC_M_DATA, 0x20); // Set Master PIC vector offset
    outb(PIC_M_DATA, 0x04); // Slave PIC connected to IRQ2
    outb(PIC_M_DATA, 0x01); // Enable Master PIC

    outb(PIC_S_CTRL, 0x11); // Initialize Slave PIC
    outb(PIC_S_DATA, 0x28); // Set Slave PIC vector offset
    outb(PIC_S_DATA, 0x02); // Slave PIC connected to IRQ2
    outb(PIC_S_DATA, 0x01); // Enable Slave PIC

    outb(PIC_M_DATA, 0xfe); // Mask all IRQs except for timer (IRQ0)
    outb(PIC_S_DATA, 0xff); // Mask all IRQs on Slave PIC

    put_str("   pic_init done\n");
}

static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function)
{
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000ffff;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xffff0000) >> 16;
}

static void idt_desc_init(void)
{
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    put_str("   idt_desc_init done\n");
}

static void general_intr_handler(uint8_t vec_nr)
{
    if (vec_nr == 0x27 || vec_nr == 0x2f) // IRQ7 and IRQ15 are not used
    {
        return;
    }
    put_str("int vector: 0x");
    put_int(vec_nr);
    put_char('\n');
}

static void exception_init(void)
{
    int i;
    // Initialize exception handlers
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        intr_name[i] = "unknown";
        idt_table[i] = general_intr_handler;
    }

    // Set specific exception names and handlers
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "#NMI Non-Maskable Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR Bound Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun Exception";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present Exception";
    intr_name[12] = "#SS Stack-Segment Fault Exception";
    intr_name[13] = "#GP General Protection Fault Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 是intel保留项
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

void idt_init(void)
{
    put_str("idt_init start\n");

    idt_desc_init();  // Initialize IDT descriptors
    exception_init(); // Initialize exception handlers
    pic_init();       // Initialize PIC

    // Load IDT
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m"(idt_operand));

    put_str("idt_init done\n");
}

/* 开中断并返回开中断前的状态 */
enum intr_status intr_enable(void)
{
    enum intr_status old_status;
    if (INTR_ON == intr_get_status())
    {
        old_status = INTR_ON; // Already enabled
        return old_status;
    }
    else
    {
        old_status = INTR_OFF; // Currently disabled
        asm volatile("sti");   // 开中断，sti指令将IF位置1
        return old_status;
    }
}

/* 关闭中断并返回关中断前的状态 */
enum intr_status intr_disable(void)
{
    enum intr_status old_status;
    if (INTR_ON == intr_get_status())
    {
        old_status = INTR_ON;               // Currently enabled
        asm volatile("cli" : : : "memory"); // 关中断，cli指令将IF位置0
        return old_status;
    }
    else
    {
        old_status = INTR_OFF; // Already disabled
        return old_status;
    }
}

/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status)
{
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status(void)
{
    uint32_t eflags = 0;
    GET_EFLAGS(eflags); // 获取EFLAGS寄存器的值
    if (eflags & EFLAGS_IF)
    {
        return INTR_ON; // IF位为1，表示中断开启
    }
    else
    {
        return INTR_OFF; // IF位为0，表示中断关闭
    }
}
