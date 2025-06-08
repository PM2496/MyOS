[bits 32]
%define ERROR_CODE nop
%define ZERO push 0

extern idt_table

section .data
global intr_entry_table
intr_entry_table:

%macro VECTOR 2
section .text
intr%1entry:
    %2
    
    push ds
    push es
    push fs
    push gs
    pushad  ; 保存所有寄存器

    mov al, 0x20
    out 0xa0, al  ; 发送EOI信号到主PIC
    out 0x20, al  ; 发送EOI信号到从PIC

    push %1
    call [idt_table + %1 * 4]  ; 调用中断处理函数
    jmp intr_exit

section .data
    dd intr%1entry
%endmacro

section .text
global intr_exit
intr_exit:
    add esp, 4  ; 清理栈空间
    popad  ; 恢复所有寄存器
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4  ; 移除错误码
    iretd  ; 返回中断

VECTOR 0x00, ZERO
VECTOR 0x01, ZERO
VECTOR 0x02, ZERO
VECTOR 0x03, ZERO
VECTOR 0x04, ZERO
VECTOR 0x05, ZERO
VECTOR 0x06, ZERO
VECTOR 0x07, ZERO
VECTOR 0x08, ERROR_CODE
VECTOR 0x09, ZERO
VECTOR 0x0A, ERROR_CODE
VECTOR 0x0B, ERROR_CODE
VECTOR 0x0C, ZERO
VECTOR 0x0D, ERROR_CODE
VECTOR 0x0E, ERROR_CODE
VECTOR 0x0F, ZERO
VECTOR 0x10, ZERO
VECTOR 0x11, ERROR_CODE
VECTOR 0x12, ZERO
VECTOR 0x13, ZERO
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO
VECTOR 0x18, ERROR_CODE
VECTOR 0x19, ZERO
VECTOR 0x1A, ERROR_CODE
VECTOR 0x1B, ERROR_CODE
VECTOR 0x1C, ZERO
VECTOR 0x1D, ERROR_CODE
VECTOR 0x1E, ERROR_CODE
VECTOR 0x1F, ZERO
VECTOR 0x20,ZERO	;时钟中断对应的入口
VECTOR 0x21,ZERO	;键盘中断对应的入口
VECTOR 0x22,ZERO	;级联用的
VECTOR 0x23,ZERO	;串口2对应的入口
VECTOR 0x24,ZERO	;串口1对应的入口
VECTOR 0x25,ZERO	;并口2对应的入口
VECTOR 0x26,ZERO	;软盘对应的入口
VECTOR 0x27,ZERO	;并口1对应的入口
VECTOR 0x28,ZERO	;实时时钟对应的入口
VECTOR 0x29,ZERO	;重定向
VECTOR 0x2a,ZERO	;保留
VECTOR 0x2b,ZERO	;保留
VECTOR 0x2c,ZERO	;ps/2鼠标
VECTOR 0x2d,ZERO	;fpu浮点单元异常
VECTOR 0x2e,ZERO	;硬盘
VECTOR 0x2f,ZERO	;保留
