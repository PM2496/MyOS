[bits 32]
section .text
global switch_to
switch_to:

    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp + 20]  ; Load the address of the new thread's context
    mov [eax], esp  ; Save the current stack pointer in the new thread's context
    

    mov eax, [esp + 24]  ; Load the address of the current thread's context
    mov esp, [eax]  ; Restore the stack pointer from the current thread's context

    pop ebp
    pop ebx
    pop edi
    pop esi 
    ret