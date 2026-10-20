/* cookies.c - cookie jar. Session cookies live in RAM only; cookies with an expiry are kept in
 * /home/nino/.cookies so they survive a reboot (the file is plain text: anyone using NinoOS can read it). */
#include "cookies.h"
#include "rtc.h"
#include "fs.h"
#include "heap.h"
#include "string.h"

static cookie_t jar[COOKIE_MAX];
static bool     dirty_persistent;

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ci_prefix(const char *s, const char *prefix)
{
    while (*prefix) {
        if (lower(*s) != *prefix) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static void copy(char *dst, int size, const char *src, int n)
{
    int i = 0;
    while (i < n && i < size - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static const char *trim_start(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int trimmed_len(const char *s, int n)
{
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    return n;
}

/* Is `domain` equal to host or a parent domain of it? */
static bool domain_match(const char *host, const char *domain, bool host_only)
{
    if (host_only) {
        return strcmp(host, domain) == 0;
    }
    int hl = (int)strlen(host), dl = (int)strlen(domain);
    if (dl > hl) return false;
    if (strcmp(host + hl - dl, domain) != 0) return false;
    return dl == hl || host[hl - dl - 1] == '.';
}

static bool path_match(const char *req, const char *cpath)
{
    int cl = (int)strlen(cpath);
    if (strncmp(req, cpath, (size_t)cl) != 0) return false;
    return req[cl] == '\0' || req[cl] == '?' || cpath[cl - 1] == '/' || req[cl] == '/';
}

/* "Wed, 21 Oct 2026 07:28:00 GMT" -> unix time (0 if it does not parse) */
static uint32_t parse_http_date(const char *s)
{
    static const char *const months[] = { "jan", "feb", "mar", "apr", "may", "jun",
                                          "jul", "aug", "sep", "oct", "nov", "dec" };
    while (*s && *s != ' ') s++;                         /* skip the weekday */
    s = trim_start(s);
    int day = 0;
    while (*s >= '0' && *s <= '9') day = day * 10 + (*s++ - '0');
    while (*s == ' ' || *s == '-') s++;
    int mon = -1;
    for (int i = 0; i < 12; i++) {
        if (ci_prefix(s, months[i])) mon = i;
    }
    if (mon < 0) return 0;
    s += 3;
    while (*s == ' ' || *s == '-') s++;
    int year = 0, digits = 0;
    while (*s >= '0' && *s <= '9') { year = year * 10 + (*s++ - '0'); digits++; }
    if (digits == 2) year += (year < 70) ? 2000 : 1900;
    while (*s == ' ') s++;
    int hh = 0, mm = 0, ss = 0;
    while (*s >= '0' && *s <= '9') hh = hh * 10 + (*s++ - '0');
    if (*s == ':') s++;
    while (*s >= '0' && *s <= '9') mm = mm * 10 + (*s++ - '0');
    if (*s == ':') s++;
    while (*s >= '0' && *s <= '9') ss = ss * 10 + (*s++ - '0');
    if (year < 1970 || day < 1) return 0;
    return days_from_civil(year, mon + 1, day) * 86400u + (uint32_t)(hh * 3600 + mm * 60 + ss);
}

static void remove_cookie(const char *domain, const char *path, const char *name)
{
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (jar[i].used && strcmp(jar[i].domain, domain) == 0 && strcmp(jar[i].path, path) == 0 &&
            strcmp(jar[i].name, name) == 0) {
            if (jar[i].expires) dirty_persistent = true;
            jar[i].used = 0;
        }
    }
}

void cookies_set_from_header(const char *host, const char *req_path, const char *value)
{
    cookie_t c;
    memset(&c, 0, sizeof(c));

    /* name=value */
    const char *p = trim_start(value);
    const char *eq = p;
    while (*eq && *eq != '=' && *eq != ';') eq++;
    if (*eq != '=') return;
    int nl = trimmed_len(p, (int)(eq - p));
    if (nl <= 0 || nl >= (int)sizeof(c.name)) return;
    copy(c.name, sizeof(c.name), p, nl);
    const char *vs = eq + 1;
    const char *ve = vs;
    while (*ve && *ve != ';') ve++;
    int vl = trimmed_len(vs, (int)(ve - vs));
    if (vl >= COOKIE_VALUE_MAX) return;                  /* too long for us: ignore the cookie */
    copy(c.value, sizeof(c.value), vs, vl);

    /* defaults */
    copy(c.domain, sizeof(c.domain), host, (int)strlen(host));
    c.host_only = 1;
    int pl = (int)strlen(req_path);
    int slash = pl;
    while (slash > 0 && req_path[slash - 1] != '/') slash--;
    if (slash <= 1) {
        c.path[0] = '/';
        c.path[1] = '\0';
    } else {
        copy(c.path, sizeof(c.path), req_path, slash - 1);   /* directory without the trailing slash */
    }
    c.expires = 0;
    bool delete_now = false;

    /* attributes */
    const char *a = ve;
    while (*a == ';') {
        a = trim_start(a + 1);
        const char *end = a;
        while (*end && *end != ';') end++;
        int len = (int)(end - a);
        if (ci_prefix(a, "domain=")) {
            const char *d = a + 7;
            if (*d == '.') d++;
            char dom[64];
            copy(dom, sizeof(dom), d, trimmed_len(d, (int)(end - d)));
            for (char *q = dom; *q; q++) *q = lower(*q);
            /* only a parent domain of the host, and never a bare TLD like "com" */
            bool has_dot = false;
            for (char *q = dom; *q; q++) if (*q == '.') has_dot = true;
            if (has_dot && domain_match(host, dom, false)) {
                copy(c.domain, sizeof(c.domain), dom, (int)strlen(dom));
                c.host_only = 0;
            } else {
                return;                                  /* refuse cookies for foreign domains */
            }
        } else if (ci_prefix(a, "path=")) {
            const char *pa = a + 5;
            int l = trimmed_len(pa, (int)(end - pa));
            if (l > 0 && pa[0] == '/') copy(c.path, sizeof(c.path), pa, l);
        } else if (ci_prefix(a, "max-age=")) {
            const char *m = a + 8;
            bool neg = (*m == '-');
            if (neg) m++;
            uint32_t n = 0;
            while (*m >= '0' && *m <= '9') n = n * 10 + (uint32_t)(*m++ - '0');
            if (neg || n == 0) delete_now = true;
            else c.expires = rtc_unix_time() + n;
        } else if (ci_prefix(a, "expires=")) {
            if (c.expires == 0) {                        /* Max-Age wins over Expires */
                uint32_t t = parse_http_date(a + 8);
                if (t && t <= rtc_unix_time()) delete_now = true;
                else if (t) c.expires = t;
            }
        } else if (len >= 6 && ci_prefix(a, "secure")) {
            c.secure = 1;
        }
        a = end;
    }

    for (char *q = c.domain; *q; q++) *q = lower(*q);
    remove_cookie(c.domain, c.path, c.name);
    if (delete_now) {
        cookies_save();
        return;
    }
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (!jar[i].used) {
            c.used = 1;
            jar[i] = c;
            if (c.expires) dirty_persistent = true;
            if (dirty_persistent) cookies_save();
            return;
        }
    }
}

int cookies_header(const char *host, const char *path, bool secure, char *out, int size)
{
    uint32_t now = rtc_unix_time();
    int n = 0;
    out[0] = '\0';
    for (int i = 0; i < COOKIE_MAX; i++) {
        cookie_t *c = &jar[i];
        if (!c->used) continue;
        if (c->expires && c->expires <= now) { c->used = 0; continue; }   /* expired */
        if (c->secure && !secure) continue;
        if (!domain_match(host, c->domain, c->host_only) || !path_match(path, c->path)) continue;
        int need = (int)strlen(c->name) + 1 + (int)strlen(c->value) + (n ? 2 : 0);
        if (n + need >= size) break;
        if (n) { out[n++] = ';'; out[n++] = ' '; }
        for (const char *s = c->name; *s; s++) out[n++] = *s;
        out[n++] = '=';
        for (const char *s = c->value; *s; s++) out[n++] = *s;
        out[n] = '\0';
    }
    return n;
}

int cookies_count(void)
{
    int n = 0;
    for (int i = 0; i < COOKIE_MAX; i++) if (jar[i].used) n++;
    return n;
}

const cookie_t *cookies_get(int idx)
{
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (jar[i].used && idx-- == 0) return &jar[i];
    }
    return 0;
}

void cookies_clear(void)
{
    memset(jar, 0, sizeof(jar));
    dirty_persistent = true;
    cookies_save();
}

/* ---------- Persistence: one cookie per line, fields separated by tabs ---------- */
void cookies_save(void)
{
    dirty_persistent = false;
    fs_node_t *dir = fs_resolve(fs_root(), "/home/nino");
    if (!dir) return;

    uint32_t cap = 1024;
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (jar[i].used && jar[i].expires) cap += 200 + (uint32_t)strlen(jar[i].value);
    }
    char *buf = kmalloc(cap);
    if (!buf) return;
    uint32_t n = 0;
    for (int i = 0; i < COOKIE_MAX; i++) {
        const cookie_t *c = &jar[i];
        if (!c->used || !c->expires) continue;
        const char *f[4] = { c->domain, c->path, c->name, c->value };
        for (int k = 0; k < 4; k++) {
            for (const char *s = f[k]; *s; s++) buf[n++] = *s;
            buf[n++] = '\t';
        }
        char num[16];
        int d = 0;
        uint32_t e = c->expires;
        do { num[d++] = (char)('0' + e % 10); e /= 10; } while (e);
        while (d) buf[n++] = num[--d];
        buf[n++] = '\t';
        buf[n++] = (char)('0' + c->secure);
        buf[n++] = (char)('0' + c->host_only);
        buf[n++] = '\n';
    }
    fs_node_t *f = fs_find_child(dir, ".cookies");
    if (!f) f = fs_create(dir, ".cookies", FS_FILE);
    if (f) fs_write(f, buf, n);
    kfree(buf);
}

void cookies_load(void)
{
    fs_node_t *f = fs_resolve(fs_root(), "/home/nino/.cookies");
    if (!f || f->type != FS_FILE || !f->size) return;
    const char *s = (const char *)f->data;
    const char *end = s + f->size;
    while (s < end) {
        const char *fields[7];
        int nf = 0;
        const char *line_end = s;
        while (line_end < end && *line_end != '\n') line_end++;
        const char *q = s;
        while (nf < 7 && q <= line_end) {
            fields[nf++] = q;
            while (q < line_end && *q != '\t') q++;
            q++;
        }
        if (nf == 7) {
            for (int i = 0; i < COOKIE_MAX; i++) {
                if (jar[i].used) continue;
                cookie_t *c = &jar[i];
                memset(c, 0, sizeof(*c));
                #define FIELD(k, dst) copy(dst, sizeof(dst), fields[k], (int)(fields[k + 1] - fields[k] - 1))
                FIELD(0, c->domain);
                FIELD(1, c->path);
                FIELD(2, c->name);
                FIELD(3, c->value);
                #undef FIELD
                uint32_t e = 0;
                for (const char *d = fields[4]; *d >= '0' && *d <= '9'; d++) e = e * 10 + (uint32_t)(*d - '0');
                c->expires = e;
                c->secure = fields[5][0] == '1';
                c->host_only = fields[6][0] == '1';
                c->used = e > rtc_unix_time();
                break;
            }
        }
        s = line_end + 1;
    }
    dirty_persistent = false;
}
