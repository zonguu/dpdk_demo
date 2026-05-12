#ifndef ICMP_REPLY_H
#define ICMP_REPLY_H

#include <rte_mbuf.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert an ICMP Echo Request into an Echo Reply and send it.
 *
 * The mbuf is modified in-place:
 *   - MAC addresses are swapped
 *   - IP addresses are swapped
 *   - ICMP type is changed from 8 (Echo Request) to 0 (Echo Reply)
 *   - IP and ICMP checksums are recalculated
 *
 * If the packet is not a valid ICMP Echo Request, it is freed.
 *
 * @param m     Received mbuf (Ethernet + IPv4 + ICMP).
 * @param port  Port on which the packet was received.
 * @return      1 if a reply was sent, 0 otherwise.
 */
int icmp_reply_send(struct rte_mbuf *m, uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* ICMP_REPLY_H */
