/* http.h - HTTP client: http:// and (with BearSSL, see README-HTTPS.txt) https://.
 * GET and POST, redirects, cookies, chunked bodies. */
#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

#define HTTP_MAX_BODY      (512u * 1024u)          /* default limit: enough for a web page */
#define HTTP_MAX_DOWNLOAD  (6u * 1024u * 1024u)    /* limit used for downloads */

typedef struct {
    uint8_t *data;              /* kmalloc'ed, NUL-terminated; free with kfree() */
    uint32_t size;
    int      status;            /* HTTP status code */
    char     content_type[48];
    char     filename[64];      /* from Content-Disposition, "" if none */
    char     final_url[256];    /* URL after redirects */
    int      truncated;         /* 1 if the body was cut at the size limit */
    int      secure;            /* 1 if the final page came over verified TLS */
} http_result_t;

typedef struct {
    const char *post_data;      /* non-zero: send a POST with this body (application/x-www-form-urlencoded) */
    uint32_t    post_len;
    const char *referer;        /* sent as Referer (and Origin for POST) */
    uint32_t    max_body;       /* 0 = HTTP_MAX_BODY */
} http_opts_t;

/* Returns the HTTP status (200, 404, ...) or -1 on error (message in err). Follows up to 6 redirects. */
int http_request(const char *url, const http_opts_t *opts, http_result_t *res, char *err, int errsize);
int http_get(const char *url, http_result_t *res, char *err, int errsize);

#endif /* HTTP_H */
