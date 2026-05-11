#include "tx_retry.h"

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

/* Maximum retry attempts before giving up and dropping */
#define TX_RETRY_MAX    3

/* Backoff between retries: ~1 us expressed in timer cycles */
static inline uint64_t
tx_retry_backoff_cycles(void)
{
    return rte_get_timer_hz() / 1000000; /* 1 microsecond */
}

uint16_t
tx_retry_burst(uint16_t port_id, uint16_t queue_id,
               struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
    uint16_t total_sent = 0;
    uint16_t retries    = 0;

    while (nb_pkts > 0 && retries <= TX_RETRY_MAX) {
        uint16_t sent = rte_eth_tx_burst(port_id, queue_id, tx_pkts, nb_pkts);

        total_sent += sent;
        tx_pkts    += sent;   /* advance pointer past sent packets */
        nb_pkts    -= sent;

        if (nb_pkts == 0)
            break;

        /*
         * The TX ring is full (or nearly full).  Some packets remain.
         *
         * Why retry instead of dropping immediately?
         *   - In burst traffic the ring often drains within a few
         *     microseconds because the NIC DMA engine is continuously
         *     pulling descriptors.
         *   - Dropping immediately would cause unnecessary packet loss
         *     under transient congestion.
         *
         * Why NOT retry forever?
         *   - If the link is down or the peer is not draining, we
         *     would spin here forever, starving the RX loop and
         *     dropping incoming packets instead.
         *   - A bounded retry (3 attempts ~= 3 µs) gives the NIC a
         *     chance to catch up without risking livelock.
         *
         * Why busy-spin instead of sleep/yield?
         *   - DPDK is a poll-mode driver; calling nanosleep() or
         *     sched_yield() would trigger a context switch costing
         *     1-10 µs, which is far longer than the typical ring-drain
         *     time (~100-500 ns).  The busy-spin keeps us on the
         *     same CPU core and preserves cache warmth.
         */
        retries++;
        if (retries <= TX_RETRY_MAX) {
            uint64_t start = rte_get_timer_cycles();
            while ((rte_get_timer_cycles() - start) < tx_retry_backoff_cycles()) {
                /* Intentionally empty: poll on the TSC register. */
            }
        }
    }

    /*
     * Free any packets that survived all retries.
     * If we forgot this step, the unsent mbufs would leak,
     * eventually exhausting the mempool and causing RX to fail
     * with ENOBUFS.
     */
    for (uint16_t i = 0; i < nb_pkts; i++) {
        rte_pktmbuf_free(tx_pkts[i]);
    }

    return total_sent;
}
