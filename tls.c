/* tls.c - glue between BearSSL and the NinoOS TCP code. Compiled with -DNINO_HTTPS when BearSSL is present. */
#include "tls.h"

#ifndef NINO_HTTPS

bool tls_available(void) { return false; }
bool tls_connect(const char *host, uint32_t ip, uint16_t port) { (void)host; (void)ip; (void)port; return false; }
bool tls_send(const void *data, uint32_t len) { (void)data; (void)len; return false; }
int  tls_recv(void *buf, uint32_t max) { (void)buf; (void)max; return -1; }
void tls_close(void) { }
const char *tls_error_text(void)
{
    return "HTTPS is not built in. Follow README-HTTPS.txt to add BearSSL.";
}
const char *tls_status(void) { return "HTTPS: not built in (see README-HTTPS.txt)"; }

#else /* NINO_HTTPS */

#include <bearssl.h>
#include "trust_anchors.h"          /* generated: TAs[] and TAs_NUM (brssl ta) */
#include "net.h"
#include "rtc.h"
#include "timer.h"
#include "io.h"
#include "util.h"
#include "string.h"

static br_ssl_client_context  sc;
static br_x509_minimal_context xc;
static br_sslio_context       ioc;
static unsigned char          iobuf[BR_SSL_BUFSIZE_BIDI];
static bool                   connected;
static bool                   seeded;
static uint32_t               bytes_out, bytes_in;
static int                    last_read;          /* 0 = peer closed, -1 = timeout, >0 = bytes */
static char                   errtext[80];
static bool                   rng_hw;               /* seeded from RDRAND */

typedef struct {
    char host[64];
    br_ssl_session_parameters sp;
    bool valid;
} sess_t;
static sess_t                 sessions[4];          /* TLS session cache: skips the slow handshake on reuse */
static int                    sess_next;
static char                   cur_host[64];
static bool                   sess_saved;

static void set_err(const char *msg)
{
    int i = 0;
    while (msg[i] && i < (int)sizeof(errtext) - 1) {
        errtext[i] = msg[i];
        i++;
    }
    errtext[i] = '\0';
}

const char *tls_error_text(void)
{
    return errtext[0] ? errtext : "TLS error";
}

static bool cpu_has_rdrand(void);

bool tls_available(void)
{
    return true;
}

const char *tls_status(void)
{
    if (!seeded) {
        rng_hw = cpu_has_rdrand();
    }
    return rng_hw ? "HTTPS: on, random numbers from the CPU (RDRAND)"
                  : "HTTPS: on, WEAK random numbers (start QEMU with -cpu max)";
}

/* ---------- Low-level I/O for BearSSL on top of our TCP ---------- */
static int sock_read(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    int r = tcp_recv(buf, (uint32_t)len, 10000);
    last_read = r;
    if (r > 0) {
        bytes_in += (uint32_t)r;
        return r;
    }
    return -1;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    if (tcp_send(buf, (uint32_t)len)) {
        bytes_out += (uint32_t)len;
        return (int)len;
    }
    last_read = -2;                                      /* the send failed */
    return -1;
}

/* ---------- Entropy ----------
 * NinoOS has no hardware random generator. We mix the CPU cycle counter, the PIT counter, the clock
 * and the jitter of timer interrupts. This is fine for a hobby OS but NOT cryptographically strong. */
static uint32_t mix(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static bool cpu_has_rdrand(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (c >> 30) & 1;
}

static bool rdrand32(uint32_t *v)
{
    for (int i = 0; i < 10; i++) {
        uint32_t r;
        uint8_t ok;
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(r), "=qm"(ok) : : "cc");
        if (ok) {
            *v = r;
            return true;
        }
    }
    return false;
}

static void gather_entropy(uint8_t *out, int n)
{
    uint8_t hh, mm, ss;
    rtc_get_time(&hh, &mm, &ss);
    uint32_t x = 0x9E3779B9u ^ timer_ticks() ^ ((uint32_t)ss << 16) ^ ((uint32_t)mm << 8) ^ hh;

    for (int i = 0; i < n; i++) {
        uint32_t lo, hi;
        __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
        outb(0x43, 0x00);                                /* latch PIT channel 0 */
        uint32_t pit = inb(0x40);
        pit |= (uint32_t)inb(0x40) << 8;
        x = mix(x ^ lo ^ (hi * 2654435761u) ^ (pit << 11));
        if (!seeded && (i % 16) == 15) {
            __asm__ volatile ("sti; hlt");               /* wait for an interrupt: timing jitter */
        }
        out[i] = (uint8_t)((x >> 8) ^ (x >> 19) ^ x);
    }
    seeded = true;

    /* Mix in the CPU's hardware random number generator when there is one (QEMU: -cpu max) */
    rng_hw = false;
    if (cpu_has_rdrand()) {
        bool all = true;
        for (int i = 0; i < n; i += 4) {
            uint32_t r;
            if (!rdrand32(&r)) { all = false; break; }
            for (int k = 0; k < 4 && i + k < n; k++) out[i + k] ^= (uint8_t)(r >> (8 * k));
        }
        rng_hw = all;
    }
}

