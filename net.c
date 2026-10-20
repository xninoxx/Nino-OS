/* net.c - a small polling TCP/IP stack for the RTL8139.
 * Everything is blocking: functions poll the card and halt until the next timer tick while they wait. */
#include "net.h"
#include "nic.h"
#include "timer.h"
#include "string.h"

/* ---------- Byte helpers (network byte order) ---------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static uint32_t csum_add(uint32_t sum, const uint8_t *d, uint32_t n)
{
    uint32_t i = 0;
    for (; i + 1 < n; i += 2) {
        sum += (uint32_t)((d[i] << 8) | d[i + 1]);
    }
    if (n & 1) {
        sum += (uint32_t)d[n - 1] << 8;
    }
    return sum;
}

static uint16_t csum_fin(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* ---------- State ---------- */
static net_stats_t stats;
static bool     inited, nic_ok, is_up, used_dhcp;
static uint8_t  my_mac[6];
static uint32_t my_ip, my_mask, my_gw, my_dns;
static uint16_t ip_id;
static const uint8_t bcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint32_t now_ms(void) { return timer_ticks() * (1000 / TIMER_HZ); }

static void net_poll(void);
static void wait_step(void)
{
    net_poll();
    __asm__ volatile ("sti; hlt");        /* sleep until the next interrupt (timer: 10 ms) */
}

/* ---------- Ethernet ---------- */
static uint8_t txf[1600];

static bool eth_send(const uint8_t *dst, uint16_t type, const uint8_t *payload, uint32_t len)
{
    if (len > 1500) {
        return false;
    }
    memcpy(txf, dst, 6);
    memcpy(txf + 6, my_mac, 6);
    wr16(txf + 12, type);
    memcpy(txf + 14, payload, len);
    return nic_send(txf, 14 + len);
}

/* ---------- ARP ---------- */
typedef struct { uint32_t ip; uint8_t mac[6]; bool valid; } arp_entry_t;
static arp_entry_t arp_table[8];
static int arp_next;

static void arp_store(uint32_t ip, const uint8_t *mac)
{
    for (int i = 0; i < 8; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            memcpy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    arp_entry_t *e = &arp_table[arp_next];
    arp_next = (arp_next + 1) & 7;
    e->ip = ip;
    e->valid = true;
    memcpy(e->mac, mac, 6);
}

static bool arp_cached(uint32_t ip, uint8_t *mac)
{
    for (int i = 0; i < 8; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            memcpy(mac, arp_table[i].mac, 6);
            return true;
        }
    }
    return false;
}

static void arp_send(uint16_t op, const uint8_t *tha, uint32_t tpa, const uint8_t *eth_dst)
{
    uint8_t p[28];
    wr16(p, 1);                 /* Ethernet */
    wr16(p + 2, 0x0800);        /* IPv4 */
    p[4] = 6;
    p[5] = 4;
    wr16(p + 6, op);
    memcpy(p + 8, my_mac, 6);
    wr32(p + 14, my_ip);
    memcpy(p + 18, tha, 6);
    wr32(p + 24, tpa);
    eth_send(eth_dst, 0x0806, p, 28);
}

static bool arp_resolve(uint32_t ip, uint8_t *mac)
{
    static const uint8_t zero[6] = { 0 };
    for (int attempt = 0; attempt < 4; attempt++) {
        if (arp_cached(ip, mac)) {
            return true;
        }
        arp_send(1, zero, ip, bcast_mac);
        uint32_t start = now_ms();
        while (now_ms() - start < 500) {
            wait_step();
            if (arp_cached(ip, mac)) {
                return true;
            }
        }
    }
    return false;
}

static void arp_input(const uint8_t *p, uint32_t len)
{
    if (len < 28 || rd16(p) != 1 || rd16(p + 2) != 0x0800) {
        return;
    }
    uint16_t op = rd16(p + 6);
    uint32_t spa = rd32(p + 14);
    uint32_t tpa = rd32(p + 24);
    arp_store(spa, p + 8);
    if (op == 1 && my_ip && tpa == my_ip) {
        arp_send(2, p + 8, spa, p + 8);
    }
}

