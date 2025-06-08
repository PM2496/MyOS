#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "../lib/kernel/list.h"
#include "../lib/stdint.h"
#include "thread.h"

struct semaphore
{
    uint8_t value;       // 信号量的值
    struct list waiters; // 等待信号量的线程队列
};

struct lock
{
    struct task_struct *holder; // 持有锁的线程
    struct semaphore semaphore; // 信号量用于实现互斥锁
    uint32_t holder_repeat_nr;  // 持有锁的线程重复获取锁的次数
};

void sema_init(struct semaphore *sema, uint8_t value);
void sema_down(struct semaphore *sema);
void sema_up(struct semaphore *sema);
void lock_init(struct lock *plock);
void lock_acquire(struct lock *plock);
void lock_release(struct lock *plock);

#endif
