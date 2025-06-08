#include "ioqueue.h"
#include "../kernel/interrupt.h"
#include "../kernel/global.h"
#include "../kernel/debug.h"

void ioqueue_init(struct ioqueue *ioq)
{
    lock_init(&ioq->lock); // Initialize the lock
    ioq->producer = NULL;  // No producer thread yet
    ioq->consumer = NULL;  // No consumer thread yet
    ioq->head = 0;         // Initialize head index
    ioq->tail = 0;         // Initialize tail index
}

static int32_t next_pos(int32_t pos)
{
    return (pos + 1) % bufsize; // Calculate the next position in a circular buffer
}

bool ioq_full(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);   // Ensure interrupts are disabled
    return next_pos(ioq->head) == ioq->tail; // Check if the buffer is full
}

bool ioq_empty(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF); // Ensure interrupts are disabled
    return ioq->head == ioq->tail;         // Check if the buffer is empty
}

static void ioq_wait(struct task_struct **waiter)
{
    ASSERT(*waiter == NULL && waiter != NULL); // Ensure the waiter is not already set
    *waiter = running_thread();                // Set the current thread as the waiter
    thread_block(TASK_BLOCKED);                // Block the current thread
}

static void wakeup(struct task_struct **waiter)
{
    ASSERT(*waiter != NULL); // Ensure the waiter is set
    thread_unblock(*waiter); // Unblock the waiting thread
    *waiter = NULL;          // Clear the waiter pointer
}

char ioq_getchar(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF); // Ensure interrupts are disabled
    while (ioq_empty(ioq))                 // Wait until the queue is not empty
    {
        lock_acquire(&ioq->lock); // Acquire the lock
        ioq_wait(&ioq->consumer); // Wait for a character to be available
        lock_release(&ioq->lock); // Release the lock
    }

    char byte = ioq->buf[ioq->tail]; // Get the character from the buffer
    ioq->tail = next_pos(ioq->tail); // Move the tail index to the next position

    if (ioq->producer != NULL) // If there is a producer waiting
    {
        wakeup(&ioq->producer); // Wake up the producer thread
    }

    return byte; // Return the character
}

void ioq_putchar(struct ioqueue *ioq, char byte)
{
    ASSERT(intr_get_status() == INTR_OFF); // Ensure interrupts are disabled
    while (ioq_full(ioq))                  // Wait until the queue is not full
    {
        lock_acquire(&ioq->lock); // Acquire the lock
        ioq_wait(&ioq->producer); // Wait for space in the queue
        lock_release(&ioq->lock); // Release the lock
    }

    ioq->buf[ioq->head] = byte;      // Put the character into the buffer
    ioq->head = next_pos(ioq->head); // Move the head index to the next position

    if (ioq->consumer != NULL) // If there is a consumer waiting
    {
        wakeup(&ioq->consumer); // Wake up the consumer thread
    }
}