/* ---------- IPv4 ---------- */
static bool ip_send_to(const uint8_t *mac, uint32_t src, uint32_t dst, uint8_t proto,
                       const uint8_t *payload, uint32_t len)
{
    static uint8_t pkt[1600];
    if (len > 1480) {
        return false;
    }
    pkt[0] = 0x45;
    pkt[1] = 0;
    wr16(pkt + 2, (uint16_t)(20 + len));
    wr16(pkt + 4, ip_id++);
    wr16(pkt + 6, 0x4000);              /* don't fragment */
    pkt[8] = 64;
    pkt[9] = proto;
    wr16(pkt + 10, 0);
    wr32(pkt + 12, src);
    wr32(pkt + 16, dst);
    wr16(pkt + 10, csum_fin(csum_add(0, pkt, 20)));
    memcpy(pkt + 20, payload, len);
    return eth_send(mac, 0x0800, pkt, 20 + len);
}

static bool ip_send(uint32_t dst, uint8_t proto, const uint8_t *payload, uint32_t len)
{
    uint8_t mac[6];
    uint32_t next = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
    if (!arp_resolve(next, mac)) {
        return false;
    }
    return ip_send_to(mac, my_ip, dst, proto, payload, len);
}

/* ---------- ICMP (ping) ---------- */
static uint16_t ping_id = 0x4E49;
static uint16_t ping_seq;
static volatile bool ping_got;

static void icmp_input(uint32_t src, const uint8_t *p, uint32_t len)
{
    if (len < 8) {
        return;
    }
    if (p[0] == 8) {                                    /* echo request: answer it */
        static uint8_t reply[1480];
        if (len > sizeof(reply)) {
            return;
        }
        memcpy(reply, p, len);
        reply[0] = 0;
        wr16(reply + 2, 0);
        wr16(reply + 2, csum_fin(csum_add(0, reply, len)));
        ip_send(src, 1, reply, len);
    } else if (p[0] == 0 && rd16(p + 4) == ping_id && rd16(p + 6) == ping_seq) {
        ping_got = true;
    }
}

int net_ping(uint32_t ip, uint32_t timeout_ms)
{
    uint8_t p[40];
    memset(p, 0, sizeof(p));
    p[0] = 8;
    wr16(p + 4, ping_id);
    wr16(p + 6, ++ping_seq);
    for (int i = 8; i < 40; i++) {
        p[i] = (uint8_t)('a' + (i % 23));
    }
    wr16(p + 2, csum_fin(csum_add(0, p, 40)));

    ping_got = false;
    uint32_t start = now_ms();
    if (!ip_send(ip, 1, p, 40)) {
        return -1;
    }
    while (now_ms() - start < timeout_ms) {
        wait_step();
        if (ping_got) {
            return (int)(now_ms() - start);
        }
    }
    return -1;
}

/* ---------- UDP, DHCP, DNS ---------- */
static bool udp_send_to(const uint8_t *mac, uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport,
                        const uint8_t *data, uint32_t len)
{
    static uint8_t d[1500];
    if (len > 1400) {
        return false;
    }
    wr16(d, sport);
    wr16(d + 2, dport);
    wr16(d + 4, (uint16_t)(8 + len));
    wr16(d + 6, 0);                                     /* checksum optional in IPv4 */
    memcpy(d + 8, data, len);
    return ip_send_to(mac, src, dst, 17, d, 8 + len);
}

#define DHCP_XID   0x4E494E4Fu
#define DNS_SPORT  53053

static int      dhcp_type;                              /* 2 = offer, 5 = ack */
static uint32_t dhcp_yiaddr, dhcp_server, dhcp_router, dhcp_mask_o, dhcp_dns_o;

static uint8_t  dns_reply[512];
static uint32_t dns_reply_len;
static uint16_t dns_id;

static void dhcp_input(const uint8_t *p, uint32_t len)
{
    if (len < 240 || p[0] != 2 || rd32(p + 4) != DHCP_XID || rd32(p + 236) != 0x63825363u) {
        return;
    }
    int type = 0;
    uint32_t server = 0, router = 0, mask = 0, dns = 0;
    for (uint32_t i = 240; i < len;) {
        uint8_t code = p[i++];
        if (code == 255) break;
        if (code == 0) continue;
        if (i >= len) break;
        uint8_t l = p[i++];
        if (i + l > len) break;
        if (code == 53 && l >= 1) type = p[i];
        if (code == 54 && l >= 4) server = rd32(p + i);
        if (code == 3 && l >= 4) router = rd32(p + i);
        if (code == 1 && l >= 4) mask = rd32(p + i);
        if (code == 6 && l >= 4) dns = rd32(p + i);
        i += l;
    }
    if (type == 2 || type == 5) {
        dhcp_type = type;
        dhcp_yiaddr = rd32(p + 16);
        dhcp_server = server;
        dhcp_router = router;
        dhcp_mask_o = mask;
        dhcp_dns_o = dns;
    }
}

