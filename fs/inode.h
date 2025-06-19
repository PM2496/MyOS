#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "../lib/stdint.h"
#include "../lib/kernel/list.h"
#include "../kernel/global.h"
#include "../device/ide.h"

struct inode
{
    uint32_t i_no;
    uint32_t i_size;            // 文件大小
    uint32_t i_open_cnts;       // 文件打开次数
    bool write_deny;            // 是否写保护
    uint32_t i_sectors[13];     // 直接块和一级间接块
    struct list_elem inode_tag; // 用于链表管理inode
};

struct inode *inode_open(struct partition *part, uint32_t inode_no);
void inode_sync(struct partition *part, struct inode *inode, void *io_buf);
void inode_init(uint32_t inode_no, struct inode *new_inode);
void inode_close(struct inode *inode);

#endif