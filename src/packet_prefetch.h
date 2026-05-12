#ifndef PACKET_PREFETCH_H
#define PACKET_PREFETCH_H

#include <rte_mbuf.h>
#include <rte_prefetch.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prefetch packet data for a burst of mbufs.
 *
 * Call this immediately after rte_eth_rx_burst() to hide memory latency.
 * We prefetch the L1 cache line where the Ethernet header starts;
 * by the time we actually parse the packet in the next loop iteration,
 * the data has hopefully already been loaded into cache.
 *
 * @param mbufs   Array of mbuf pointers received from RX.
 * @param nb_pkts Number of mbufs in the array.
 */
static inline void
packet_prefetch_burst(struct rte_mbuf **mbufs, uint16_t nb_pkts)
{
    /*
     * Walk the burst and issue a prefetch for each packet's data.
     * rte_prefetch0() is a hint to the CPU to bring the cache line
     * into L1.  It does not stall the pipeline.
     */
    for (uint16_t i = 0; i < nb_pkts; i++) {
        /* Prefetch the first cache line of the packet payload.
         * rte_pktmbuf_mtod() gives us the virtual address of the
         * start of the data (Ethernet header on RX). */
        rte_prefetch0(rte_pktmbuf_mtod(mbufs[i], void *));
    }
}

#ifdef __cplusplus
}
#endif

#endif /* PACKET_PREFETCH_H */
