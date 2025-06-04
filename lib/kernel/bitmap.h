#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include "../stdint.h"
#include "../../kernel/global.h"
#define BITMAP_MASK 1
struct bitmap
{
    uint32_t btmp_bytes_len; // Bitmap size in bytes
    uint8_t *bits;           // Pointer to the bitmap bits
};
void bitmap_init(struct bitmap *btmp);
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx);
int bitmap_scan(struct bitmap *btmp, uint32_t cnt);
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value);
#endif
