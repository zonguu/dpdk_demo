#ifndef TX_RETRY_H
#define TX_RETRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <rte_mbuf.h>

/**
 * @brief Transmit a burst of mbufs with limited retries on congestion.
 *
 * rte_eth_tx_burst() may return fewer packets than requested when the
 * TX ring is full.  Instead of immediately freeing the unsent mbufs,
 * we retry up to TX_RETRY_MAX times with a tiny backoff.  This gives
 * the NIC a chance to drain descriptors without stalling the pipeline.
 *
 * @param port_id    Destination Ethernet port.
 * @param queue_id   TX queue id.
 * @param tx_pkts    Array of mbufs to send.
 * @param nb_pkts    Number of mbufs in the array.
 * @return           Total number of packets successfully transmitted.
 */
uint16_t tx_retry_burst(uint16_t port_id, uint16_t queue_id,
                        struct rte_mbuf **tx_pkts, uint16_t nb_pkts);

#ifdef __cplusplus
}
#endif

#endif /* TX_RETRY_H */
