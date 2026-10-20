/* cookies.h - HTTP cookie jar (Set-Cookie / Cookie headers) */
#ifndef COOKIES_H
#define COOKIES_H

#include <stdint.h>
#include <stdbool.h>

#define COOKIE_MAX       64
#define COOKIE_VALUE_MAX 512

typedef struct {
    char     domain[64];        /* without leading dot */
    char     path[64];
    char     name[48];
    char     value[COOKIE_VALUE_MAX];
    uint8_t  secure;            /* only send over https */
    uint8_t  host_only;         /* no Domain attribute: exact host match only */
    uint32_t expires;           /* unix time, 0 = session cookie */
    uint8_t  used;
} cookie_t;

/* Handles one "Set-Cookie:" header value received from host for a request to req_path */
void cookies_set_from_header(const char *host, const char *req_path, const char *value);

/* Builds "a=1; b=2" for a request; returns the length (0 = no cookies) */
int  cookies_header(const char *host, const char *path, bool secure, char *out, int size);

int  cookies_count(void);
const cookie_t *cookies_get(int i);       /* i-th used cookie, 0 if out of range */
void cookies_clear(void);

void cookies_load(void);                  /* from /home/nino/.cookies (persistent ones) */
void cookies_save(void);

#endif /* COOKIES_H */
