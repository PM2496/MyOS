#include "keyboard.h"
#include "../lib/kernel/print.h"
#include "../kernel/interrupt.h"
#include "../lib/kernel/io.h"
#include "../kernel/global.h"
#include "ioqueue.h"

#define KBD_BUF_PORT 0x60 // Keyboard data port

/* 用转义字符定义部分控制字符 */
#define esc '\033' // 八进制表示字符,也可以用十六进制'\x1b'
#define backspace '\b'
#define tab '\t'
#define enter '\r'
#define delete '\177' // 八进制表示字符,十六进制为'\x7f'

/* 以上不可见字符一律定义为0 */
#define char_invisible 0
#define ctrl_l_char char_invisible
#define ctrl_r_char char_invisible
#define shift_l_char char_invisible
#define shift_r_char char_invisible
#define alt_l_char char_invisible
#define alt_r_char char_invisible
#define caps_lock_char char_invisible

/* 定义控制字符的通码和断码 */
#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define alt_r_break 0xe0b8
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define ctrl_r_break 0xe09d
#define caps_lock_make 0x3a

struct ioqueue kbd_buf;

/* 定义以下变量记录相应键是否按下的状态,
 * ext_scancode用于记录makecode是否以0xe0开头 */
static bool ctrl_status, shift_status, alt_status, caps_lock_status, ext_scancode;

/* 以通码make_code为索引的二维数组 */
static char keymap[][2] = {
    /* 扫描码   未与shift组合  与shift组合*/
    /* ---------------------------------- */
    /* 0x00 */ {0, 0},
    /* 0x01 */ {esc, esc},
    /* 0x02 */ {'1', '!'},
    /* 0x03 */ {'2', '@'},
    /* 0x04 */ {'3', '#'},
    /* 0x05 */ {'4', '$'},
    /* 0x06 */ {'5', '%'},
    /* 0x07 */ {'6', '^'},
    /* 0x08 */ {'7', '&'},
    /* 0x09 */ {'8', '*'},
    /* 0x0A */ {'9', '('},
    /* 0x0B */ {'0', ')'},
    /* 0x0C */ {'-', '_'},
    /* 0x0D */ {'=', '+'},
    /* 0x0E */ {backspace, backspace},
    /* 0x0F */ {tab, tab},
    /* 0x10 */ {'q', 'Q'},
    /* 0x11 */ {'w', 'W'},
    /* 0x12 */ {'e', 'E'},
    /* 0x13 */ {'r', 'R'},
    /* 0x14 */ {'t', 'T'},
    /* 0x15 */ {'y', 'Y'},
    /* 0x16 */ {'u', 'U'},
    /* 0x17 */ {'i', 'I'},
    /* 0x18 */ {'o', 'O'},
    /* 0x19 */ {'p', 'P'},
    /* 0x1A */ {'[', '{'},
    /* 0x1B */ {']', '}'},
    /* 0x1C */ {enter, enter},
    /* 0x1D */ {ctrl_l_char, ctrl_l_char},
    /* 0x1E */ {'a', 'A'},
    /* 0x1F */ {'s', 'S'},
    /* 0x20 */ {'d', 'D'},
    /* 0x21 */ {'f', 'F'},
    /* 0x22 */ {'g', 'G'},
    /* 0x23 */ {'h', 'H'},
    /* 0x24 */ {'j', 'J'},
    /* 0x25 */ {'k', 'K'},
    /* 0x26 */ {'l', 'L'},
    /* 0x27 */ {';', ':'},
    /* 0x28 */ {'\'', '"'},
    /* 0x29 */ {'`', '~'},
    /* 0x2A */ {shift_l_char, shift_l_char},
    /* 0x2B */ {'\\', '|'},
    /* 0x2C */ {'z', 'Z'},
    /* 0x2D */ {'x', 'X'},
    /* 0x2E */ {'c', 'C'},
    /* 0x2F */ {'v', 'V'},
    /* 0x30 */ {'b', 'B'},
    /* 0x31 */ {'n', 'N'},
    /* 0x32 */ {'m', 'M'},
    /* 0x33 */ {',', '<'},
    /* 0x34 */ {'.', '>'},
    /* 0x35 */ {'/', '?'},
    /* 0x36	*/ {shift_r_char, shift_r_char},
    /* 0x37 */ {'*', '*'},
    /* 0x38 */ {alt_l_char, alt_l_char},
    /* 0x39 */ {' ', ' '},
    /* 0x3A */ {caps_lock_char, caps_lock_char}
    /*其它按键暂不处理*/
};

