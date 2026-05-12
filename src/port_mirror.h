#ifndef PORT_MIRROR_H
#define PORT_MIRROR_H

#include <stdint.h>
#include <rte_mbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send a clone of each packet to a mirror port.
 *
 * The original mbufs are **not** modified or freed; ownership remains
 * with the caller.  Clones share the underlying data buffer via
 * refcnt, so this is a zero-copy mirror (unless the caller later
 * modifies the packet, in which case a true copy is required).
 *
 * @param mirror_port  Destination port for the cloned packets.
 * @param mbufs        Array of mbuf pointers to mirror.
 * @param nb_pkts      Number of mbufs.
 * @return             Number of clones successfully transmitted.
 */
uint16_t port_mirror_send(uint16_t mirror_port,
                          struct rte_mbuf **mbufs,
                          uint16_t nb_pkts);

#ifdef __cplusplus
}
#endif

#endif /* PORT_MIRROR_H */
