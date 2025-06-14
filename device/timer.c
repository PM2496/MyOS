#include "timer.h"
#include "../lib/kernel/io.h"
#include "../lib/kernel/print.h"
#include "../kernel/interrupt.h"
#include "../thread/thread.h"
#include "../kernel/debug.h"

#define IRQ0_FREQUENCY 100
#define INPUT_FREQUENCY 1193180
#define COUNTER0_VALUE (INPUT_FREQUENCY / IRQ0_FREQUENCY)
#define COUNTER0_PORT 0x40
#define COUNTER0_NO 0
#define COUNTER0_MODE 2
#define READ_WRITE_LATCH 3
#define PIT_CONTROL_PORT 0x43
#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY) // 每次时钟中断的毫秒数

uint32_t ticks; // ticks是内核自中断开启以来总共的嘀嗒数，即时钟中断的次数

static void frequency_set(uint8_t counter_port,
                          uint8_t counter_no,
                          uint8_t rwl,
                          uint8_t counter_mode,
                          uint16_t counter_value)
{
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    outb(counter_port, (uint8_t)counter_value);      // Low byte
    outb(counter_port, (uint8_t)counter_value >> 8); // High byte
}

static void intr_timer_handler(void)
{
    struct task_struct *cur_thread = running_thread();

    ASSERT(cur_thread->stack_magic == 0x19870916); // 检查栈边界标记

    cur_thread->elapsed_ticks++; // 增加已运行的时间片数
    ticks++;                     // 增加总的ticks数

    if (cur_thread->ticks == 0) // 如果当前线程的时间片用完
    {
        schedule(); // 调度下一个线程
    }
    else
    {
        cur_thread->ticks--; // 否则减少当前线程的时间片
    }
}

static void ticks_to_sleep(uint32_t sleep_ticks)
{
    uint32_t start_tick = ticks;

    while (ticks - start_tick < sleep_ticks)
    {
        thread_yield();
    }
}

void mtime_sleep(uint32_t m_seconds)
{
    uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(sleep_ticks); // 将ticks转换为睡眠时间
}

void timer_init(void)
{
    put_str("timer_init start\n");
    frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER0_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler); // 注册时钟中断处理函数
    put_str("timer_init done\n");
}