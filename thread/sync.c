#include "sync.h"
#include "../lib/kernel/list.h"
#include "../kernel/global.h"
#include "../kernel/debug.h"
#include "../kernel/interrupt.h"

void sema_init(struct semaphore *sema, uint8_t value)
{
    sema->value = value;       // Initialize the semaphore value
    list_init(&sema->waiters); // Initialize the waiters list
}

void lock_init(struct lock *plock)
{
    plock->holder = NULL;            // Initialize the holder to NULL
    sema_init(&plock->semaphore, 1); // Initialize the semaphore with value 1
    plock->holder_repeat_nr = 0;     // Initialize the holder repeat number to 0
}

void sema_down(struct semaphore *psema)
{
    enum intr_status old_status = intr_disable(); // Disable interrupts
    while (psema->value == 0)                     // Wait until the semaphore value is greater than 0
    {
        ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag)); // Ensure current thread is not already in waiters list

        if (elem_find(&psema->waiters, &running_thread()->general_tag))
        {
            PANIC("sema_down: thread already in waiters list\n"); // Panic if current thread is already in waiters list
        }
        list_append(&psema->waiters, &running_thread()->general_tag); // Add current thread to waiters list
        thread_block(TASK_BLOCKED);                                   // Block the current thread
    }

    psema->value--;              // Decrease the semaphore value
    ASSERT(psema->value == 0);   // Ensure semaphore value is not negative
    intr_set_status(old_status); // Restore previous interrupt status
};

void sema_up(struct semaphore *psema)
{
    enum intr_status old_status = intr_disable(); // Disable interrupts
    ASSERT(psema->value == 0);                    // Ensure semaphore value is 0 before releasing

    if (!list_empty(&psema->waiters)) // If there are threads waiting for the semaphore
    {
        struct task_struct *thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters)); // Get the thread from the list element
        thread_unblock(thread_blocked);                                                                              // Unblock the thread
    }

    psema->value++;              // Increase the semaphore value
    ASSERT(psema->value == 1);   // Ensure semaphore value is positive
    intr_set_status(old_status); // Restore previous interrupt status
}

void lock_acquire(struct lock *plock)
{
    if (plock->holder != running_thread()) // If the current thread does not hold the lock
    {
        sema_down(&plock->semaphore);         // Acquire the semaphore
        plock->holder = running_thread();     // Set the current thread as the holder of the lock
        ASSERT(plock->holder_repeat_nr == 0); // Ensure holder repeat number is 0
        plock->holder_repeat_nr = 1;          // Initialize holder repeat number to 1
    }
    else
    {
        plock->holder_repeat_nr++; // If the current thread already holds the lock, increment the repeat number
    }
}

void lock_release(struct lock *plock)
{
    ASSERT(plock->holder == running_thread()); // Ensure the current thread holds the lock
    if (plock->holder_repeat_nr > 1)           // If the holder repeat number is greater than 1
    {
        plock->holder_repeat_nr--; // Decrement the repeat number

        return;
    }
    ASSERT(plock->holder_repeat_nr == 1); // Ensure holder repeat number is 1

    plock->holder = NULL;        // Set the holder to NULL
    plock->holder_repeat_nr = 0; // Reset the holder repeat number
    sema_up(&plock->semaphore);
}