static void dhcp_send(uint8_t msg_type, uint32_t req_ip, uint32_t server)
{
    uint8_t p[300];
    memset(p, 0, sizeof(p));
    p[0] = 1;                       /* BOOTREQUEST */
    p[1] = 1;                       /* Ethernet */
    p[2] = 6;
    wr32(p + 4, DHCP_XID);
    wr16(p + 10, 0x8000);           /* ask for a broadcast reply (we have no address yet) */
    memcpy(p + 28, my_mac, 6);
    wr32(p + 236, 0x63825363u);
    int i = 240;
    p[i++] = 53; p[i++] = 1; p[i++] = msg_type;
    if (req_ip) {
        p[i++] = 50; p[i++] = 4; wr32(p + i, req_ip); i += 4;
        p[i++] = 54; p[i++] = 4; wr32(p + i, server); i += 4;
    }
    p[i++] = 55; p[i++] = 3; p[i++] = 1; p[i++] = 3; p[i++] = 6;
    p[i++] = 255;
    udp_send_to(bcast_mac, 0, 0xFFFFFFFFu, 68, 67, p, 300);
}

static bool dhcp_run(void)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        dhcp_type = 0;
        dhcp_send(1, 0, 0);                              /* DISCOVER */
        uint32_t start = now_ms();
        while (now_ms() - start < 1500 && dhcp_type != 2) {
            wait_step();
        }
        if (dhcp_type != 2) {
            continue;
        }
        uint32_t offered = dhcp_yiaddr, server = dhcp_server;
        uint32_t router = dhcp_router, mask = dhcp_mask_o, dns = dhcp_dns_o;

        dhcp_type = 0;
        dhcp_send(3, offered, server);                   /* REQUEST */
        start = now_ms();
        while (now_ms() - start < 1500 && dhcp_type != 5) {
            wait_step();
        }
        if (dhcp_type == 5) {
            my_ip = offered;
            my_mask = mask ? mask : IP4(255, 255, 255, 0);
            my_gw = router;
            my_dns = dns ? dns : router;
            return true;
        }
    }
    return false;
}

static void dns_input(const uint8_t *p, uint32_t len)
{
    if (len >= 12 && rd16(p) == dns_id && len <= sizeof(dns_reply)) {
        memcpy(dns_reply, p, len);
        dns_reply_len = len;
    }
}

static uint32_t dns_skip_name(const uint8_t *p, uint32_t len, uint32_t i)
{
    while (i < len) {
        uint8_t l = p[i];
        if (l == 0) return i + 1;
        if ((l & 0xC0) == 0xC0) return i + 2;           /* compression pointer ends the name */
        i += 1u + l;
    }
    return len;
}

bool net_parse_ip(const char *s, uint32_t *ip)
{
    uint32_t v = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return false;
        uint32_t n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (uint32_t)(*s++ - '0');
            if (n > 255) return false;
        }
        v = (v << 8) | n;
        if (part < 3) {
            if (*s != '.') return false;
            s++;
        }
    }
    if (*s) return false;
    *ip = v;
    return true;
}

