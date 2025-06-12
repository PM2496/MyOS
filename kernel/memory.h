#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "../lib/stdint.h"
#include "../lib/kernel/bitmap.h"
#include "../lib/kernel/list.h"

enum pool_flags
{
    PF_KERNEL = 1, // Kernel memory pool
    PF_USER = 2    // User memory pool
};

#define PG_P_1 1
#define PG_P_0 0
#define PG_RW_R 0
#define PG_RW_W 2
#define PG_US_S 0
#define PG_US_U 4

struct virtual_addr
{
    struct bitmap vaddr_bitmap; // Bitmap for virtual addresses
    uint32_t vaddr_start;       // Start address of the virtual memory area
};

struct mem_block
{
    struct list_elem free_elem; // List element for linking blocks
};

struct mem_block_desc
{
    uint32_t block_size;       // Size of each memory block
    uint32_t blocks_per_arena; // Number of blocks in an arena
    struct list free_list;     // List of free memory blocks
};

#define DESC_CNT 7 // Number of memory block descriptors

extern struct pool kernel_pool, user_pool;
void mem_init(void);
void *get_kernel_pages(uint32_t pg_cnt);
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t *pte_ptr(uint32_t vaddr);
uint32_t *pde_ptr(uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void *get_user_pages(uint32_t pg_cnt);
void *get_a_page(enum pool_flags pf, uint32_t vaddr);
void block_desc_init(struct mem_block_desc *desc_array);
void *sys_malloc(uint32_t size);
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt);
void pfree(uint32_t pg_phy_addr);
void sys_free(void *ptr);

#endif