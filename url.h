/* url.h - http(s):// URL parsing and joining (pure functions, no kernel dependencies) */
#ifndef URL_H
#define URL_H

#include <stdint.h>

typedef struct {
    char     host[64];
    uint16_t port;
    char     path[200];       /* always starts with '/' */
    int      tls;             /* 1 for https:// */
} url_t;

/* 0 = ok, -1 = malformed (also: not an http/https URL) */
int  url_parse(const char *url, url_t *u);

/* Resolves rel against base (absolute, relative, //host, /path, ?query, #fragment) */
void url_join(const char *base, const char *rel, char *out, int outsize);

#endif /* URL_H */
