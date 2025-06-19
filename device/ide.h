#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H

#include "../lib/stdint.h"
#include "../thread/sync.h"
#include "../lib/kernel/bitmap.h"
#include "../lib/kernel/list.h"
#include "../fs/super_block.h"

struct partition
{
    uint32_t start_lba;         // Start LBA of the partition
    uint32_t sec_cnt;           // Number of sectors in the partition
    struct disk *my_disk;       // Pointer to the disk this partition belongs to
    struct list_elem part_tag;  // List element for linking partitions
    char name[8];               // Partition name, e.g., "sda1", "sda2"
    struct super_block *sb;     // Pointer to the superblock of the partition
    struct bitmap block_bitmap; // Bitmap for managing blocks in the partition
    struct bitmap inode_bitmap; // Bitmap for managing inodes in the partition
    struct list open_inodes;    // List of open inodes in the partition
};

struct disk
{
    char name[8];                    // Disk name, e.g., "sda", "sdb"
    struct ide_channel *my_channel;  // Pointer to the IDE channel this disk belongs to
    uint8_t dev_no;                  // Device number, 0 for master, 1 for slave
    struct partition prim_parts[4];  // Primary partitions
    struct partition logic_parts[8]; // Logical partitions
};

struct ide_channel
{
    char name[8];               // Channel name, e.g., "ide0", "ide1"
    uint16_t port_base;         // Base port address for the channel
    uint8_t irq_no;             // IRQ number for the channel
    struct lock lock;           // Lock for synchronizing access to the channel
    bool expecting_intr;        // Flag to indicate if an interrupt is expected
    struct semaphore disk_done; // Semaphore to signal when a disk operation is done
    struct disk devices[2];     // Two devices (master and slave) on the channel
};

void intr_hd_handler(uint8_t irq_no); // IDE interrupt handler
void ide_init(void);
extern uint8_t channel_cnt;           // Number of IDE channels
extern struct ide_channel channels[]; // Array of IDE channels
extern struct list partition_list;    // List of all partitions
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
#endif