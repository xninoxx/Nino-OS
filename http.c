/* http.c - minimal HTTP/1.1 client (Connection: close) for http:// and https:// */
#include "http.h"
#include "net.h"
#include "tls.h"
#include "cookies.h"
#include "url.h"
#include "heap.h"
#include "string.h"

static void set_err(char *err, int size, const char *msg)
{
    int i = 0;
    while (msg[i] && i < size - 1) {
        err[i] = msg[i];
        i++;
    }
    err[i] = '\0';
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Case-insensitive prefix test */
static int starts_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (lower(*s) != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int find_header_end(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i + 3 < n; i++) {
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
            return (int)(i + 4);
        }
    }
    return -1;
}

/* Decodes a chunked body in place; returns the new length */
static uint32_t dechunk(uint8_t *b, uint32_t n)
{
    uint32_t in = 0, out = 0;
    while (in < n) {
        uint32_t size = 0;
        int digits = 0;
        while (in < n) {
            char c = lower((char)b[in]);
            int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (v < 0) break;
            size = size * 16 + (uint32_t)v;
            digits++;
            in++;
        }
        if (!digits) break;
        while (in < n && b[in] != '\n') in++;           /* skip chunk extensions */
        in++;
        if (size == 0) break;
        if (in + size > n) size = n - in;               /* truncated download */
        memmove(b + out, b + in, size);
        out += size;
        in += size;
        if (in < n && b[in] == '\r') in++;
        if (in < n && b[in] == '\n') in++;
    }
    return out;
}

/* Transport: plain TCP for http://, TLS for https:// */
static bool tx_connect(const url_t *u, uint32_t ip, char *err, int es)
{
    if (u->tls) {
        if (!tls_available()) {
            set_err(err, es, tls_error_text());
            return false;
        }
        if (!tls_connect(u->host, ip, u->port)) {
            set_err(err, es, tls_error_text());
            return false;
        }
        return true;
    }
    if (!tcp_connect(ip, u->port)) {
        set_err(err, es, "Connection failed");
        return false;
    }
    return true;
}

static bool tx_send(const url_t *u, const void *d, uint32_t n) { return u->tls ? tls_send(d, n) : tcp_send(d, n); }
static int  tx_recv(const url_t *u, void *b, uint32_t n)       { return u->tls ? tls_recv(b, n) : tcp_recv(b, n, 8000); }
static void tx_close(const url_t *u)                           { if (u->tls) tls_close(); else tcp_close(); }

int http_get(const char *start_url, http_result_t *res, char *err, int errsize)
{
    return http_request(start_url, 0, res, err, errsize);
}

