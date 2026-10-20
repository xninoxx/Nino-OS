/* url.c - URL parsing and joining */
#include "url.h"
#include "string.h"

static int starts(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static void copy(char *dst, int size, const char *src)
{
    int i = 0;
    while (src[i] && i < size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void append(char *dst, int size, const char *src)
{
    int n = (int)strlen(dst);
    copy(dst + n, size - n, src);
}

int url_parse(const char *url, url_t *u)
{
    u->tls = 0;
    if (starts(url, "https://")) {
        u->tls = 1;
        url += 8;
    } else if (starts(url, "http://")) {
        url += 7;
    } else if (strlen(url) > 3) {
        const char *c = url;                  /* "ftp://", "mailto:" ... are not http */
        while (*c && *c != ':' && *c != '/' && *c != '.') {
            c++;
        }
        if (*c == ':' && c[1] == '/') {
            return -1;
        }
    }

    int h = 0;
    while (*url && *url != '/' && *url != ':' && *url != '?' && *url != '#') {
        if (h >= (int)sizeof(u->host) - 1) {
            return -1;
        }
        u->host[h++] = *url++;
    }
    u->host[h] = '\0';
    if (h == 0) {
        return -1;
    }

    u->port = u->tls ? 443 : 80;
    if (*url == ':') {
        url++;
        uint32_t port = 0;
        while (*url >= '0' && *url <= '9') {
            port = port * 10 + (uint32_t)(*url++ - '0');
            if (port > 65535) {
                return -1;
            }
        }
        if (port == 0) {
            return -1;
        }
        u->port = (uint16_t)port;
    }

    int p = 0;
    if (*url != '/') {
        u->path[p++] = '/';                   /* "http://host" or "http://host?x" */
    }
    while (*url && *url != '#') {
        if (p >= (int)sizeof(u->path) - 1) {
            return -1;
        }
        u->path[p++] = (*url == ' ') ? '+' : *url;
        url++;
    }
    u->path[p] = '\0';
    return 0;
}

void url_join(const char *base, const char *rel, char *out, int outsize)
{
    out[0] = '\0';
    if (starts(rel, "http://") || starts(rel, "https://")) {
        copy(out, outsize, rel);
        return;
    }
    if (starts(rel, "//")) {
        copy(out, outsize, "http:");
        append(out, outsize, rel);
        return;
    }

    /* origin = scheme://host[:port] ; path starts at the first '/' after it */
    int scheme = starts(base, "http://") ? 7 : (starts(base, "https://") ? 8 : 0);
    int origin_len = scheme;
    while (base[origin_len] && base[origin_len] != '/' && base[origin_len] != '?' && base[origin_len] != '#') {
        origin_len++;
    }
    char origin[200];
    int ol = origin_len < (int)sizeof(origin) - 1 ? origin_len : (int)sizeof(origin) - 1;
    memcpy(origin, base, (size_t)ol);
    origin[ol] = '\0';

    if (rel[0] == '#') {                      /* same page */
        int bl = 0;
        while (base[bl] && base[bl] != '#') bl++;
        int n = bl < outsize - 1 ? bl : outsize - 1;
        memcpy(out, base, (size_t)n);
        out[n] = '\0';
        return;
    }
    if (rel[0] == '/') {
        copy(out, outsize, origin);
        append(out, outsize, rel);
        return;
    }

    /* Path part of base without query/fragment */
    char path[200];
    int pl = 0;
    for (int i = origin_len; base[i] && base[i] != '?' && base[i] != '#' && pl < (int)sizeof(path) - 1; i++) {
        path[pl++] = base[i];
    }
    path[pl] = '\0';

    if (rel[0] == '?') {
        copy(out, outsize, origin);
        append(out, outsize, path[0] ? path : "/");
        append(out, outsize, rel);
        return;
    }

    /* Directory of base: everything up to and including the last '/' */
    if (pl == 0) {
        path[0] = '/';
        path[1] = '\0';
        pl = 1;
    }
    while (pl > 0 && path[pl - 1] != '/') {
        path[--pl] = '\0';
    }
    for (;;) {
        if (starts(rel, "./")) {
            rel += 2;
        } else if (starts(rel, "../")) {
            rel += 3;
            if (pl > 1) {                     /* drop the last directory */
                pl--;
                while (pl > 0 && path[pl - 1] != '/') pl--;
                path[pl] = '\0';
            }
        } else {
            break;
        }
    }
    copy(out, outsize, origin);
    append(out, outsize, path);
    append(out, outsize, rel);
}
