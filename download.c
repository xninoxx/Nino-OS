/* download.c - save downloads to /downloads */
#include "download.h"
#include "fs.h"
#include "string.h"

static bool name_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

static int clean_name(char *dst, const char *src, int max)
{
    int n = 0;
    for (; *src && n < max; src++) {
        dst[n++] = name_char_ok(*src) ? *src : '_';
    }
    dst[n] = '\0';
    if (n == 0 || strcmp(dst, ".") == 0 || strcmp(dst, "..") == 0) {
        dst[0] = '\0';
        return 0;
    }
    return n;
}

bool download_save(const http_result_t *res, const char *name_hint, char *saved, int size)
{
    char name[FS_NAME_MAX + 1];
    name[0] = '\0';

    if (name_hint && *name_hint) {
        clean_name(name, name_hint, FS_NAME_MAX);
    }
    if (!name[0] && res->filename[0]) {
        const char *f = res->filename;                       /* drop any directory part */
        for (const char *q = f; *q; q++) if (*q == '/' || *q == '\\') f = q + 1;
        clean_name(name, f, FS_NAME_MAX);
    }
    if (!name[0]) {
        const char *u = res->final_url;
        if (u[0] == 'h') {                                   /* skip "http(s)://" */
            while (*u && !(u[0] == ':' && u[1] == '/' && u[2] == '/')) u++;
            if (*u) u += 3;
        }
        while (*u && *u != '/') u++;                         /* skip the host */
        const char *last = u;
        for (const char *q = u; *q && *q != '?' && *q != '#'; q++) {
            if (*q == '/') last = q + 1;
        }
        char tmp[FS_NAME_MAX + 1];
        int i = 0;
        while (last[i] && last[i] != '?' && last[i] != '#' && i < FS_NAME_MAX) { tmp[i] = last[i]; i++; }
        tmp[i] = '\0';
        clean_name(name, tmp, FS_NAME_MAX);
    }
    if (!name[0]) {
        const char *d = "index.html";
        int i = 0;
        while (d[i]) { name[i] = d[i]; i++; }
        name[i] = '\0';
    }

    fs_node_t *dir = fs_resolve(fs_root(), "/downloads");
    if (!dir) {
        dir = fs_create(fs_root(), "downloads", FS_DIR);
    }
    if (!dir) return false;
    fs_node_t *f = fs_find_child(dir, name);
    if (!f) f = fs_create(dir, name, FS_FILE);
    if (!f || f->type != FS_FILE) return false;
    if (res->size && !fs_write(f, res->data, res->size)) return false;
    if (!res->size) fs_write(f, "", 0);

    int i = 0;
    while (name[i] && i < size - 1) { saved[i] = name[i]; i++; }
    saved[i] = '\0';
    return true;
}