bool net_resolve(const char *host, uint32_t *ip)
{
    if (net_parse_ip(host, ip)) {
        return true;
    }
    if (!is_up || !my_dns) {
        return false;
    }
    uint8_t q[300];
    uint32_t hl = (uint32_t)strlen(host);
    if (hl == 0 || hl > 200) {
        return false;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        dns_id = (uint16_t)(0x1000 + timer_ticks());
        uint32_t n = 12;
        memset(q, 0, 12);
        wr16(q, dns_id);
        wr16(q + 2, 0x0100);                             /* recursion desired */
        wr16(q + 4, 1);                                  /* one question */
        const char *s = host;
        while (*s) {                                     /* labels */
            const char *dot = s;
            while (*dot && *dot != '.') dot++;
            uint32_t ll = (uint32_t)(dot - s);
            if (ll == 0 || ll > 63) return false;
            q[n++] = (uint8_t)ll;
            memcpy(q + n, s, ll);
            n += ll;
            s = *dot ? dot + 1 : dot;
        }
        q[n++] = 0;
        wr16(q + n, 1); n += 2;                          /* type A */
        wr16(q + n, 1); n += 2;                          /* class IN */

        dns_reply_len = 0;
        uint8_t mac[6];
        uint32_t next = ((my_dns & my_mask) == (my_ip & my_mask)) ? my_dns : my_gw;
        if (!arp_resolve(next, mac)) {
            return false;
        }
        udp_send_to(mac, my_ip, my_dns, DNS_SPORT, 53, q, n);

        uint32_t start = now_ms();
        while (now_ms() - start < 2000 && !dns_reply_len) {
            wait_step();
        }
        if (!dns_reply_len) {
            continue;
        }
        const uint8_t *r = dns_reply;
        uint32_t rl = dns_reply_len;
        if ((r[3] & 0x0F) != 0) {
            return false;                                /* NXDOMAIN or server failure */
        }
        uint16_t qd = rd16(r + 4), an = rd16(r + 6);
        uint32_t i = 12;
        for (uint16_t k = 0; k < qd; k++) {
            i = dns_skip_name(r, rl, i) + 4;
        }
        for (uint16_t k = 0; k < an && i < rl; k++) {
            i = dns_skip_name(r, rl, i);
            if (i + 10 > rl) break;
            uint16_t type = rd16(r + i);
            uint16_t rdl = rd16(r + i + 8);
            i += 10;
            if (type == 1 && rdl == 4 && i + 4 <= rl) {
                *ip = rd32(r + i);
                return true;
            }
            i += rdl;
        }
        return false;                                    /* answer without an A record */
    }
    return false;
}

static void udp_input(const uint8_t *p, uint32_t len)
{
    if (len < 8) {
        return;
    }
    uint16_t dport = rd16(p + 2);
    uint32_t ulen = rd16(p + 4);
    if (ulen < 8 || ulen > len) {
        return;
    }
    if (dport == 68) {
        dhcp_input(p + 8, ulen - 8);
    } else if (dport == DNS_SPORT) {
        dns_input(p + 8, ulen - 8);
    }
}

/* ---------- TCP (one connection, client side) ---------- */
enum { T_CLOSED, T_SYN_SENT, T_ESTABLISHED, T_FIN_WAIT };

#define RING_SIZE 65536
static struct {
    bool     used;
    int      state;
    uint32_t rip;
    uint16_t lport, rport;
    uint32_t snd_una, snd_nxt, rcv_nxt;
    bool     fin_rcvd;
} conn;

static uint8_t  ring[RING_SIZE];
static uint32_t ring_head, ring_tail;                    /* monotonic counters */

static bool tcp_emit(uint8_t flags, uint32_t seq, const uint8_t *data, uint32_t len)
{
    static uint8_t seg[24 + 1460];
    uint32_t hl = (flags & 0x02) ? 24 : 20;
    uint32_t free_space = RING_SIZE - (ring_head - ring_tail);
    if (free_space > 32768) free_space = 32768;

    wr16(seg, conn.lport);
    wr16(seg + 2, conn.rport);
    wr32(seg + 4, seq);
    wr32(seg + 8, (flags & 0x10) ? conn.rcv_nxt : 0);
    seg[12] = (uint8_t)((hl / 4) << 4);
    seg[13] = flags;
    wr16(seg + 14, (uint16_t)free_space);
    wr16(seg + 16, 0);
    wr16(seg + 18, 0);
    if (hl == 24) {                                      /* MSS option on SYN */
        seg[20] = 2; seg[21] = 4;
        wr16(seg + 22, 1400);
    }
    if (len) {
        memcpy(seg + hl, data, len);
    }

    uint8_t ph[12];
    wr32(ph, my_ip);
    wr32(ph + 4, conn.rip);
    ph[8] = 0;
    ph[9] = 6;
    wr16(ph + 10, (uint16_t)(hl + len));
    uint32_t sum = csum_add(0, ph, 12);
    sum = csum_add(sum, seg, hl + len);
    wr16(seg + 16, csum_fin(sum));
    return ip_send(conn.rip, 6, seg, hl + len);
}

