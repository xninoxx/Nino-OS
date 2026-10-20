/* util.h - tiny string-building helpers (no printf in a kernel!)
 * Every function writes at p, NUL-terminates, and returns a pointer to the NUL,
 * so calls can be chained. */
#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

char *put_str(char *p, const char *s);
char *put_uint(char *p, uint32_t v);
char *put_2digits(char *p, uint32_t v);          /* 7 -> "07" */
char *put_uptime(char *p, uint32_t seconds);     /* 3725 -> "1h 2m 5s" */

#endif /* UTIL_H */