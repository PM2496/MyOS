TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

section .data
put_int_buffer dq 0      ; 定义8字节的缓冲区用于存储整数转换后的字符串

[bits 32]
section .text
;----------- put_str -----------------
; 功能描述：通过put_char函数来打印以'\0'结尾的字符串
;--------------------------------------
global put_str
put_str:
    push ebx
    push ecx
    xor ecx, ecx
    mov ebx, [esp + 12]   ; 获取字符串地址
.goon:
    mov cl, [ebx]
    cmp cl, 0             ; 检查是否到字符串结尾
    jz .str_over
    push ecx              ; 为put_char函数准备参数
    call put_char         ; 调用put_char函数打印字符
    add esp, 4            ; 清理栈空间
    inc ebx               ; 移动到下一个字符
    jmp .goon             ; 继续打印下一个字符
.str_over:
    pop ecx
    pop ebx
    ret 




;----------- put_char -----------------
; 功能描述：把栈中的一个字符写入光标所在处
;--------------------------------------
global put_char
put_char:
    pushad               ; 保存所有寄存器
    ; 将gs设置为视频段选择子
    ; 保险起见，每次打印都为gs赋值
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    ; 获取当前光标位置
    ; 先获取高8位
    mov dx, 0x3d4        ; 索引寄存器
    mov al, 0x0e         ; 用于提供光标位置高8位
    out dx, al
    mov dx, 0x3d5        ; 数据寄存器
    in al, dx            ; 读取光标位置高8位
    mov ah, al 
    ; 再获取低8位
    mov dx, 0x3d4       
    mov al, 0x0f     
    out dx, al
    mov dx, 0x3d5   
    in al, dx        ; 读取光标位置低8位

    ; 将光标存入bx
    mov bx, ax 
    ; 获取栈中待打印字符
    mov ecx, [esp + 36]   ; pushad压入4*8=32字节
                          ; 加上主调函数4字节返回地址，共36字节
    cmp cl, 0xd           ; CR是0xd，LF是0xa
    jz .is_carriage_return
    cmp cl, 0xa
    jz .is_line_feed

    cmp cl, 0x08         ; BS（backscace）的asci码为0x08
    jz .is_backspace
    jmp .put_other

.is_backspace:
    ; 光标左移一格
    dec bx
    shl bx, 1

    mov byte [gs:bx], 0x20  ; 用空格覆盖字符
    inc bx
    mov byte [gs:bx], 0x07 ; 设置光标颜色为白色
    shr bx, 1
    jmp .set_cursor

.put_other:
    shl bx, 1

    mov byte [gs:bx], cl   ; 将字符写入光标位置
    inc bx
    mov byte [gs:bx], 0x07 ; 设置字符属性
    shr bx, 1              ; 回复原光标值
    inc bx
    cmp bx, 80*25
    jl .set_cursor         ; 如果光标位置小于80*25，表示未写到显存最后
                           ; 则取设置新的光标值
.is_line_feed:             ; 否则，进行换行处理
.is_carriage_return:
    ; 光标位置回到行首
    xor dx, dx             ; dx是被除数的高16位，清0
    mov ax, bx             ; ax是被除数的低16位，bx是光标位置
    mov si, 80

    div si                 
    sub bx, dx             ; 光标值减去除以80的余数，即取整

.is_carriage_return_end:
    add bx, 80            ; 光标位置加一行
    cmp bx, 80*25
.is_line_feed_end:
    jl .set_cursor         ; 如果光标位置小于80*25，表示未写到显存最后
                           ; 则取设置新的光标值 

; 屏幕行范围0-24，滚屏原理是将1-24行搬运到0-23行
; 再将第24行清空
.roll_screen:
    cld
    mov ecx, 960           ; 一次搬运4字节，共24*80*2=3840字节，需要960次

    mov esi, 0xc00b80a0    ; 第1行行首
    mov edi, 0xc00b8000    ; 第0行行首
    rep movsd          

    ; 将第24行清空
    mov ebx, 3840
    mov ecx, 80

.cls:
    mov word [gs:ebx], 0x0720   ; 0x0720是黑底白字的空格键
    add ebx, 2
    loop .cls
    mov bx, 1920 ; 将光标值重置为最后一行的行首

