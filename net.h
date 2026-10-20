/* net.h - network stack: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS and a tiny TCP client */
#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

/* IPv4 addresses are host-order integers: 10.0.2.15 = 0x0A00020F */
#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

bool net_init(void);                 /* first call: find the card, run DHCP. Later calls: return the result */
bool net_up(void);                   /* true once the card works and we have an address */
bool net_used_dhcp(void);            /* false = QEMU default addresses were used as a fallback */
const uint8_t *net_mac(void);
uint32_t net_ip(void);
uint32_t net_mask(void);
uint32_t net_gateway(void);
uint32_t net_dns_server(void);
char    *net_ip_str(char *buf, uint32_t ip);          /* "10.0.2.15" (buf >= 16 bytes) */
bool     net_parse_ip(const char *s, uint32_t *ip);   /* dotted quad only */

bool net_resolve(const char *host, uint32_t *ip);     /* dotted quad or DNS lookup */
int  net_ping(uint32_t ip, uint32_t timeout_ms);      /* round trip in ms, or -1 */

/* One TCP connection at a time */
/* Counters for diagnosing problems (see the 'net' command) */
typedef struct {
    uint32_t rx_frames, frag_drops, tcp_rst, tcp_rx_bytes, tcp_tx_bytes, tcp_ooo, tcp_ring_full, tcp_retx;
} net_stats_t;
const net_stats_t *net_stats(void);
uint32_t net_nic_resets(void);
bool tcp_peer_closed(void);                           /* FIN or RST seen on the current connection */

bool tcp_connect(uint32_t ip, uint16_t port);
bool tcp_send(const void *data, uint32_t len);
int  tcp_recv(void *buf, uint32_t max, uint32_t timeout_ms);   /* >0 bytes, 0 = connection closed, -1 = timeout */
void tcp_close(void);

#endif /* NET_H */
