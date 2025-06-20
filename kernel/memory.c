#include "memory.h"
#include "../lib/kernel/bitmap.h"
#include "../lib/stdint.h"
#include "global.h"
#include "debug.h"
#include "../lib/kernel/print.h"
#include "../lib/string.h"
#include "../thread/sync.h"
#include "interrupt.h"
#include "../lib/kernel/list.h"

#define PG_SIZE 4096 // Page size in bytes

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) // Page Directory Entry index
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12) // Page Table Entry index

#define MEM_BITMAP_BASE 0xc009a000 // Base address for the memory bitmap

#define K_HEAP_START 0xc0100000 // Start address for the kernel heap

struct pool
{
    struct bitmap pool_bitmap; // Bitmap for the memory pool
    uint32_t phy_addr_start;   // Start address of the physical memory area
    uint32_t pool_size;        // Size of the memory pool in bytes
    struct lock lock;          // 申请内存时互斥
};

struct arena
{
    struct mem_block_desc *desc; // Pointer to the memory block descriptor
    /* large为true时，cnt表示页框数
     * 否则cnt表示空闲mem_block数量 */
    uint32_t cnt;
    bool large;
};

struct mem_block_desc k_block_descs[DESC_CNT];

struct pool kernel_pool, user_pool;
struct virtual_addr kernel_vaddr;

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页,
 * 成功则返回虚拟页的起始地址, 失败则返回NULL */
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int vaddr_start = 0;
    int bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL)
    {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
        {
            return NULL; // No free virtual address found
        }
        while (cnt < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE; // Calculate the start address of the virtual memory area
    }
    else
    {
        // User pool handling can be added here
        struct task_struct *cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
        {
            return NULL; // No free virtual address found
        }

        while (cnt < pg_cnt)
        {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1); // Set the bits in the user virtual address bitmap
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE; // Calculate the start address of the user virtual memory area

        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE)); // Ensure the virtual address is below the kernel space
    }
    return (void *)vaddr_start;
}

uint32_t *pte_ptr(uint32_t vaddr)
{
    uint32_t *pte = (uint32_t *)(0xffc00000 +
                                 ((vaddr & 0xffc00000) >> 10) +
                                 PTE_IDX(vaddr) * 4); // Calculate the address of the Page Table Entry
    return pte;                                       // Return the pointer to the Page Table Entry
}

uint32_t *pde_ptr(uint32_t vaddr)
{
    uint32_t *pde = (uint32_t *)(0xfffff000 + PDE_IDX(vaddr) * 4); // Calculate the address of the Page Directory Entry
    return pde;                                                    // Return the pointer to the Page Directory Entry
}

/* 在m_pool指向的物理内存池中分配1个物理页,
 * 成功则返回页框的物理地址,失败则返回NULL */
static void *palloc(struct pool *m_pool)
{
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1)
    {
        return NULL; // No free page found
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start); // Calculate the physical address of the allocated page
    return (void *)page_phyaddr;
}

static void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr;               // Ensure vaddr is treated as a 32-bit unsigned integer
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr; // Ensure page_phyaddr is treated as a 32-bit unsigned integer
    uint32_t *pde = pde_ptr(vaddr);                  // Get the pointer to the Page Directory Entry
    uint32_t *pte = pte_ptr(vaddr);                  // Get the pointer to the Page Table Entry

    if (*pde & 0x00000001)
    {
        ASSERT(!(*pte & 0x00000001)); // Ensure the Page Table Entry is not already present

        if (!(*pte & 0x00000001))
        {
            *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // Set the Page Table Entry with the physical address and flags
        }
        else
        {
            PANIC("pte repeat");
            // *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // Set the Page Table Entry with the physical address and flags
        }
    }
    else
    {
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool); // Allocate a new page for the Page Directory Entry
        *pde = pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1;       // Set the Page Directory Entry with the physical address and flags

        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE); // Clear the allocated page

        ASSERT(!(*pte & 0x00000001));                     // Ensure the Page Table Entry is not already present
        *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // Set the Page Table Entry with the physical address and flags
    }
}

