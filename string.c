/* string.c - minimal freestanding string/memory functions.
 * GCC may emit calls to memset/memcpy/memmove even in freestanding code,
 * so the kernel must provide them. */
#include "string.h"

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = dst;
    while (n--) {
        *d++ = (unsigned char)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = a;
    const unsigned char *pb = b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return pa[i] < pb[i] ? -1 : 1;
        }
    }
    return 0;
}