static void tcp_input(uint32_t src, const uint8_t *seg, uint32_t len)
{
    if (!conn.used || src != conn.rip || len < 20) {
        return;
    }
    if (rd16(seg) != conn.rport || rd16(seg + 2) != conn.lport) {
        return;
    }
    uint32_t seq = rd32(seg + 4), ack = rd32(seg + 8);
    uint32_t off = (uint32_t)(seg[12] >> 4) * 4;
    uint8_t flags = seg[13];
    if (off < 20 || off > len) {
        return;
    }
    const uint8_t *data = seg + off;
    uint32_t dlen = len - off;

    if (flags & 0x04) {                                  /* RST */
        stats.tcp_rst++;
        conn.state = T_CLOSED;
        return;
    }
    if (conn.state == T_SYN_SENT) {
        if ((flags & 0x12) == 0x12 && ack == conn.snd_nxt) {
            conn.rcv_nxt = seq + 1;
            conn.snd_una = ack;
            conn.state = T_ESTABLISHED;
            tcp_emit(0x10, conn.snd_nxt, 0, 0);
        }
        return;
    }
    if ((flags & 0x10) && (int32_t)(ack - conn.snd_una) > 0 && (int32_t)(conn.snd_nxt - ack) >= 0) {
        conn.snd_una = ack;
    }
    if (conn.state != T_ESTABLISHED && conn.state != T_FIN_WAIT) {
        return;
    }

    bool need_ack = false;
    if (dlen) {
        if (seq != conn.rcv_nxt) {
            stats.tcp_ooo++;
        } else if (RING_SIZE - (ring_head - ring_tail) < dlen) {
            stats.tcp_ring_full++;
        }
        if (seq == conn.rcv_nxt && RING_SIZE - (ring_head - ring_tail) >= dlen) {
            stats.tcp_rx_bytes += dlen;
            uint32_t pos = ring_head % RING_SIZE;
            uint32_t first = RING_SIZE - pos < dlen ? RING_SIZE - pos : dlen;
            memcpy(ring + pos, data, first);
            memcpy(ring, data + first, dlen - first);
            ring_head += dlen;
            conn.rcv_nxt += dlen;
        }
        need_ack = true;                                 /* also re-ACKs duplicates / out-of-order data */
    }
    if (flags & 0x01) {                                  /* FIN */
        if (seq + dlen == conn.rcv_nxt) {
            conn.rcv_nxt++;
            conn.fin_rcvd = true;
        }
        need_ack = true;
    }
    if (need_ack) {
        tcp_emit(0x10, conn.snd_nxt, 0, 0);
    }
}

bool tcp_connect(uint32_t ip, uint16_t port)
{
    if (!is_up) {
        return false;
    }
    memset(&conn, 0, sizeof(conn));
    ring_head = ring_tail = 0;
    conn.used = true;
    conn.rip = ip;
    conn.rport = port;
    conn.lport = (uint16_t)(40000 + (timer_ticks() & 0x3FFF));
    uint32_t iss = timer_ticks() * 64013u + 0x1234567u;
    conn.snd_una = iss;
    conn.snd_nxt = iss + 1;                              /* the SYN uses one sequence number */
    conn.state = T_SYN_SENT;

    for (int attempt = 0; attempt < 4; attempt++) {
        tcp_emit(0x02, iss, 0, 0);
        uint32_t start = now_ms();
        while (now_ms() - start < 1500 && conn.state == T_SYN_SENT) {
            wait_step();
        }
        if (conn.state == T_ESTABLISHED) {
            return true;
        }
        if (conn.state == T_CLOSED) {
            break;                                       /* reset by the peer */
        }
    }
    conn.used = false;
    conn.state = T_CLOSED;
    return false;
}

bool tcp_send(const void *data, uint32_t len)
{
    const uint8_t *d = data;
    while (len) {
        uint32_t chunk = len > 1400 ? 1400 : len;
        uint32_t seq = conn.snd_nxt;
        uint32_t target = seq + chunk;
        conn.snd_nxt = target;                           /* the data is in flight: ACKs up to here are valid */

        bool acked = false;
        for (int attempt = 0; attempt < 5 && !acked; attempt++) {
            if (conn.state != T_ESTABLISHED) {
                return false;
            }
            if (attempt > 0) {
                stats.tcp_retx++;
            }
            tcp_emit(0x18, seq, d, chunk);               /* PSH | ACK */
            uint32_t start = now_ms();
            while (now_ms() - start < 1000) {
                wait_step();
                if (conn.snd_una == target || conn.state == T_CLOSED) {
                    break;
                }
            }
            acked = (conn.snd_una == target);
        }
        if (!acked) {
            return false;
        }
        stats.tcp_tx_bytes += chunk;
        d += chunk;
        len -= chunk;
    }
    return true;
}