void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840); // Ensure the page count is within valid range

    void *vaddr_start = vaddr_get(pf, pg_cnt); // Get the starting virtual address for the requested pages
    if (vaddr_start == NULL)
    {
        return NULL; // No free virtual address found
    }

    uint32_t vaddr = (uint32_t)vaddr_start; // Ensure vaddr is treated as a 32-bit unsigned integer
    uint32_t cnt = pg_cnt;                  // Number of pages to allocate

    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool; // Determine the memory pool based on the flags

    while (cnt-- > 0)
    {
        void *page_phyaddr = palloc(mem_pool); // Allocate a physical page from the memory pool
        if (page_phyaddr == NULL)
        {
            return NULL; // No free physical page found
        }
        page_table_add((void *)vaddr, page_phyaddr); // Add the page to the page table
        vaddr += PG_SIZE;                            // Move to the next virtual address
    }

    return vaddr_start; // Return the starting virtual address of the allocated pages
}

void *get_kernel_pages(uint32_t pg_cnt)
{
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt); // Allocate kernel pages
    if (vaddr != NULL)
    {
        memset(vaddr, 0, pg_cnt * PG_SIZE); // Clear the allocated pages
    }
    return vaddr; // Return the starting virtual address of the allocated kernel pages
}

void *get_user_pages(uint32_t pg_cnt)
{
    lock_acquire(&user_pool.lock);              // Acquire the lock for the user pool
    void *vaddr = malloc_page(PF_USER, pg_cnt); // Allocate user pages
    memset(vaddr, 0, pg_cnt * PG_SIZE);         // Clear the allocated pages
    lock_release(&user_pool.lock);              // Release the lock for the user pool
    return vaddr;                               // Return the starting virtual address of the allocated user pages
}

void *get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool; // Determine the memory pool based on the flags

    lock_acquire(&mem_pool->lock); // Acquire the lock for the memory pool

    struct task_struct *cur = running_thread(); // Get the currently running thread
    int32_t bit_idx = -1;

    if (cur->pgdir != NULL && pf == PF_USER)
    {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE; // Calculate the bit index for user virtual address
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1); // Set the bit in the user virtual address bitmap
    }
    else if (cur->pgdir == NULL && pf == PF_KERNEL)
    {
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE; // Calculate the bit index for kernel virtual address
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1); // Set the bit in the kernel virtual address bitmap
    }
    else
    {
        PANIC("get_a_page: not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void *page_phyaddr = palloc(mem_pool); // Allocate a physical page from the memory pool
    if (page_phyaddr == NULL)
    {
        return NULL; // No free physical page found
    }

    page_table_add((void *)vaddr, page_phyaddr); // Add the page to the page table
    lock_release(&mem_pool->lock);               // Release the lock for the memory pool
    return (void *)vaddr;                        // Return the virtual address of the allocated page
}

/* 安装1页大小的vaddr,专门针对fork时虚拟地址位图无须操作的情况 */
void *get_a_page_without_opvaddrbitmap(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);
    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
    {
        lock_release(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void *)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}

uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr); // Get the pointer to the Page Table Entry

    return (*pte & 0xfffff000) + (vaddr & 0x00000fff); // Return the physical address corresponding to the virtual address
}

