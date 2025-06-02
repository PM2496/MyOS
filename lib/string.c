#include "string.h"
#include "../kernel/global.h"
#include "../kernel/debug.h"
#include "stdint.h"

void memset(void *dst_, uint8_t value, uint32_t size)
{
    ASSERT(dst_ != NULL);

    uint8_t *dst = (uint8_t *)dst_;
    while (size-- > 0)
    {
        *dst++ = value;
    }
}

void memcpy(void *dst_, const void *src_, uint32_t size)
{
    ASSERT(dst_ != NULL && src_ != NULL);

    uint8_t *dst = (uint8_t *)dst_;
    const uint8_t *src = (const uint8_t *)src_;
    while (size-- > 0)
    {
        *dst++ = *src++;
    }
}

int memcmp(const void *a_, const void *b_, uint32_t size)
{
    ASSERT(a_ != NULL && b_ != NULL);

    const char *a = (const char *)a_;
    const char *b = (const char *)b_;
    while (size-- > 0)
    {
        if (*a != *b)
        {
            return *a > *b ? 1 : -1; // Return 1 if a > b, -1 if a < b
        }
        a++;
        b++;
    }
    return 0; // Equal
}

char *strcpy(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);

    char *r = dst_;
    while ((*dst_++ = *src_++) != '\0')
        ; // Copy until null terminator
    return r;
}

uint32_t strlen(const char *str)
{
    ASSERT(str != NULL);

    const char *p = str;
    while (*p != '\0')
    {
        p++;
    }
    return p - str; // Return length
}

int8_t strcmp(const char *a_, const char *b_)
{
    ASSERT(a_ != NULL && b_ != NULL);

    while (*a_ != '\0' && *a_ == *b_)
    {
        a_++;
        b_++;
    }
    return *a_ < *b_ ? -1 : *a_ > *b_; // Return -1 if a < b, 1 if a > b, 0 if equal
}

char *strchr(const char *string, const uint8_t ch)
{
    ASSERT(string != NULL);

    while (*string != '\0')
    {
        if (*string == ch)
        {
            return (char *)string; // Return pointer to first occurrence
        }
        string++;
    }
    return NULL; // Not found
}

char *strrchr(const char *string, const uint8_t ch)
{
    ASSERT(string != NULL);

    const char *last_occurrence = NULL;
    while (*string != '\0')
    {
        if (*string == ch)
        {
            last_occurrence = string; // Update last occurrence
        }
        string++;
    }
    return (char *)last_occurrence; // Return pointer to last occurrence or NULL
}

char *strcat(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);

    char *str = dst_;
    while (*str != '\0') // Move to the end of dst_
    {
        str++;
    }
    while ((*str++ = *src_++))
        ;        // Copy src_ to the end of dst_
    return dst_; // Return concatenated string
}

/* 在字符串str中查找字符ch出现的次数 */
uint32_t strchrs(const char *str, uint8_t ch)
{
    ASSERT(str != NULL);

    uint32_t count = 0;
    const char *p = str;
    while (*p != '\0')
    {
        if (*p == ch)
        {
            count++;
        }
        p++;
    }
    return count; // Return the number of occurrences
}