int tcp_recv(void *buf, uint32_t max, uint32_t timeout_ms)
{
    uint8_t *out = buf;
    uint32_t start = now_ms();
    for (;;) {
        uint32_t avail = ring_head - ring_tail;
        if (avail) {
            uint32_t n = avail < max ? avail : max;
            uint32_t pos = ring_tail % RING_SIZE;
            uint32_t first = RING_SIZE - pos < n ? RING_SIZE - pos : n;
            memcpy(out, ring + pos, first);
            memcpy(out + first, ring, n - first);
            ring_tail += n;
            return (int)n;
        }
        if (conn.fin_rcvd || conn.state == T_CLOSED || !conn.used) {
            return 0;
        }
        if (now_ms() - start >= timeout_ms) {
            return -1;
        }
        wait_step();
    }
}

void tcp_close(void)
{
    if (conn.used && conn.state == T_ESTABLISHED) {
        uint32_t fin_seq = conn.snd_nxt;
        conn.snd_nxt++;
        conn.state = T_FIN_WAIT;
        tcp_emit(0x11, fin_seq, 0, 0);                   /* FIN | ACK */
        uint32_t start = now_ms();
        while (now_ms() - start < 500 && !(conn.fin_rcvd && conn.snd_una == conn.snd_nxt)) {
            wait_step();
        }
    }
    conn.used = false;
    conn.state = T_CLOSED;
}

/* ---------- Frame dispatch ---------- */
static void handle_frame(const uint8_t *f, uint32_t len)
{
    stats.rx_frames++;
    if (len < 14) {
        return;
    }
    uint16_t type = rd16(f + 12);
    const uint8_t *p = f + 14;
    uint32_t plen = len - 14;

    if (type == 0x0806) {
        arp_input(p, plen);
        return;
    }
    if (type != 0x0800 || plen < 20 || (p[0] >> 4) != 4) {
        return;
    }
    uint32_t ihl = (uint32_t)(p[0] & 0x0F) * 4;
    uint32_t total = rd16(p + 2);
    if (ihl < 20 || total < ihl || total > plen) {
        return;
    }
    uint32_t src = rd32(p + 12), dst = rd32(p + 16);
    bool for_us = (my_ip == 0) || dst == my_ip || dst == 0xFFFFFFFFu;
    if (!for_us) {
        return;
    }
    if (rd16(p + 6) & 0x3FFF) {
        stats.frag_drops++;
        return;                                          /* fragment: not supported */
    }
    const uint8_t *payload = p + ihl;
    uint32_t pl = total - ihl;
    switch (p[9]) {
    case 1:  icmp_input(src, payload, pl); break;
    case 6:  tcp_input(src, payload, pl);  break;
    case 17: udp_input(payload, pl);       break;
    default: break;
    }
}

static bool polling;
static void net_poll(void)
{
    if (polling || !nic_ok) {
        return;
    }
    polling = true;
    nic_poll(handle_frame);
    polling = false;
}

/* ---------- Public API ---------- */
bool net_init(void)
{
    if (inited) {
        return is_up;
    }
    inited = true;
    nic_ok = nic_init(my_mac);
    if (!nic_ok) {
        return false;
    }
    if (dhcp_run()) {
        used_dhcp = true;
    } else {                                             /* QEMU user-mode networking defaults */
        my_ip = IP4(10, 0, 2, 15);
        my_mask = IP4(255, 255, 255, 0);
        my_gw = IP4(10, 0, 2, 2);
        my_dns = IP4(10, 0, 2, 3);
        used_dhcp = false;
    }
    is_up = true;
    return true;
}

const net_stats_t *net_stats(void) { return &stats; }
uint32_t net_nic_resets(void) { return nic_resets(); }
bool tcp_peer_closed(void)  { return conn.fin_rcvd || conn.state == T_CLOSED; }
bool net_up(void)           { return is_up; }
bool net_used_dhcp(void)    { return used_dhcp; }
const uint8_t *net_mac(void){ return my_mac; }
uint32_t net_ip(void)       { return my_ip; }
uint32_t net_mask(void)     { return my_mask; }
uint32_t net_gateway(void)  { return my_gw; }
uint32_t net_dns_server(void) { return my_dns; }

char *net_ip_str(char *buf, uint32_t ip)
{
    char *p = buf;
    for (int i = 3; i >= 0; i--) {
        uint32_t v = (ip >> (8 * i)) & 0xFF;
        char tmp[4];
        int n = 0;
        do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) *p++ = tmp[--n];
        if (i) *p++ = '.';
    }
    *p = '\0';
    return buf;
}