static void mem_pool_init(uint32_t all_mem)
{
    put_str("   mem_pool_init start\n");
    uint32_t page_table_size = PG_SIZE * 256; // Size of a page table

    uint32_t used_mem = page_table_size + 0x100000; // Reserve space for page tables and kernel code/data
    uint32_t free_mem = all_mem - used_mem;         // Calculate free memory
    uint16_t all_free_pages = free_mem / PG_SIZE;   // Total number of free pages

    uint16_t kernel_free_pages = all_free_pages / 2;               // Half for kernel pool
    uint16_t user_free_pages = all_free_pages - kernel_free_pages; // Remaining for user pool

    uint32_t kbm_length = kernel_free_pages / 8; // Length of the kernel bitmap in bytes
    uint32_t ubm_length = user_free_pages / 8;   // Length of the user bitmap in bytes

    uint32_t kp_start = used_mem;                               // Start address for the kernel pool
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // Start address for the user pool

    kernel_pool.phy_addr_start = kp_start; // Set the physical address start for the kernel pool
    user_pool.phy_addr_start = up_start;   // Set the physical address start for the user pool

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE; // Set the size of the kernel pool
    user_pool.pool_size = user_free_pages * PG_SIZE;     // Set the size of the user pool

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length; // Set the bitmap length for the kernel pool
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;   // Set the bitmap length for the user pool

    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;              // Set the bitmap bits for the kernel pool
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length); // Set the bitmap bits for the user pool

    /******************** 输出内存池信息 **********************/
    put_str("      kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str("\n");
    put_str("      kernel_pool_bitmap_end:");
    put_int((int)kernel_pool.pool_bitmap.bits + kernel_pool.pool_bitmap.btmp_bytes_len);
    put_str("\n");
    put_str("       kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("       kernel_pool_phy_addr_end:");
    put_int(kernel_pool.phy_addr_start + kernel_pool.pool_size);
    put_str("\n");
    put_str("      user_pool_bitmap_start:");

    put_int((int)user_pool.pool_bitmap.bits);
    put_str("\n");
    put_str("      user_pool_bitmap_end:");
    put_int((int)user_pool.pool_bitmap.bits + user_pool.pool_bitmap.btmp_bytes_len);
    put_str("\n");
    put_str("       user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");
    put_str("       user_pool_phy_addr_end:");
    put_int(user_pool.phy_addr_start + user_pool.pool_size);
    put_str("\n");

    bitmap_init(&kernel_pool.pool_bitmap); // Initialize the kernel pool bitmap
    bitmap_init(&user_pool.pool_bitmap);   // Initialize the user pool bitmap

    lock_init(&kernel_pool.lock); // Initialize the lock for the kernel pool
    lock_init(&user_pool.lock);   // Initialize the lock for the user pool

    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length; // Set the bitmap length for virtual addresses

    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length); // Set the bitmap bits for virtual addresses
    kernel_vaddr.vaddr_start = K_HEAP_START;

    put_str("     kernel_vaddr.vaddr_bitmap.start:");
    put_int((int)kernel_vaddr.vaddr_bitmap.bits);
    put_str("\n");
    put_str("     kernel_vaddr.vaddr_bitmap.end:");
    put_int((int)kernel_vaddr.vaddr_bitmap.bits + kernel_vaddr.vaddr_bitmap.btmp_bytes_len);
    put_str("\n"); // Set the start address for the kernel virtual memory area

    bitmap_init(&kernel_vaddr.vaddr_bitmap); // Initialize the virtual address bitmap
    put_str("   mem_pool_init done\n");
}

void block_desc_init(struct mem_block_desc *desc_array)
{
    uint16_t desc_idx, block_size = 16; // Start with a block size of 16 bytes

    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
    {
        desc_array[desc_idx].block_size = block_size;                                          // Set the block size for the descriptor
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size; // Calculate the number of blocks per arena
        list_init(&desc_array[desc_idx].free_list);                                            // Initialize the free list for the descriptor
        block_size *= 2;                                                                       // Double the block size for the next descriptor
    }
}

/* 返回arena中第idx个内存块的地址 */
static struct mem_block *arena2block(struct arena *a, uint32_t idx)
{
    return (struct mem_block *)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

static struct arena *block2arena(struct mem_block *b)
{
    return (struct arena *)((uint32_t)b & 0xfffff000); // Calculate the arena address from the block address
}

void *sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    uint32_t pool_size;
    struct mem_block_desc *desc;
    struct task_struct *cur_thread = running_thread(); // Get the currently running thread

