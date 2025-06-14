#include "ide.h"
#include "../thread/sync.h"
#include "../lib/stdio.h"
#include "../lib/kernel/stdio_kernel.h"
#include "../kernel/interrupt.h"
#include "../kernel/memory.h"
#include "../kernel/debug.h"
#include "../lib/string.h"
#include "../lib/kernel/io.h"
#include "timer.h"

/* 定义硬盘各寄存器的端口号 */
#define reg_data(channel) (channel->port_base + 0)
#define reg_error(channel) (channel->port_base + 1)
#define reg_sect_cnt(channel) (channel->port_base + 2)
#define reg_lba_l(channel) (channel->port_base + 3)
#define reg_lba_m(channel) (channel->port_base + 4)
#define reg_lba_h(channel) (channel->port_base + 5)
#define reg_dev(channel) (channel->port_base + 6)
#define reg_status(channel) (channel->port_base + 7)
#define reg_cmd(channel) (reg_status(channel))
#define reg_alt_status(channel) (channel->port_base + 0x206)
#define reg_ctl(channel) reg_alt_status(channel)

/* reg_alt_status寄存器的一些关键位 */
#define BIT_STAT_BSY 0x80  // 硬盘忙
#define BIT_STAT_DRDY 0x40 // 驱动器准备好
#define BIT_STAT_DRQ 0x8   // 数据传输准备好了

/* device寄存器的一些关键位 */
#define BIT_DEV_MBS 0xa0 // 第7位和第5位固定为1
#define BIT_DEV_LBA 0x40
#define BIT_DEV_DEV 0x10

/* 一些硬盘操作的指令 */
#define CMD_IDENTIFY 0xec     // identify指令
#define CMD_READ_SECTOR 0x20  // 读扇区指令
#define CMD_WRITE_SECTOR 0x30 // 写扇区指令

/* 定义可读写的最大扇区数,调试用的 */
#define max_lba ((80 * 1024 * 1024 / 512) - 1) // 只支持80MB硬盘

uint8_t channel_cnt;            // 按硬盘数计算的通道数
struct ide_channel channels[2]; // 有两个ide通道

int32_t ext_lba_base = 0; // 扩展分区的起始lba地址,用于逻辑分区

uint8_t p_no = 0, l_no = 0; // 分区号和逻辑分区号,用于分区的编号

struct list partition_list; // 分区链表,用于存储所有分区

struct partition_table_entry
{
    uint8_t bootable;      // 是否可引导
    uint8_t start_head;    // 起始磁头
    uint8_t start_sec;     // 起始扇区
    uint8_t start_chs;     // 起始柱面
    uint8_t fs_type;       // 分区类型
    uint8_t end_head;      // 结束磁头
    uint8_t end_sec;       // 结束扇区
    uint8_t end_chs;       // 结束柱面
    uint32_t start_lba;    // 起始LBA地址
    uint32_t sec_cnt;      // 扇区数
} __attribute__((packed)); // 分区表项结构体,使用packed属性确保结构体按字节对齐

struct boot_sector
{
    uint8_t other[446];                              // 引导代码
    struct partition_table_entry partition_table[4]; // 分区表,最多4个主分区
    uint16_t signature;                              // 分区表签名,0x55aa
} __attribute__((packed));                           // 引导扇区结构体,使用packed属性确保结构体按字节对齐

static void select_disk(struct disk *hd)
{
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) // 如果是从盘
    {
        reg_device |= BIT_DEV_DEV; // 设置从盘标志位
    }
    outb(reg_dev(hd->my_channel), reg_device); // 选择硬盘
}

/* 向硬盘控制器写入起始扇区地址及要读写的扇区数 */
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    struct ide_channel *channel = hd->my_channel;

    /* 写入要读写的扇区数*/
    outb(reg_sect_cnt(channel), sec_cnt); // 如果sec_cnt为0,则表示写入256个扇区

    /* 写入lba地址(即扇区号) */
    outb(reg_lba_l(channel), lba);       // lba地址的低8位,不用单独取出低8位.outb函数中的汇编指令outb %b0, %w1会只用al。
    outb(reg_lba_m(channel), lba >> 8);  // lba地址的8~15位
    outb(reg_lba_h(channel), lba >> 16); // lba地址的16~23位

    /* 因为lba地址的24~27位要存储在device寄存器的0～3位,
     * 无法单独写入这4位,所以在此处把device寄存器再重新写入一次*/
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

