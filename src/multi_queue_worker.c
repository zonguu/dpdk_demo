#include "multi_queue_worker.h"
#include "dpdk_init.h"
#include "packet_worker.h"
#include "stats.h"
#include "pcap_dump.h"

#include <stdio.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

/* Each lcore processes ports where port_id % num_lcores == lcore_id */
static int lcore_main(void *arg)
{
    (void)arg;
    uint16_t lcore_id = rte_lcore_id();
    uint16_t nb_ports = get_port_count();
    uint16_t nb_lcores = rte_lcore_count();
    uint16_t is_master = (lcore_id == rte_get_main_lcore());

    printf("[LCORE %u] Starting, handling ports: ", lcore_id);
    fflush(stdout);
    int first = 1;
    for (uint16_t p = 0; p < nb_ports && p < MAX_PORTS; p++) {
        if (p % nb_lcores == lcore_id) {
            printf("%s%u", first ? "" : ",", p);
            first = 0;
        }
    }
    printf(first ? "(none)\n" : "\n");
    fflush(stdout);

    while (!force_quit) {
        for (uint16_t port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            if (port % nb_lcores != lcore_id)
                continue;

            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
                continue;

            stats_record_rx(port, bufs, nb_rx);
            pcap_dump_mbufs(bufs, nb_rx);

            const uint16_t nb_tx = rte_eth_tx_burst(port, 0, bufs, nb_rx);
            stats_record_tx(port, nb_tx, nb_rx);

            if (unlikely(nb_tx < nb_rx)) {
                for (uint16_t i = nb_tx; i < nb_rx; i++) {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
        }

        /* Only master prints periodic stats to avoid interleaved output */
        if (is_master)
            stats_print_periodic();
    }

    printf("[LCORE %u] Stopped.\n", lcore_id);
    fflush(stdout);
    return 0;
}

void multi_lcore_loop(void)
{
    uint16_t lcore_id;

    /* Initialize stats before any lcore starts processing */
    stats_init();

    /* Launch workers (all lcores except master) */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_remote_launch(lcore_main, NULL, lcore_id);
    }

    /* Master also runs the polling loop */
    lcore_main(NULL);

    /* Wait for workers to finish */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_wait_lcore(lcore_id);
    }

    printf("[MULTICORE] All lcores stopped.\n");
}