.set_cursor:
    ; 将光标值设置为bx
    mov dx, 0x3d4        ; 索引寄存器
    mov al, 0x0e         ; 用于设置光标位置高8位
    out dx, al
    mov dx, 0x3d5        ; 数据寄存器
    mov al, bh           ; 高8位
    out dx, al

    mov dx, 0x3d4        ; 索引寄存器
    mov al, 0x0f         ; 用于设置光标位置低8位
    out dx, al
    mov dx, 0x3d5        ; 数据寄存器
    mov al, bl           ; 低8位
    out dx, al
.put_char_done:
    popad                ; 恢复所有寄存器
    ret                  ; 返回到主调函数   


global cls_screen
cls_screen:
   pushad
   ;;;;;;;;;;;;;;;
	; 由于用户程序的cpl为3,显存段的dpl为0,故用于显存段的选择子gs在低于自己特权的环境中为0,
	; 导致用户程序再次进入中断后,gs为0,故直接在put_str中每次都为gs赋值. 
   mov ax, SELECTOR_VIDEO	       ; 不能直接把立即数送入gs,须由ax中转
   mov gs, ax

   mov ebx, 0
   mov ecx, 80*25
 .cls:
   mov word [gs:ebx], 0x0720		  ;0x0720是黑底白字的空格键
   add ebx, 2
   loop .cls 
   mov ebx, 0

 .set_cursor:				  ;直接把set_cursor搬过来用,省事
;;;;;;; 1 先设置高8位 ;;;;;;;;
   mov dx, 0x03d4			  ;索引寄存器
   mov al, 0x0e				  ;用于提供光标位置的高8位
   out dx, al
   mov dx, 0x03d5			  ;通过读写数据端口0x3d5来获得或设置光标位置 
   mov al, bh
   out dx, al

;;;;;;; 2 再设置低8位 ;;;;;;;;;
   mov dx, 0x03d4
   mov al, 0x0f
   out dx, al
   mov dx, 0x03d5 
   mov al, bl
   out dx, al
   popad
   ret

;----------- put_int -----------------
; 功能描述：将整数转换为字符串并打印16进制表示   
;--------------------------------------
global put_int
put_int:
    pushad
    mov ebp, esp
    mov eax, [ebp + 4*9]     ; call的返回地址4字节加上pushad的8个4字节
    mov edx, eax
    mov edi, 7               ; 指定在put_int_buffer中初始的偏移量
    mov ecx, 8
    mov ebx, put_int_buffer  ; ebx指向缓冲区

; 将32位数字按照16进制的形式从低位到高位逐个处理，共处理8个16进制数字
.16based_4bits:
    and edx, 0x0000000f ; 取出低4位
    cmp edx, 9
    jg .is_A2F
    add edx, '0'
    jmp .store 
.is_A2F:
    sub edx, 10
    add edx, 'A'
.store:
    mov [ebx + edi], dl 
    dec edi 
    shr eax, 4
    mov edx, eax 
    loop .16based_4bits

.read_to_print:
    inc edi 
.skip_prefix_0:
    cmp edi, 8
    je .full0
.go_on_skip:
    mov cl, [put_int_buffer + edi]
    inc edi 
    cmp cl, '0'
    je .skip_prefix_0
    dec edi 
    jmp .put_each_num
.full0:
    mov cl, '0'
.put_each_num:
    push ecx
    call put_char 
    add esp, 4
    inc edi 
    mov cl, [put_int_buffer + edi]
    cmp edi, 8
    jl .put_each_num
    popad
    ret 

global set_cursor
set_cursor:
    pushad
    mov bx, [esp + 36] ; 获取参数

    ;;;;;;; 1 先设置高8位 ;;;;;;;;
    mov dx, 0x03d4			  ;索引寄存器
    mov al, 0x0e				  ;用于提供光标位置的高8位
    out dx, al
    mov dx, 0x03d5			  ;通过读写数据端口0x3d5来获得或设置光标位置 
    mov al, bh
    out dx, al

    ;;;;;;; 2 再设置低8位 ;;;;;;;;;
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5 
    mov al, bl
    out dx, al
    popad
    ret


