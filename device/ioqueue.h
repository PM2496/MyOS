#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "../lib/stdint.h"
#include "../thread/thread.h"
#include "../thread/sync.h"

#define bufsize 64

struct ioqueue
{
    struct lock lock;             // 互斥锁
    struct task_struct *producer; // 生产者线程
    struct task_struct *consumer; // 消费者线程
    char buf[bufsize];            // 缓冲区
    int32_t head;                 // 缓冲区头部索引
    int32_t tail;                 // 缓冲区尾部索引
};

void ioqueue_init(struct ioqueue *ioq);
bool ioq_full(struct ioqueue *ioq);
bool ioq_empty(struct ioqueue *ioq);
char ioq_getchar(struct ioqueue *ioq);
void ioq_putchar(struct ioqueue *ioq, char byte);

#endif