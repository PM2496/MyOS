#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "../lib/stdint.h"
#include "inode.h"
#include "fs.h"

#define MAX_FILE_NAME_LEN 16 // 文件名最大长度

struct dir
{
    struct inode *inode;  // 目录对应的inode
    uint32_t dir_pos;     // 目录项读取位置
    uint8_t dir_buf[512]; // 目录项缓冲区
};

struct dir_entry
{
    char filename[MAX_FILE_NAME_LEN]; // 文件名
    uint32_t i_no;                    // 目录项对应的inode号
    enum file_types f_type;           // 文件类型
};

extern struct dir root_dir; // 根目录
void open_root_dir(struct partition *part);
struct dir *dir_open(struct partition *part, uint32_t inode_no);
void dir_close(struct dir *dir);
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e);
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de);
bool sync_dir_entry(struct dir *parent_dir, struct dir_entry *p_de, void *io_buf);
bool delete_dir_entry(struct partition *part, struct dir *pdir, uint32_t inode_no, void *io_buf);
struct dir_entry *dir_read(struct dir *dir);
bool dir_is_empty(struct dir *dir);
int32_t dir_remove(struct dir *parent_dir, struct dir *child_dir);

#endif