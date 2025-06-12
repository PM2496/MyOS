#include "stdio.h"
#include "../kernel/interrupt.h"
#include "../kernel/global.h"
#include "string.h"
#include "./kernel/print.h"
#include "../lib/user/syscall.h"

#define va_start(ap, v) ap = (va_list) & v // 把ap指向第一个固定参数v
#define va_arg(ap, t) *((t *)(ap += 4))    // ap指向下一个参数并返回其值
#define va_end(ap) ap = NULL               // 清除ap

/* 将整型转换成字符(integer to ascii) */
static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base)
{
    uint32_t m = value % base; // 取余
    uint32_t i = value / base; // 整除
    if (i)                     // 如果value不为0,继续递归
    {
        itoa(i, buf_ptr_addr, base);
    }

    if (m < 10) // 如果余数小于10,转换为字符
    {
        **buf_ptr_addr = m + '0';
    }
    else // 如果余数大于等于10,转换为字符
    {
        **buf_ptr_addr = m - 10 + 'A';
    }
    (*buf_ptr_addr)++; // 移动指针到下一个位置
}

/* 将参数ap按照格式format输出到字符串str,并返回替换后str长度 */
uint32_t vsprintf(char *str, const char *format, va_list ap)
{
    char *buf_ptr = str;            // buf_ptr指向str的起始位置
    const char *index_ptr = format; // index_ptr指向format的起始位置
    char index_char = *index_ptr;
    int32_t arg_int;
    char *arg_str;
    while (index_char)
    {
        if (index_char != '%') // 如果不是格式化符号,直接复制到buf_ptr
        {
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr); // 移动到下一个字符
            continue;
        }
        index_char = *(++index_ptr); // 跳过'%'
        switch (index_char)
        {
        case 's':
            /* code */
            arg_str = va_arg(ap, char *); // 获取字符串参数
            strcpy(buf_ptr, arg_str);     // 复制字符串到buf_ptr
            buf_ptr += strlen(arg_str);   // 移动buf_ptr到字符串末尾
            index_char = *(++index_ptr);  // 移动到下一个字符
            break;
        case 'c':
            /* code */
            *(buf_ptr++) = va_arg(ap, char); // 获取字符参数并存储到buf_ptr
            index_char = *(++index_ptr);     // 移动到下一个字符
            break;
        case 'd':
            /* code */
            arg_int = va_arg(ap, int); // 获取整数参数
            if (arg_int < 0)           // 如果是负数,先输出'-'
            {
                *buf_ptr++ = '-';
                arg_int = 0 - arg_int; // 取绝对值
            }
            itoa(arg_int, &buf_ptr, 10); // 将整数转换为字符串
            index_char = *(++index_ptr); // 移动到下一个字符
            break;
        case 'x':
            /* code */
            arg_int = va_arg(ap, int);
            itoa(arg_int, &buf_ptr, 16); // 将整数转换为十六进制字符串
            index_char = *(++index_ptr); // 移动到下一个字符
            break;
        }
    }
    return strlen(str); // 返回字符串长度
}

/* 格式化输出字符串format */
uint32_t printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);      // 使args指向format
    char buf[1024] = {0};        // 缓冲区,用于存储格式化后的字符串
    vsprintf(buf, format, args); // 调用vsprintf进行格式化
    va_end(args);                // 清除args
    return write(buf);           // 将格式化后的字符串输出到控制台
}

uint32_t sprintf(char *buf, const char *format, ...)
{
    va_list args;
    uint32_t retval;
    va_start(args, format);               // 使args指向format
    retval = vsprintf(buf, format, args); // 调用vsprintf进行格式化
    va_end(args);                         // 清除args
    return retval;                        // 返回格式化后的字符串长度
}