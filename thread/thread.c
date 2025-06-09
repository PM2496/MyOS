#include "thread.h"
#include "../lib/stdint.h"
#include "../lib/string.h"
#include "../kernel/global.h"
#include "../kernel/debug.h"
#include "../kernel/interrupt.h"
#include "../lib/kernel/print.h"
#include "../kernel/memory.h"
#include "../userprog/process.h"

#define PG_SIZE 4096

struct task_struct *main_thread;     // 主线程的pcb
struct list thread_ready_list;       // 就绪线程队列
struct list thread_all_list;         // 所有线程队列
static struct list_elem *thread_tag; // 用于遍历线程链表的指针

extern void switch_to(struct task_struct *cur, struct task_struct *next);

/* 获取当前线程pcb指针 */
struct task_struct *running_thread(void)
{
    uint32_t esp;
    asm("movl %%esp, %0" : "=g"(esp)); // 获取当前线程的栈指针
    /* 取esp整数部分即pcb起始地址 */
    return (struct task_struct *)(esp & 0xfffff000); // 返回pcb地址
}

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func *function, void *func_arg)
{
    /* 执行function前要开中断,避免后面的时钟中断被屏蔽,而无法调度其它线程 */
    intr_enable();
    /* 执行函数 */
    function(func_arg);
}

/* 初始化线程栈thread_stack，将待执行的函数和参数放到thread_stack中相应的位置 */
void thread_create(struct task_struct *pthread, thread_func *function, void *func_arg)
{
    pthread->self_kstack -= sizeof(struct intr_stack);
    pthread->self_kstack -= sizeof(struct thread_stack);
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack;
    kthread_stack->eip = kernel_thread; // eip指向kernel_thread
    kthread_stack->function = function; // 待执行的函数
    kthread_stack->func_arg = func_arg; // 函数参数
    kthread_stack->ebp = 0;             // ebp初始化为0
    kthread_stack->ebx = 0;             // ebx初始化为0
    kthread_stack->edi = 0;             // edi初始化为0
    kthread_stack->esi = 0;             // esi初始化为0
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct *pthread, char *name, int prio)
{
    memset(pthread, 0, sizeof(*pthread)); // 清空线程结构体
    strcpy(pthread->name, name);          // 复制线程名

    if (pthread == main_thread)
    {
        pthread->status = TASK_RUNNING; // 主线程状态为运行中
    }
    else
    {
        pthread->status = TASK_READY; // 其它线程状态为就绪
    }

    /* self_kstack是线程自己在内核态下使用的栈顶地址 */
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE); // 栈顶地址为pcb起始地址加一页大小
    pthread->priority = prio;                                         // 设置线程优先级
    pthread->ticks = prio;                                            // 初始化时间片为优先级
    pthread->elapsed_ticks = 0;                                       // 已运行时间片数初始化为0
    pthread->pgdir = NULL;                                            // 进程页目录初始化为NULL
    pthread->stack_magic = 0x19870916;                                // 栈边界标记，用于检测栈溢出
}

/* 创建一优先级为prio的线程,线程名为name,线程所执行的函数是function(func_arg) */
/*
 *  name:线程名
 *  prio:线程优先级
 *  function:执行函数
 *  func_arg:函数参数
 * */

struct task_struct *thread_start(char *name, int prio, thread_func *function, void *func_arg)
{
    struct task_struct *thread = get_kernel_pages(1); // 分配一页内存作为线程的pcb
    init_thread(thread, name, prio);                  // 初始化线程基本信息

    /* 创建线程栈 */
    thread_create(thread, function, func_arg);

    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));

    list_append(&thread_ready_list, &thread->general_tag); // 将线程添加到就绪队列

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));

    list_append(&thread_all_list, &thread->all_list_tag); // 将线程添加到所有线程队列

    // asm volatile("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret"
    //              :                          /* no output */
    //              : "g"(thread->self_kstack) // 将线程的内核栈地址加载到esp
    //              : "memory");

    /* 返回新创建的线程pcb */
    return thread;
}

static void make_main_thread(void)
{
    main_thread = running_thread();       // 获取当前线程pcb
    init_thread(main_thread, "main", 31); // 初始化主线程

    /* main函数是当前线程,当前线程不在thread_ready_list中,
     * 所以只将其加在thread_all_list中. */
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

void schedule(void)
{
    ASSERT(intr_get_status() == INTR_OFF);      // 确保在关中断状态下调用调度函数
    struct task_struct *cur = running_thread(); // 获取当前线程pcb

    if (cur->status == TASK_RUNNING) // 如果当前线程是运行中状态
    {
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag)); // 确保当前线程不在就绪队列中
        list_append(&thread_ready_list, &cur->general_tag);        // 将当前线程添加到就绪队列
        cur->status = TASK_READY;
        cur->ticks = cur->priority; // 则将其状态改为就绪
    }
    else
    {
        /* 若此线程需要某事件发生后才能继续上cpu运行,
      不需要将其加入队列,因为当前线程不在就绪队列中。*/
    }

    ASSERT(!list_empty(&thread_ready_list)); // 确保就绪队列不为空
    thread_tag = NULL;                       // 清空遍历指针

    /* 将thread_ready_list队列中的第一个就绪线程弹出,准备将其调度上cpu. */
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag); // 获取下一个线程pcb
    next->status = TASK_RUNNING;                                                        // 将下一个线程状态设置为运行中
    process_activate(next);                                                             // 激活下一个线程的页表
    switch_to(cur, next);                                                               // 切换到下一个线程
}

void thread_block(enum task_status status)
{
    ASSERT(((status == TASK_BLOCKED) || (status == TASK_WAITING) || (status == TASK_HANGING)));
    enum intr_status old_status = intr_disable();      // 关中断
    struct task_struct *cur_thread = running_thread(); // 获取当前线程pcb
    cur_thread->status = status;                       // 设置当前线程状态
    schedule();                                        // 调度下一个线程

    intr_set_status(old_status); // 恢复中断状态
}

void thread_unblock(struct task_struct *pthread)
{
    enum intr_status old_status = intr_disable(); // 关中断
    ASSERT(pthread->status == TASK_BLOCKED || pthread->status == TASK_WAITING || pthread->status == TASK_HANGING);
    if (pthread->status != TASK_READY)                                 // 如果线程不是就绪状态
    {                                                                  // 将线程状态设置为就绪
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag)); // 确保线程不在就绪队列中
        if (elem_find(&thread_ready_list, &pthread->general_tag))
        {
            PANIC("thread_unblock: blocked thread in ready_list\n"); // 如果线程在就绪队列中，报错
        }
        list_push(&thread_ready_list, &pthread->general_tag); // 将线程添加到就绪队列
        pthread->status = TASK_READY;                         // 设置线程状态为就绪
    }

    intr_set_status(old_status); // 恢复中断状态
}

void thread_init(void)
{
    put_str("thread_init start\n");
    list_init(&thread_ready_list); // 初始化就绪线程队列
    list_init(&thread_all_list);   // 初始化所有线程队列

    make_main_thread(); // 创建主线程

    put_str("thread_init done\n");
}