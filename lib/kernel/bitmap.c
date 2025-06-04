#include "bitmap.h"
#include "../stdint.h"
#include "../string.h"
#include "print.h"
#include "../../kernel/interrupt.h"
#include "../../kernel/debug.h"
#include "../../kernel/global.h"

void bitmap_init(struct bitmap *btmp)
{
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx)
{
    uint32_t byte_idx = bit_idx / 8;
    uint8_t bit_odd = bit_idx % 8;
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

int bitmap_scan(struct bitmap *btmp, uint32_t cnt)
{
    uint32_t idx_byte = 0;

    while ((0xff == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len))
    {
        idx_byte++;
    }

    ASSERT(idx_byte < btmp->btmp_bytes_len);
    if (idx_byte == btmp->btmp_bytes_len)
    {
        return -1; // No free bits found
    }

    int idx_bit = 0;
    while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte])
    {
        idx_bit++;
    }

    int bit_idx_start = idx_byte * 8 + idx_bit;
    if (cnt == 1)
    {
        return bit_idx_start; // Return the index of the first free bit
    }

    uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start);
    uint32_t next_bit = bit_idx_start + 1;
    uint32_t count = 1;

    bit_idx_start = -1; // Reset to indicate no valid start found yet
    while (bit_left-- > 0)
    {
        if (!bitmap_scan_test(btmp, next_bit))
        {
            count++;
        }
        else
        {
            count = 0; // Reset count if a used bit is found
        }

        if (count == cnt)
        {
            bit_idx_start = next_bit - cnt + 1; // Found a sequence of free bits
            break;                              // Exit the loop as we found the required sequence
        }
        next_bit++;
    }
    return bit_idx_start; // Return the starting index of the free bits or -1 if not found
}

void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value)
{
    ASSERT((value == 0) || (value == 1));
    uint32_t byte_idx = bit_idx / 8;
    uint8_t bit_odd = bit_idx % 8;

    if (value)
    {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd); // Set the bit
    }
    else
    {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd); // Clear the bit
    }
}