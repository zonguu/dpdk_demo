#include "icmp_reply.h"
#include "packet_modify.h"

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_icmp.h>
#include <rte_mbuf.h>

int
icmp_reply_send(struct rte_mbuf *m, uint16_t port)
{
    /*
     * Guard 1: minimum length check.
     * If we skipped this and received a runt frame (< 42 bytes),
     * the subsequent pointer arithmetic (eth + 1) could read past
     * the end of the allocated mbuf buffer, causing a segfault
     * or silent memory corruption.
     */
    if (unlikely(rte_pktmbuf_pkt_len(m) < sizeof(struct rte_ether_hdr) +
                 sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_icmp_hdr))) {
        rte_pktmbuf_free(m);
        return 0;
    }

    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);

    /*
     * Guard 2: EtherType must be IPv4.
     * Without this check, an ARP or IPv6 packet could reach the
     * ICMP parser.  We would interpret random bytes as an IPv4
     * header and produce a malformed reply, confusing the peer.
     */
    if (unlikely(eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))) {
        rte_pktmbuf_free(m);
        return 0;
    }

    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);

    /* Guard 3: IP next-protocol must be ICMP. */
    if (unlikely(ip->next_proto_id != IPPROTO_ICMP)) {
        rte_pktmbuf_free(m);
        return 0;
    }

    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    struct rte_icmp_hdr *icmp = (struct rte_icmp_hdr *)((char *)ip + ihl);

    /*
     * Guard 4: only reply to Echo Request (type = 8).
     * If we replied to an Echo Reply (type = 0) we would create
     * an infinite ping-pong between two hosts.
     */
    if (unlikely(icmp->icmp_type != RTE_IP_ICMP_ECHO_REQUEST)) {
        rte_pktmbuf_free(m);
        return 0;
    }

    /*
     * Step 1: Swap MAC addresses.
     * The reply must go back to the original sender.  If we forgot
     * this step, the Ethernet switch would drop the frame or deliver
     * it to the wrong host because the Dst MAC would still point to us.
     */
    swap_mac_addresses(m);

    /*
     * Step 2: Swap IP addresses.
     * Same reasoning as MAC: the reply must be routed back to the
     * source.  Without this the packet would boomerang to ourselves
     * or be dropped by the peer as an unexpected source IP.
     */
    packet_modify_swap_ip(m);

    /*
     * Step 3: Change ICMP type from Echo Request (8) to Echo Reply (0).
     * Code stays 0.  We MUST clear the checksum field to 0 before
     * recomputing; otherwise the old checksum value is included in
     * the calculation and the result is garbage.
     */
    icmp->icmp_type = RTE_IP_ICMP_ECHO_REPLY;
    icmp->icmp_cksum = 0;

    /*
     * Step 4: Recompute ICMP checksum.
     * rte_raw_cksum() computes the one's-complement sum of a
     * contiguous buffer.  We cover the entire ICMP payload
     * (header + data).  If we under-counted, the peer would
     * reject the reply as corrupted.
     */
    uint16_t icmp_len = rte_be_to_cpu_16(ip->total_length) - ihl;
    icmp->icmp_cksum = rte_raw_cksum(icmp, icmp_len);

    /*
     * RFC 792: checksum value 0x0000 is reserved (means "no checksum").
     * In one's complement arithmetic, 0x0000 and 0xFFFF are equivalent.
     * We must write 0xFFFF so the receiver knows a valid checksum is
     * present.  Without this fix, some strict stacks would discard
     * our reply.
     */
    if (icmp->icmp_cksum == 0)
        icmp->icmp_cksum = 0xFFFF;

    /*
     * Step 5: Recompute IPv4 header checksum.
     * We modified the IP addresses, which are part of the IP header
     * checksum scope.  The old checksum is now invalid.  If we sent
     * the packet with the stale checksum, the receiving IP layer
     * would drop it immediately.
     */
    ip->hdr_checksum = 0;
    ip->hdr_checksum = rte_ipv4_cksum(ip);

    /*
     * Step 6: Transmit.
     * We send on the SAME port the request arrived on.  In a simple
     * two-port setup this means the reply goes back out the interface
     * it came in, which is correct for a directly connected ping.
     */
    uint16_t nb_tx = rte_eth_tx_burst(port, 0, &m, 1);
    if (unlikely(nb_tx < 1))
        rte_pktmbuf_free(m);
    /*
     * NOTE: If nb_tx == 0 we free the mbuf here to prevent a leak.
     * We do NOT retry because ICMP replies are best-effort; stalling
     * the RX loop for a retry would hurt throughput.
     */

    return 1;
}
