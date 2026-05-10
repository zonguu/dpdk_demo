#ifndef PACKET_MODIFY_H
#define PACKET_MODIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <rte_mbuf.h>

/*
 * Swap source/destination MAC addresses.
 * Defined in packet_modify.c.
 */
void swap_mac_addresses(struct rte_mbuf *mbuf);

/*
 * Swap source/destination IPv4 addresses.
 * mbuf must contain a valid Ethernet + IPv4 header.
 */
void packet_modify_swap_ip(struct rte_mbuf *m);

/*
 * Swap source/destination L4 port (TCP or UDP).
 * mbuf must contain a valid Ethernet + IPv4 + L4 header.
 */
void packet_modify_swap_port(struct rte_mbuf *m);

/*
 * Recalculate IPv4 header checksum and TCP/UDP checksum.
 */
void packet_modify_recalc_checksum(struct rte_mbuf *m);

/*
 * Convenience: swap MAC, swap IP, swap port, recalc checksum.
 * Typical usage for a symmetric forwarding path.
 */
static inline void
packet_modify_swap_all(struct rte_mbuf *m)
{
    swap_mac_addresses(m);
    packet_modify_swap_ip(m);
    packet_modify_swap_port(m);
    packet_modify_recalc_checksum(m);
}

#ifdef __cplusplus
}
#endif

#endif /* PACKET_MODIFY_H */