static void intr_keyboard_handler(void)
{
    bool ctrl_down_last = ctrl_status;      // 保存上次ctrl状态
    bool shift_down_last = shift_status;    // 保存上次shift状态
    bool caps_lock_last = caps_lock_status; // 保存上次caps lock状态

    bool break_code;                       // 是否为断码
    uint16_t scancode = inb(KBD_BUF_PORT); // 从键盘数据端口读取扫描码

    /* 若扫描码是e0开头的,表示此键的按下将产生多个扫描码,
     * 所以马上结束此次中断处理函数,等待下一个扫描码进来*/
    if (scancode == 0xe0)
    {
        ext_scancode = true; // 设置扩展扫描码标志
        return;
    }
    /* 如果上次是以0xe0开头,将扫描码合并 */
    if (ext_scancode)
    {
        scancode = ((0xe000) | scancode); // 将0xe0与扫描码合并
        ext_scancode = false;             // 清除扩展扫描码标志
    }

    break_code = ((scancode & 0x0080) != 0); // 判断是否为断码

    if (break_code)
    {
        /* 由于ctrl_r 和alt_r的make_code和break_code都是两字节,
         * 所以可用下面的方法取make_code,多字节的扫描码暂不处理 */
        uint16_t make_code = (scancode & 0xff7f); // 去掉断码的标志位

        if (make_code == ctrl_l_make || make_code == ctrl_r_make)
        {
            ctrl_status = false; // 清除ctrl状态
        }
        else if (make_code == shift_l_make || make_code == shift_r_make)
        {
            shift_status = false; // 清除shift状态
        }
        else if (make_code == alt_l_make || make_code == alt_r_break)
        {
            alt_status = false; // 清除alt状态
        } /* 由于caps_lock不是弹起后关闭,所以需要单独处理 */

        return;
    }
    else if ((scancode > 0x00 && scancode < 0x3b) ||
             (scancode == alt_r_make) ||
             (scancode == ctrl_r_make))
    {
        bool shift = false; // 是否按下shift键
        if ((scancode < 0x0e) || (scancode == 0x29) ||
            (scancode == 0x1a) || (scancode == 0x1b) ||
            (scancode == 0x2b) || (scancode == 0x27) ||
            (scancode == 0x28) || (scancode == 0x33) ||
            (scancode == 0x34) || (scancode == 0x35))
        {
            /****** 代表两个字母的键 ********
             0x0e 数字'0'~'9',字符'-',字符'='
             0x29 字符'`'
             0x1a 字符'['
             0x1b 字符']'
             0x2b 字符'\\'
             0x27 字符';'
             0x28 字符'\''
             0x33 字符','
             0x34 字符'.'
             0x35 字符'/'
            *******************************/
            if (shift_down_last)
            {
                shift = true; // 如果上次shift按下,则设置shift为true
            }
        }
        else
        {
            /* 默认为字母键 */
            if (shift_down_last && caps_lock_last)
            {
                // 如果shift和capslock同时按下
                shift = false;
            }
            else if (shift_down_last || caps_lock_last)
            {
                // 如果shift或capslock按下
                shift = true;
            }
            else
            {
                // 如果都没有按下
                shift = false;
            }
        }

        // 将扫描码的高字节置0,主要是针对高字节是e0的扫描码.
        uint8_t index = scancode & 0x00ff;
        char cur_char = keymap[index][shift]; // 获取当前字符

        /* 只处理ascii码不为零的键 */
        if (cur_char)
        {
            /*****************  快捷键ctrl+l和ctrl+u的处理 *********************
             * 下面是把ctrl+l和ctrl+u这两种组合键产生的字符置为:
             * cur_char的asc码-字符a的asc码, 此差值比较小,
             * 属于asc码表中不可见的字符部分.故不会产生可见字符.
             * 我们在shell中将ascii值为l-a和u-a的分别处理为清屏和删除输入的快捷键*/
            if ((ctrl_down_last && cur_char == 'l') || (ctrl_down_last && cur_char == 'u'))
            {
                cur_char -= 'a';
            }
            /****************************************************************/
            if (!ioq_full(&kbd_buf))
            {
                // put_char(cur_char); // 输出字符到控制台
                /* 如果键盘缓冲区未满,则将字符放入缓冲区 */
                ioq_putchar(&kbd_buf, cur_char);
            }
            return;
        }

        /* 记录本次是否按下了下面几类控制键之一,供下次键入时判断组合键 */
        if (scancode == ctrl_l_make || scancode == ctrl_r_make)
        {
            ctrl_status = true;
        }
        else if (scancode == shift_l_make || scancode == shift_r_make)
        {
            shift_status = true;
        }
        else if (scancode == alt_l_make || scancode == alt_r_make)
        {
            alt_status = true;
        }
        else if (scancode == caps_lock_make)
        {
            /* 不管之前是否有按下caps_lock键,当再次按下时则状态取反,
             * 即:已经开启时,再按下同样的键是关闭。关闭时按下表示开启。*/
            caps_lock_status = !caps_lock_status;
        }
    }
    else
    {
        put_str("unknown key\n");
    }
}

void keyboard_init(void)
{
    put_str("keyboard_init start\n");
    ioqueue_init(&kbd_buf);                        // 初始化键盘缓冲区
    register_handler(0x21, intr_keyboard_handler); // Register keyboard interrupt handler
    put_str("keyboard_init done\n");
}