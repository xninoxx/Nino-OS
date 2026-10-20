/* util.c - tiny string-building helpers */
#include "util.h"

char *put_str(char *p, const char *s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = '\0';
    return p;
}

char *put_uint(char *p, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) {
        *p++ = tmp[--n];
    }
    *p = '\0';
    return p;
}

char *put_2digits(char *p, uint32_t v)
{
    *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    *p = '\0';
    return p;
}

char *put_uptime(char *p, uint32_t seconds)
{
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds / 60) % 60;
    uint32_t s = seconds % 60;

    if (h) {
        p = put_uint(p, h);
        p = put_str(p, "h ");
    }
    if (h || m) {
        p = put_uint(p, m);
        p = put_str(p, "m ");
    }
    p = put_uint(p, s);
    p = put_str(p, "s");
    return p;
}