int http_request(const char *start_url, const http_opts_t *opts, http_result_t *res, char *err, int errsize)
{
    char url[256];
    memset(res, 0, sizeof(*res));
    for (int i = 0; ; i++) {
        url[i] = start_url[i];
        if (!start_url[i] || i == (int)sizeof(url) - 1) { url[i] = '\0'; break; }
    }

    if (!net_init()) {
        set_err(err, errsize, "No network (start QEMU with an rtl8139 card: make run)");
        return -1;
    }

    uint32_t max_body = (opts && opts->max_body) ? opts->max_body : HTTP_MAX_BODY;
    bool post = opts && opts->post_data;
    const char *post_data = post ? opts->post_data : 0;
    uint32_t post_len = post ? opts->post_len : 0;
    char referer[256];
    referer[0] = '\0';
    if (opts && opts->referer) {
        for (int i = 0; i < (int)sizeof(referer) - 1 && opts->referer[i]; i++) { referer[i] = opts->referer[i]; referer[i + 1] = '\0'; }
    }

    for (int redirects = 0; redirects <= 6; redirects++) {
        url_t u;
        int pr = url_parse(url, &u);
        if (pr < 0) {
            set_err(err, errsize, "Bad URL (use http:// or https://)");
            return -1;
        }

        uint32_t ip;
        if (!net_resolve(u.host, &ip)) {
            set_err(err, errsize, "DNS lookup failed");
            return -1;
        }
        if (!tx_connect(&u, ip, err, errsize)) {
            return -1;
        }

        /* ---- request ---- */
        char cookie[1400];
        cookies_header(u.host, u.path, u.tls != 0, cookie, sizeof(cookie));
        uint32_t rcap = 2400 + post_len + (uint32_t)strlen(u.path);
        char *req = kmalloc(rcap);
        if (!req) {
            set_err(err, errsize, "Out of memory");
            return -1;
        }
        uint32_t n = 0;
        #define ADD(str) do { const char *_s = (str); while (*_s && n < rcap - 1) req[n++] = *_s++; } while (0)
        ADD(post ? "POST " : "GET "); ADD(u.path); ADD(" HTTP/1.1\r\nHost: "); ADD(u.host);
        if ((!u.tls && u.port != 80) || (u.tls && u.port != 443)) {
            char pn[8]; char *pp = pn; uint32_t pv = u.port; char tmp[6]; int k = 0;
            do { tmp[k++] = (char)('0' + pv % 10); pv /= 10; } while (pv);
            *pp++ = ':'; while (k) *pp++ = tmp[--k]; *pp = '\0';
            ADD(pn);
        }
        ADD("\r\nUser-Agent: NinoOS/0.7 NinoBrowser\r\nAccept: text/html,*/*;q=0.8\r\nAccept-Language: en\r\n"
            "Accept-Encoding: identity\r\nConnection: close\r\n");
        if (referer[0]) { ADD("Referer: "); ADD(referer); ADD("\r\n"); }
        if (cookie[0]) { ADD("Cookie: "); ADD(cookie); ADD("\r\n"); }
        if (post) {
            char num[12]; int k = 0; uint32_t v = post_len; char tmp[12];
            do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            int m = 0; while (k) num[m++] = tmp[--k]; num[m] = '\0';
            ADD("Content-Type: application/x-www-form-urlencoded\r\nContent-Length: "); ADD(num); ADD("\r\n");
            ADD("Origin: "); ADD(u.tls ? "https://" : "http://"); ADD(u.host); ADD("\r\n");
        }
        ADD("\r\n");
        if (post) {
            for (uint32_t i = 0; i < post_len && n < rcap - 1; i++) req[n++] = post_data[i];
        }
        #undef ADD
        bool sent = tx_send(&u, req, n);
        kfree(req);
        if (!sent) {
            const char *why = u.tls ? tls_error_text() : "Sending the request failed";
            tx_close(&u);
            set_err(err, errsize, why);
            return -1;
        }

        uint32_t cap = 16384, len = 0;
        uint8_t *buf = kmalloc(cap + 1);
        if (!buf) {
            tx_close(&u);
            set_err(err, errsize, "Out of memory");
            return -1;
        }
        int truncated = 0, timed_out = 0;
        for (;;) {
            if (len == cap) {
                if (cap >= max_body + 8192) {
                    truncated = 1;
                    break;
                }
                uint32_t ncap = cap * 2;
                uint8_t *nb = kmalloc(ncap + 1);
                if (!nb) {
                    truncated = 1;
                    break;
                }
                memcpy(nb, buf, len);
                kfree(buf);
                buf = nb;
                cap = ncap;
            }
            int r = tx_recv(&u, buf + len, cap - len);
            if (r > 0) {
                len += (uint32_t)r;
            } else {
                timed_out = (r < 0);
                break;
            }
        }
        const char *tls_why = (u.tls && timed_out) ? tls_error_text() : 0;
        tx_close(&u);
        buf[len] = '\0';

        int hend = find_header_end(buf, len);
        if (hend < 0 || len < 12 || !starts_ci((const char *)buf, "http/")) {
            kfree(buf);
            set_err(err, errsize, tls_why ? tls_why
                                          : (len == 0 && timed_out) ? "Timed out waiting for the server"
                                                                    : "Invalid or empty response");
            return -1;
        }

        const char *line = (const char *)buf;
        while (*line && *line != ' ') line++;
        int status = 0;
        line = skip_ws(line);
        while (*line >= '0' && *line <= '9') status = status * 10 + (*line++ - '0');

        char location[256] = "";
        char ctype[48] = "";
        int chunked = 0;
        for (const char *h = (const char *)buf; h < (const char *)buf + hend; ) {
            const char *eol = h;
            while (eol < (const char *)buf + hend && *eol != '\n') eol++;
            if (starts_ci(h, "location:")) {
                const char *v = skip_ws(h + 9);
                int i = 0;
                while (v < eol && *v != '\r' && i < (int)sizeof(location) - 1) location[i++] = *v++;
                location[i] = '\0';
            } else if (starts_ci(h, "content-type:")) {
                const char *v = skip_ws(h + 13);
                int i = 0;
                while (v < eol && *v != '\r' && i < (int)sizeof(ctype) - 1) ctype[i++] = *v++;
                ctype[i] = '\0';
            } else if (starts_ci(h, "set-cookie:")) {
                char line[700];
                const char *v = skip_ws(h + 11);
                int i = 0;
                while (v < eol && *v != '\r' && i < (int)sizeof(line) - 1) line[i++] = *v++;
                line[i] = '\0';
                cookies_set_from_header(u.host, u.path, line);
            } else if (starts_ci(h, "content-disposition:")) {
                const char *v = h + 20;
                while (v < eol && !(lower(v[0]) == 'f' && starts_ci(v, "filename="))) v++;
                if (v < eol) {
                    v += 9;
                    char qc = (*v == '"') ? *v++ : 0;
                    int i = 0;
                    while (v < eol && *v != '\r' && *v != ';' && *v != qc && i < (int)sizeof(res->filename) - 1) res->filename[i++] = *v++;
                    res->filename[i] = '\0';
                }
            } else if (starts_ci(h, "transfer-encoding:")) {
                const char *v = skip_ws(h + 18);
                if (starts_ci(v, "chunked")) chunked = 1;
            }
            h = eol + 1;
        }

        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && location[0]) {
            char next[256];
            url_join(url, location, next, sizeof(next));
            kfree(buf);
            if (status != 307 && status != 308) {
                post = false;                                /* 301/302/303: continue with GET */
            }
            memcpy(referer, url, strlen(url) + 1 > sizeof(referer) ? sizeof(referer) : strlen(url) + 1);
            referer[sizeof(referer) - 1] = '\0';
            res->filename[0] = '\0';
            memcpy(url, next, strlen(next) + 1);
            continue;
        }

        uint32_t body_len = len - (uint32_t)hend;
        memmove(buf, buf + hend, body_len);
        if (chunked) {
            body_len = dechunk(buf, body_len);
        }
        if (body_len > max_body) {
            body_len = max_body;
            truncated = 1;
        }
        buf[body_len] = '\0';

        res->data = buf;
        res->size = body_len;
        res->status = status;
        res->truncated = truncated;
        res->secure = u.tls;
        memcpy(res->content_type, ctype, strlen(ctype) + 1);
        memcpy(res->final_url, url, strlen(url) + 1);
        return status;
    }
    set_err(err, errsize, "Too many redirects");
    return -1;
}