    if (cur_thread->pgdir == NULL) // If the current thread is a kernel thread
    {
        PF = PF_KERNEL;                    // Set the pool flag to kernel
        pool_size = kernel_pool.pool_size; // Get the size of the kernel memory pool
        mem_pool = &kernel_pool;           // Use the kernel memory pool
        desc = k_block_descs;              // Use the kernel block descriptors
    }
    else // If the current thread is a user thread
    {
        PF = PF_USER;                    // Set the pool flag to user
        pool_size = user_pool.pool_size; // Get the size of the user memory pool
        mem_pool = &user_pool;           // Use the user memory pool
        desc = cur_thread->u_block_desc; // Use the user block descriptors
    }

    if (!(size > 0 && size < pool_size)) // Check if the requested size is valid
    {
        return NULL; // Invalid size, return NULL
    }
    struct arena *a;
    struct mem_block *b;
    lock_acquire(&mem_pool->lock); // Acquire the lock for the memory pool

    if (size > 1024)
    {
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE); // Calculate the number of pages needed
        a = malloc_page(PF, page_cnt);                                          // Allocate memory pages
        if (a != NULL)
        {
            memset(a, 0, page_cnt * PG_SIZE); // Clear the allocated pages

            a->desc = NULL;                // Set the descriptor to NULL for large allocations
            a->cnt = page_cnt;             // Set the count of pages in the arena
            a->large = true;               // Mark this arena as large
            lock_release(&mem_pool->lock); // Release the lock after allocation
            return (void *)(a + 1);
        }
        else
        {
            lock_release(&mem_pool->lock); // Release the lock if allocation fails
            return NULL;                   // Allocation failed, return NULL
        }
    }
    else
    {
        uint8_t desc_idx;

        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
        {
            if (size <= desc[desc_idx].block_size) // Find the appropriate block descriptor
            {
                break; // Found the descriptor for the requested size
            }
        }

        if (list_empty(&desc[desc_idx].free_list)) // Check if the free list for this descriptor is empty
        {
            a = malloc_page(PF, 1); // Allocate one page for the arena
            if (a == NULL)
            {
                lock_release(&mem_pool->lock); // Release the lock if allocation fails
                return NULL;                   // Allocation failed, return NULL
            }
            memset(a, 0, PG_SIZE); // Clear the allocated page

            a->desc = &desc[desc_idx];                // Set the descriptor for this arena
            a->cnt = desc[desc_idx].blocks_per_arena; // Set the count of blocks in the arena
            a->large = false;                         // Mark this arena as not large

            uint32_t block_idx;

            enum intr_status old_status = intr_disable(); // Disable interrupts to ensure atomicity

            for (block_idx = 0; block_idx < desc[desc_idx].blocks_per_arena; block_idx++)
            {
                b = arena2block(a, block_idx);                          // Get the block address
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem)); // Ensure the block is not already in the free list
                list_append(&a->desc->free_list, &b->free_elem);        // Add the block to the free list
            }
            intr_set_status(old_status); // Restore the previous interrupt status
        }
        b = elem2entry(struct mem_block,
                       free_elem,
                       list_pop(&(desc[desc_idx].free_list))); // Get a free block from the free list
        memset(b, 0, desc[desc_idx].block_size);               // Clear the allocated block
        a = block2arena(b);                                    // Get the arena from the block
        a->cnt--;                                              // Decrease the count of free blocks in the arena
        lock_release(&mem_pool->lock);                         // Release the lock after allocation
        return (void *)b;                                      // Return the address of the allocated block
    }
}

void pfree(uint32_t pg_phy_addr)
{
    struct pool *mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        mem_pool = &user_pool;                                        // Determine the memory pool based on the physical address
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE; // Calculate the bit index for user pool
    }
    else
    {
        mem_pool = &kernel_pool;                                        // Determine the memory pool based on the physical address
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE; // Calculate the bit index for kernel pool
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0); // Clear the bit in the memory pool bitmap
}

static void page_table_pte_remove(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;                                     // 将页表项pte的P位置置0
    asm volatile("invlpg %0" : : "m"(vaddr) : "memory"); // Flush the TLB entry for the virtual address
}

