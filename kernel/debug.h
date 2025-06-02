#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

void panic_spin(char *filename, int line, const char *func, const char *condition);

/* __VA_ARGS__是预处理器支持的专用标识符
  代表所有与省略号相对应的参数
  "..."是可变参数的标识符
*/
#define PANIC(...) panic_spin(__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef NDEBUG
#define ASSERT(CONDITION) ((void)0)
#else
#define ASSERT(CONDITION)      \
    do                         \
    {                          \
        if (!(CONDITION))      \
        {                      \
            PANIC(#CONDITION); \
        }                      \
    } while (0)
#endif

#endif