#include "list.h"
#include "../../kernel/interrupt.h"

void list_init(struct list *plist)
{
    plist->head.prev = NULL;
    plist->head.next = &plist->tail;
    plist->tail.prev = &plist->head;
    plist->tail.next = NULL;
}

void list_insert_before(struct list_elem *before, struct list_elem *elem)
{
    enum intr_status old_status = intr_disable(); // Disable interrupts

    before->prev->next = elem; // Set the next of the previous element
    elem->prev = before->prev; // Set the previous of the new element
    elem->next = before;       // Set the next of the new element
    before->prev = elem;       // Set the previous of the before element to the new element

    intr_set_status(old_status); // Restore the previous interrupt status
}

void list_push(struct list *plist, struct list_elem *elem)
{
    list_insert_before(plist->head.next, elem);
}

void list_append(struct list *plist, struct list_elem *elem)
{
    list_insert_before(&plist->tail, elem);
}

void list_remove(struct list_elem *elem)
{
    enum intr_status old_status = intr_disable(); // Disable interrupts

    elem->prev->next = elem->next; // Set the next of the previous element
    elem->next->prev = elem->prev; // Set the previous of the next element

    intr_set_status(old_status); // Restore the previous interrupt status
}

struct list_elem *list_pop(struct list *plist)
{
    struct list_elem *elem = plist->head.next; // Get the first element
    list_remove(elem);                         // Remove the first element
    return elem;                               // Return the removed element
}

bool elem_find(struct list *plist, struct list_elem *obj_elem)
{
    struct list_elem *elem = plist->head.next; // Start from the head
    while (elem != &plist->tail)               // Traverse until the tail
    {
        if (elem == obj_elem) // If found
        {
            return true; // Return true
        }
        elem = elem->next; // Move to the next element
    }
    return false; // Not found, return false
}

struct list_elem *list_traversal(struct list *plist, function func, int arg)
{
    struct list_elem *elem = plist->head.next; // Start from the head
    if (list_empty(plist))                     // If the list is empty
    {
        return NULL; // Return NULL
    }

    while (elem != &plist->tail) // Traverse until the tail
    {
        if (func(elem, arg)) // If the function returns true
        {
            return elem; // Return the current element
        }
        elem = elem->next; // Move to the next element
    }
    return NULL; // Not found, return NULL
}

uint32_t list_len(struct list *plist)
{
    uint32_t length = 0;                       // Initialize length
    struct list_elem *elem = plist->head.next; // Start from the head

    while (elem != &plist->tail) // Traverse until the tail
    {
        length++;          // Increment length
        elem = elem->next; // Move to the next element
    }
    return length; // Return the total length
}

bool list_empty(struct list *plist)
{
    return (plist->head.next == &plist->tail ? true : false); // Check if the next of head is tail
}