static void vaddr_remove(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t bit_idx_start = 0;
    uint32_t vaddr = (uint32_t)_vaddr; // Ensure vaddr is treated as a 32-bit unsigned integer
    uint32_t cnt = 0;

    if (pf == PF_KERNEL)
    {
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE; // Calculate the bit index for kernel virtual address
        while (cnt < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0); // Clear the bits in the kernel virtual address bitmap
        }
    }
    else
    {
        struct task_struct *cur_thread = running_thread();
        bit_idx_start = (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE; // Calculate the bit index for user virtual address
        while (cnt < pg_cnt)
        {
            bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0); // Clear the bits in the user virtual address bitmap
        }
    }
}

void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr; // Ensure vaddr is treated as a 32-bit unsigned integer
    uint32_t page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0); // Ensure the page count is within valid range
    pg_phy_addr = addr_v2p(vaddr);               // Convert the virtual address to physical address

    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000); // Ensure the physical address is page-aligned
    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;              // Move to the next virtual address
            pg_phy_addr = addr_v2p(vaddr); // Convert the new virtual address to physical address

            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start); // Ensure the physical address is page-aligned
            pfree(pg_phy_addr);                                                              // Free the physical page
            page_table_pte_remove(vaddr);                                                    // Remove the page table entry for the virtual address
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt); // Remove the virtual address from the bitmap
    }
    else
    {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;              // Move to the next virtual address
            pg_phy_addr = addr_v2p(vaddr); // Convert the new virtual address to physical address

            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr < user_pool.phy_addr_start && pg_phy_addr >= kernel_pool.phy_addr_start); // Ensure the physical address is page-aligned
            pfree(pg_phy_addr);                                                                                                          // Free the physical page
            page_table_pte_remove(vaddr);                                                                                                // Remove the page table entry for the virtual address
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt); // Remove the virtual address from the bitmap
    }
}

void sys_free(void *ptr)
{
    ASSERT(ptr != NULL); // Ensure the pointer is not NULL
    if (ptr != NULL)
    {
        enum pool_flags pf;
        struct pool *mem_pool;
        if (running_thread()->pgdir == NULL) // If the current thread is a kernel thread
        {
            ASSERT((uint32_t)ptr >= K_HEAP_START); // Ensure the pointer is within the kernel heap range
            pf = PF_KERNEL;                        // Set the pool flag to kernel
            mem_pool = &kernel_pool;               // Use the kernel memory pool
        }
        else // If the current thread is a user thread
        {
            pf = PF_USER;          // Set the pool flag to user
            mem_pool = &user_pool; // Use the user memory pool
        }

        lock_acquire(&mem_pool->lock);    // Acquire the lock for the memory pool
        struct mem_block *b = ptr;        // Treat the pointer as a memory block
        struct arena *a = block2arena(b); // Get the arena from the memory block

        ASSERT(a->large == 0 || a->large == 1);
        if (a->desc == NULL && a->large == true)
        {
            mfree_page(pf, a, a->cnt); // If the arena is large, free the entire arena
        }
        else
        {
            list_append(&a->desc->free_list, &b->free_elem); // If the arena is not large, add the block back to the free list
            if (++a->cnt == a->desc->blocks_per_arena)       // If all blocks in the arena are free
            {
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++)
                {
                    struct mem_block *b = arena2block(a, block_idx);       // Get each block in the arena
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem)); // Ensure the block is in the free list
                    list_remove(&b->free_elem);                            // Remove the block from the free list
                }
                mfree_page(pf, a, 1); // Free the entire arena if all blocks are free
            }
        }
        lock_release(&mem_pool->lock); // Release the lock for the memory pool
    }
}

void mem_init(void)
{
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00)); // Get total memory size from BIOS

    mem_pool_init(mem_bytes_total); // Initialize memory pools
    block_desc_init(k_block_descs); // Initialize memory block descriptors
    put_str("mem_init done\n");
}