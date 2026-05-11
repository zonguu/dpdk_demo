#include "port_mirror.h"

#include <rte_ethdev.h>
#include <rte_mbuf.h>

uint16_t
port_mirror_send(uint16_t mirror_port,
                 struct rte_mbuf **mbufs,
                 uint16_t nb_pkts)
{
    uint16_t tx_total = 0;

    for (uint16_t i = 0; i < nb_pkts; i++) {
        /*
         * rte_pktmbuf_clone() creates a new mbuf header that points to
         * the SAME data buffer as the original.  The data buffer's
         * refcnt is incremented, so the original mbuf can be freed or
         * sent independently of the clone.
         *
         * This is effectively a zero-copy mirror.
         */
        struct rte_mbuf *clone = rte_pktmbuf_clone(mbufs[i], mbufs[i]->pool);
        if (unlikely(clone == NULL)) {
            /* Mempool exhausted; skip this packet. */
            continue;
        }

        /*
         * Send the clone.  We burst one-by-one here for simplicity.
         * In production you would accumulate clones and burst them.
         */
        uint16_t nb_tx = rte_eth_tx_burst(mirror_port, 0, &clone, 1);
        if (unlikely(nb_tx < 1)) {
            /* Queue full; free the clone to avoid leak. */
            rte_pktmbuf_free(clone);
        } else {
            tx_total++;
        }
    }

    return tx_total;
}
