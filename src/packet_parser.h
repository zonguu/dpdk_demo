#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include <stdint.h>
#include <rte_mbuf.h>

typedef struct {
    uint16_t eth_type;          /* Ethernet type (host byte order) */
    uint8_t  src_mac[6];
    uint8_t  dst_mac[6];
    uint8_t  is_ipv4;
    uint8_t  ip_proto;
    uint32_t src_ip;            /* network byte order */
    uint32_t dst_ip;            /* network byte order */
    uint8_t  is_tcp;
    uint8_t  is_udp;
    uint8_t  is_icmp;
    uint16_t src_port;          /* host byte order, 0 if not tcp/udp */
    uint16_t dst_port;          /* host byte order, 0 if not tcp/udp */
    uint16_t ip_total_len;
} packet_info_t;

/*
 * Parse Ethernet + IPv4 + TCP/UDP/ICMP headers from mbuf.
 * Returns 0 on success, -1 on failure or non-IPv4.
 */
int packet_parse(struct rte_mbuf *mbuf, packet_info_t *info);

/*
 * Format parsed info into a human-readable string.
 * Returns number of bytes written (excluding null).
 */
int packet_format(const packet_info_t *info, char *buf, size_t len);

#endif /* PACKET_PARSER_H */
