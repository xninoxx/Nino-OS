/* tls.h - HTTPS support. The real code needs BearSSL (see README-HTTPS.txt); without it every
 * function reports "not available" and the rest of the system keeps working. */
#ifndef TLS_H
#define TLS_H

#include <stdint.h>
#include <stdbool.h>

bool        tls_available(void);                       /* true if built with BearSSL */
bool        tls_connect(const char *host, uint32_t ip, uint16_t port);   /* TCP connect + TLS setup */
bool        tls_send(const void *data, uint32_t len);  /* the handshake happens here, on first use */
int         tls_recv(void *buf, uint32_t max);         /* >0 bytes, 0 = end of data, -1 = error */
void        tls_close(void);
const char *tls_status(void);                          /* one-line status incl. random number source */
const char *tls_error_text(void);                      /* human readable reason for the last failure */

#endif /* TLS_H */