/* ---------- Time for certificate validation: days since 0000-01-01 and seconds in the day ---------- */
/* ---------- Public API ---------- */
bool tls_connect(const char *host, uint32_t ip, uint16_t port)
{
    errtext[0] = '\0';
    connected = false;
    bytes_out = bytes_in = 0;
    last_read = 1;
    if (!tcp_connect(ip, port)) {
        set_err("Connection failed");
        return false;
    }

    uint16_t year;
    uint8_t month, day, hh, mm, ss;
    rtc_get_date(&year, &month, &day);
    rtc_get_time(&hh, &mm, &ss);

    br_ssl_client_init_full(&sc, &xc, TAs, TAs_NUM);
    br_x509_minimal_set_time(&xc, days_from_civil(year, month, day) + 719528u,
                             (uint32_t)hh * 3600u + (uint32_t)mm * 60u + ss);

    uint8_t seed[64];
    gather_entropy(seed, sizeof(seed));
    br_ssl_engine_inject_entropy(&sc.eng, seed, sizeof(seed));

    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);

    int hl = 0;
    while (host[hl] && hl < (int)sizeof(cur_host) - 1) { cur_host[hl] = host[hl]; hl++; }
    cur_host[hl] = '\0';
    sess_saved = false;
    int resume = 0;
    for (int i = 0; i < 4; i++) {
        if (sessions[i].valid && strcmp(sessions[i].host, cur_host) == 0) {
            br_ssl_engine_set_session_parameters(&sc.eng, &sessions[i].sp);
            resume = 1;
            break;
        }
    }
    if (!br_ssl_client_reset(&sc, host, resume)) {
        tcp_close();
        set_err("TLS setup failed");
        return false;
    }
    br_sslio_init(&ioc, &sc.eng, sock_read, 0, sock_write, 0);
    connected = true;
    return true;
}

static void explain_error(void)
{
    int e = br_ssl_engine_last_error(&sc.eng);
    char buf[96];
    switch (e) {
    case BR_ERR_X509_EXPIRED:
        set_err("TLS: certificate expired or not yet valid (is the clock right?)");
        return;
    case BR_ERR_X509_NOT_TRUSTED:
        set_err("TLS: certificate not trusted (CA missing in trust_anchors.h)");
        return;
    case BR_ERR_X509_BAD_SERVER_NAME:
        set_err("TLS: certificate does not match the host name");
        return;
    case BR_ERR_NO_RANDOM:
        set_err("TLS: no random numbers available");
        return;
    case BR_ERR_IO: {
        char *p = put_str(buf, "TLS IO: sent ");
        p = put_uint(p, bytes_out);
        p = put_str(p, ", got ");
        p = put_uint(p, bytes_in);
        p = put_str(p, last_read == 0 ? ", server closed" : last_read == -1 ? ", timeout" :
                       last_read == -2 ? ", send failed" : ", ?");
        set_err(buf);
        return;
    }
    default:
        break;
    }
    char *p = put_str(buf, "TLS error code ");
    put_uint(p, (uint32_t)e);
    set_err(buf);
}

bool tls_send(const void *data, uint32_t len)
{
    if (!connected) {
        return false;
    }
    if (br_sslio_write_all(&ioc, data, len) < 0 || br_sslio_flush(&ioc) < 0) {
        explain_error();
        return false;
    }
    if (!sess_saved) {                                   /* the handshake is done: remember the session */
        sess_saved = true;
        br_ssl_session_parameters sp;
        br_ssl_engine_get_session_parameters(&sc.eng, &sp);
        if (sp.session_id_len > 0) {
            int slot = sess_next;
            for (int i = 0; i < 4; i++) {
                if (sessions[i].valid && strcmp(sessions[i].host, cur_host) == 0) slot = i;
            }
            if (slot == sess_next) sess_next = (sess_next + 1) & 3;
            sessions[slot].sp = sp;
            sessions[slot].valid = true;
            memcpy(sessions[slot].host, cur_host, strlen(cur_host) + 1);
        }
    }
    return true;
}

int tls_recv(void *buf, uint32_t max)
{
    if (!connected) {
        return -1;
    }
    int r = br_sslio_read(&ioc, buf, max);
    if (r > 0) {
        return r;
    }
    int e = br_ssl_engine_last_error(&sc.eng);
    if (e == BR_ERR_OK || e == BR_ERR_IO) {
        return 0;                                        /* closed by the server: end of data */
    }
    explain_error();
    return -1;
}

void tls_close(void)
{
    connected = false;
    tcp_close();                                         /* no close_notify: avoids waiting for the server */
}

#endif /* NINO_HTTPS */
