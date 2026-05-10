#include "packet_modify.h"
#include "packet_parser.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

void swap_mac_addresses(struct rte_mbuf *mbuf)
{
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    struct rte_ether_addr tmp;
    rte_ether_addr_copy(&eth->src_addr, &tmp);
    rte_ether_addr_copy(&eth->dst_addr, &eth->src_addr);
    rte_ether_addr_copy(&tmp, &eth->dst_addr);
}

void packet_modify_swap_ip(struct rte_mbuf *m)
{
    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    uint32_t tmp = ip->src_addr;
    ip->src_addr = ip->dst_addr;
    ip->dst_addr = tmp;
}

void packet_modify_swap_port(struct rte_mbuf *m)
{
    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;

    if (ip->next_proto_id == IPPROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)((char *)ip + ihl);
        uint16_t tmp = tcp->src_port;
        tcp->src_port = tcp->dst_port;
        tcp->dst_port = tmp;
    } else if (ip->next_proto_id == IPPROTO_UDP) {
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((char *)ip + ihl);
        uint16_t tmp = udp->src_port;
        udp->src_port = udp->dst_port;
        udp->dst_port = tmp;
    }
}

void packet_modify_recalc_checksum(struct rte_mbuf *m)
{
    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;

    ip->hdr_checksum = 0;

    if (ip->next_proto_id == IPPROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)((char *)ip + ihl);
        tcp->cksum = 0;
        tcp->cksum = rte_ipv4_udptcp_cksum(ip, tcp);
    } else if (ip->next_proto_id == IPPROTO_UDP) {
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((char *)ip + ihl);
        udp->dgram_cksum = 0;
        udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);
    }

    ip->hdr_checksum = rte_ipv4_cksum(ip);
}
