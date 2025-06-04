#include "memory.h"
#include "../lib/kernel/bitmap.h"
#include "../lib/stdint.h"
#include "global.h"
#include "debug.h"
#include "../lib/kernel/print.h"
#include "../lib/string.h"

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
};

struct pool kernel_pool, user_pool;
struct virtual_addr kernel_vaddr;

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页,
 * 成功则返回虚拟页的起始地址, 失败则返回NULL */
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    uint32_t vaddr_start = 0;
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
    }
    return (void *)vaddr_start;
}

uint32_t *pte_ptr(uint32_t vaddr)
{
    uint32_t pte = (uint32_t *)(0xffc00000 +
                                ((vaddr & 0xffc00000) >> 10) +
                                PTE_IDX(vaddr) * 4); // Calculate the address of the Page Table Entry
    return pte;                                      // Return the pointer to the Page Table Entry
}

uint32_t *pde_ptr(uint32_t vaddr)
{
    uint32_t pde = (uint32_t *)(0xfffff000 + PDE_IDX(vaddr) * 4); // Calculate the address of the Page Directory Entry
    return pde;                                                   // Return the pointer to the Page Directory Entry
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

static void page_table_add(uint32_t _vaddr, uint32_t _page_phyaddr)
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
            *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // Set the Page Table Entry with the physical address and flags
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
        page_table_add(vaddr, (uint32_t)page_phyaddr); // Add the page to the page table
        vaddr += PG_SIZE;                              // Move to the next virtual address
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

void mem_init(void)
{
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00)); // Get total memory size from BIOS

    mem_pool_init(mem_bytes_total); // Initialize memory pools
    put_str("mem_init done\n");
}