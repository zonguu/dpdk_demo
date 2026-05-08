#include "packet_parser.h"

#include <stdio.h>
#include <string.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_icmp.h>
#include <rte_byteorder.h>

int packet_parse(struct rte_mbuf *mbuf, packet_info_t *info)
{
    memset(info, 0, sizeof(*info));

    if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct rte_ether_hdr)))
        return -1;

    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    info->eth_type = rte_be_to_cpu_16(eth_hdr->ether_type);
    rte_ether_addr_copy(&eth_hdr->src_addr, (struct rte_ether_addr *)info->src_mac);
    rte_ether_addr_copy(&eth_hdr->dst_addr, (struct rte_ether_addr *)info->dst_mac);

    if (info->eth_type != RTE_ETHER_TYPE_IPV4)
        return -1;  /* We only parse IPv4 for this demo */

    if (unlikely(rte_pktmbuf_data_len(mbuf) < sizeof(struct rte_ether_hdr) +
                                               sizeof(struct rte_ipv4_hdr)))
        return -1;

    struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)((char *)eth_hdr +
                                                          sizeof(struct rte_ether_hdr));
    info->is_ipv4 = 1;
    info->src_ip = ip_hdr->src_addr;
    info->dst_ip = ip_hdr->dst_addr;
    info->ip_proto = ip_hdr->next_proto_id;
    info->ip_total_len = rte_be_to_cpu_16(ip_hdr->total_length);

    /* Validate IP header length: IHL must be at least 5 (20 bytes) */
    uint8_t ihl = ip_hdr->version_ihl & 0x0F;
    if (unlikely(ihl < 5))
        return -1;

    size_t ip_hdr_len = ihl * 4;

    /* Validate IP total length >= header length */
    if (unlikely(info->ip_total_len < ip_hdr_len))
        return -1;

    /* Check for fragmentation: if MF is set or fragment offset > 0,
     * we cannot reliably parse L4 headers. */
    uint16_t frag_offset = rte_be_to_cpu_16(ip_hdr->fragment_offset);
    int is_fragmented = (frag_offset & (RTE_IPV4_HDR_MF_FLAG | RTE_IPV4_HDR_OFFSET_MASK)) != 0;

    size_t l4_offset = sizeof(struct rte_ether_hdr) + ip_hdr_len;
    void *l4_hdr = (char *)ip_hdr + ip_hdr_len;

    switch (info->ip_proto) {
    case IPPROTO_TCP:
        if (is_fragmented)
            break;
        if (likely(rte_pktmbuf_data_len(mbuf) >= l4_offset + sizeof(struct rte_tcp_hdr))) {
            struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)l4_hdr;
            info->is_tcp = 1;
            info->src_port = rte_be_to_cpu_16(tcp->src_port);
            info->dst_port = rte_be_to_cpu_16(tcp->dst_port);
        }
        break;
    case IPPROTO_UDP:
        if (is_fragmented)
            break;
        if (likely(rte_pktmbuf_data_len(mbuf) >= l4_offset + sizeof(struct rte_udp_hdr))) {
            struct rte_udp_hdr *udp = (struct rte_udp_hdr *)l4_hdr;
            info->is_udp = 1;
            info->src_port = rte_be_to_cpu_16(udp->src_port);
            info->dst_port = rte_be_to_cpu_16(udp->dst_port);
        }
        break;
    case IPPROTO_ICMP:
        info->is_icmp = 1;
        break;
    default:
        break;
    }

    return 0;
}

int packet_format(const packet_info_t *info, char *buf, size_t len)
{
    if (!info->is_ipv4) {
        return snprintf(buf, len, "eth_type=0x%04X (non-IPv4)", info->eth_type);
    }

    char src_ip[16], dst_ip[16];
    snprintf(src_ip, sizeof(src_ip), "%u.%u.%u.%u",
             (info->src_ip >> 0) & 0xFF, (info->src_ip >> 8) & 0xFF,
             (info->src_ip >> 16) & 0xFF, (info->src_ip >> 24) & 0xFF);
    snprintf(dst_ip, sizeof(dst_ip), "%u.%u.%u.%u",
             (info->dst_ip >> 0) & 0xFF, (info->dst_ip >> 8) & 0xFF,
             (info->dst_ip >> 16) & 0xFF, (info->dst_ip >> 24) & 0xFF);

    if (info->is_tcp || info->is_udp) {
        return snprintf(buf, len, "%s %s:%u -> %s:%u (len=%u)",
                        info->is_tcp ? "TCP" : "UDP",
                        src_ip, info->src_port,
                        dst_ip, info->dst_port,
                        info->ip_total_len);
    } else if (info->is_icmp) {
        return snprintf(buf, len, "ICMP %s -> %s (len=%u)",
                        src_ip, dst_ip, info->ip_total_len);
    } else {
        return snprintf(buf, len, "IP proto=%u %s -> %s (len=%u)",
                        info->ip_proto,
                        src_ip, dst_ip, info->ip_total_len);
    }
}