/* 向通道channel发命令cmd */
static void cmd_out(struct ide_channel *channel, uint8_t cmd)
{
    /* 只要向硬盘发出了命令便将此标记置为true,硬盘中断处理程序需要根据它来判断 */
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

/* 硬盘读入sec_cnt个扇区的数据到buf */
static void read_from_sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t size_in_byte;
    if (sec_cnt == 0)
    {
        /* 因为sec_cnt是8位变量,由主调函数将其赋值时,若为256则会将最高位的1丢掉变为0 */
        size_in_byte = 256 * 512;
    }
    else
    {
        size_in_byte = sec_cnt * 512;
    }
    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 将buf中sec_cnt扇区的数据写入硬盘 */
static void write2sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t size_in_byte;
    if (sec_cnt == 0)
    {
        /* 因为sec_cnt是8位变量,由主调函数将其赋值时,若为256则会将最高位的1丢掉变为0 */
        size_in_byte = 256 * 512;
    }
    else
    {
        size_in_byte = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 等待30秒 */
static bool busy_wait(struct disk *hd)
{
    struct ide_channel *channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000; // 可以等待30000毫秒
    while (time_limit -= 10 >= 0)
    {
        if (!(inb(reg_status(channel)) & BIT_STAT_BSY))
        {
            return (inb(reg_status(channel)) & BIT_STAT_DRQ);
        }
        else
        {
            mtime_sleep(10); // 睡眠10毫秒
        }
    }
    return false;
}

/* 从硬盘读取sec_cnt个扇区到buf */
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    /* 1 先选择操作的硬盘 */
    select_disk(hd);

    uint32_t secs_op;       // 每次操作的扇区数
    uint32_t secs_done = 0; // 已完成的扇区数
    while (secs_done < sec_cnt)
    {
        if ((secs_done + 256) <= sec_cnt)
        {
            secs_op = 256;
        }
        else
        {
            secs_op = sec_cnt - secs_done;
        }

        /* 2 写入待读入的扇区数和起始扇区号 */
        select_sector(hd, lba + secs_done, secs_op);

        /* 3 执行的命令写入reg_cmd寄存器 */
        cmd_out(hd->my_channel, CMD_READ_SECTOR); // 准备开始读数据

        /*********************   阻塞自己的时机  ***********************
           在硬盘已经开始工作(开始在内部读数据或写数据)后才能阻塞自己,现在硬盘已经开始忙了,
           将自己阻塞,等待硬盘完成读操作后通过中断处理程序唤醒自己*/
        sema_down(&hd->my_channel->disk_done);
        /*************************************************************/

        /* 4 检测硬盘状态是否可读 */
        /* 醒来后开始执行下面代码*/
        if (!busy_wait(hd))
        { // 若失败
            char error[64];
            sprintf(error, "%s read sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }

        /* 5 把数据从硬盘的缓冲区中读出 */
        read_from_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/* 将buf中sec_cnt扇区数据写入硬盘 */
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    /* 1 先选择操作的硬盘 */
    select_disk(hd);

    uint32_t secs_op;       // 每次操作的扇区数
    uint32_t secs_done = 0; // 已完成的扇区数
    while (secs_done < sec_cnt)
    {
        if ((secs_done + 256) <= sec_cnt)
        {
            secs_op = 256;
        }
        else
        {
            secs_op = sec_cnt - secs_done;
        }

        /* 2 写入待写入的扇区数和起始扇区号 */
        select_sector(hd, lba + secs_done, secs_op);

        /* 3 执行的命令写入reg_cmd寄存器 */
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR); // 准备开始写数据

        /* 4 检测硬盘状态是否可读 */
        if (!busy_wait(hd))
        { // 若失败
            char error[64];
            sprintf(error, "%s write sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }

        /* 5 将数据写入硬盘 */
        write2sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);

        /* 在硬盘响应期间阻塞自己 */
        sema_down(&hd->my_channel->disk_done);
        secs_done += secs_op;
    }
    /* 醒来后开始释放锁*/
    lock_release(&hd->my_channel->lock);
}

/* 硬盘中断处理程序 */
void intr_hd_handler(uint8_t irq_no)
{
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel *channel = &channels[ch_no];
    ASSERT(channel->irq_no == irq_no);
    /* 不必担心此中断是否对应的是这一次的expecting_intr,
     * 每次读写硬盘时会申请锁,从而保证了同步一致性 */
    if (channel->expecting_intr)
    {
        channel->expecting_intr = false;
        sema_up(&channel->disk_done);

        /* 读取状态寄存器使硬盘控制器认为此次的中断已被处理,从而硬盘可以继续执行新的读写 */
        inb(reg_status(channel));
    }
}

/* 将dst中len个相邻字节交换位置后存入buf */
static void swap_pairs_bytes(const char *dst, char *buf, uint32_t len)
{
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2)
    {
        buf[idx + 1] = *dst++;
        buf[idx] = *dst++;
    }
    buf[idx] = '\0'; // 确保字符串以'\0'结尾
}

/* 获取硬盘参数信息 */
static void identify_disk(struct disk *hd)
{
    char id_info[512];                     // 用于存储硬盘信息
    select_disk(hd);                       // 选择硬盘
    cmd_out(hd->my_channel, CMD_IDENTIFY); // 发送identify命令

    sema_down(&hd->my_channel->disk_done); // 等待硬盘操作完成

    if (!busy_wait(hd)) // 等待硬盘准备好
    {
        char error[64];
        sprintf(error, "%s identify failed!!!!!!\n", hd->name);
        PANIC(error);
    }
    read_from_sector(hd, id_info, 1); // 从硬盘读取信息

    char buf[64]; // 用于存储格式化后的硬盘信息
    uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(&id_info[sn_start], buf, sn_len); // 交换字节对
    printk("    disk %s info:\n    SN: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));                       // 清空buf
    swap_pairs_bytes(&id_info[md_start], buf, md_len); // 交换字节对
    printk("    MODULE: %s\n", buf);
    uint32_t sectors = *((uint32_t *)&id_info[60 * 2]); // 获取扇区数
    printk("    SECTORS: %d\n", sectors);
    printk("    CAPACITY: %dMB\n", sectors * 512 / (1024 * 1024)); // 打印硬盘大小
}

/* 扫描硬盘hd中地址为ext_lba的扇区中的所有分区 */
static void partition_scan(struct disk *hd, uint32_t ext_lba)
{
    struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector)); // 分配内存用于引导扇区
    ide_read(hd, ext_lba, bs, 1);                                    // 读取引导扇区
    uint8_t part_idx = 0;                                            // 分区索引
    struct partition_table_entry *p = bs->partition_table;           // 获取分区表

    /* 遍历分区表,查找主分区和逻辑分区 */
    while (part_idx++ < 4)
    {
        if (p->fs_type == 0x5)
        { // 扩展分区
            if (ext_lba_base != 0)
            {
                partition_scan(hd, p->start_lba + ext_lba_base); // 递归扫描逻辑分区
            }
            else
            {
                ext_lba_base = p->start_lba;      // 记录扩展分区的起始LBA地址
                partition_scan(hd, p->start_lba); // 扫描扩展分区
            }
        }
        else if (p->fs_type != 0)
        { // 主分区或逻辑分区
            if (ext_lba == 0)
            {
                // 主分区
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;        // 设置主分区的起始LBA地址
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;                      // 设置主分区的扇区数
                hd->prim_parts[p_no].my_disk = hd;                              // 设置主分区所属的硬盘
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);   // 将主分区添加到分区链表
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1); // 设置主分区名称
                p_no++;                                                         // 增加主分区计数
                ASSERT(p_no < 4);                                               // 确保主分区数量不超过4个
            }
            else
            {
                // 逻辑分区
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;        // 设置逻辑分区的起始LBA地址
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;                      // 设置逻辑分区的扇区数
                hd->logic_parts[l_no].my_disk = hd;                              // 设置逻辑分区所属的硬盘
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);   // 将逻辑分区添加到分区链表
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5); // 设置逻辑分区名称，逻辑分区数字从5开始
                l_no++;                                                          // 增加逻辑分区计数
                if (l_no >= 8)
                {
                    return;
                }
            }
        }
        p++; // 移动到下一个分区表项
    }
    sys_free(bs); // 释放引导扇区内存
}

/* 打印分区信息 */
static bool partition_info(struct list_elem *pelem, int arg UNUSED)
{
    struct partition *part = elem2entry(struct partition, part_tag, pelem); // 获取分区结构体
    printk("    %s start_lba: 0x%x, sec_cnt: 0x%x\n", part->name, part->start_lba, part->sec_cnt);
    return false; // 返回false表示不需要继续遍历
}

void ide_init(void)
{
    printk("ide_init start\n");
    uint8_t hd_cnt = *((uint8_t *)(0x475)); // 从BIOS数据区获取硬盘数
    printk("   ide_init hd_cnt:%d\n", hd_cnt);
    ASSERT(hd_cnt > 0);
    list_init(&partition_list);            // 初始化分区链表
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2); // 每个通道最多支持两个硬盘
    struct ide_channel *channel;
    uint8_t channel_no = 0, dev_no = 0;

    while (channel_no < channel_cnt)
    {
        channel = &channels[channel_no];
        sprintf(channel->name, "ide%d", channel_no);

        switch (channel_no)
        {
        case 0:
            channel->port_base = 0x1f0;  // 主通道基地址
            channel->irq_no = 0x20 + 14; // 主通道中断号
            break;

        case 1:
            channel->port_base = 0x170;  // 从通道基地址
            channel->irq_no = 0x20 + 15; // 从通道中断号
            break;
        }

        channel->expecting_intr = false; // 初始化中断期望标志
        lock_init(&channel->lock);       // 初始化锁

        sema_init(&channel->disk_done, 0); // 初始化信号量

        register_handler(channel->irq_no, intr_hd_handler); // 注册中断处理程序

        while (dev_no < 2)
        {
            struct disk *hd = &channel->devices[dev_no];
            hd->my_channel = channel; // 设置硬盘所属的通道
            hd->dev_no = dev_no;      // 设置硬盘设备号

            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no); // 设置硬盘名称

            identify_disk(hd); // 获取硬盘参数信息

            if (dev_no != 0)
            {
                // 内核本身的裸硬盘(hd60M.img)不处理
                partition_scan(hd, 0); // 扫描主分区
            }

            p_no = 0, l_no = 0; // 重置分区号和逻辑分区号
            dev_no++;
        }
        dev_no = 0; // 重置设备号
        channel_no++;
    }

    printk("\n   all partition info\n");
    /* 打印所有分区信息 */
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done\n");
}