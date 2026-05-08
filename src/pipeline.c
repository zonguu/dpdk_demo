#include "pipeline.h"
#include "dpdk_init.h"
#include "packet_parser.h"
#include "packet_worker.h"
#include "stats.h"
#include "pcap_dump.h"

#include <stdio.h>
#include <string.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ring.h>

#define RING_SIZE   4096
#define BURST_SIZE  32

static struct rte_ring *g_rx_ring;

/* Worker lcore main function */
static int worker_main(void *arg)
{
    (void)arg;
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint16_t port;

    printf("[PIPELINE] Worker started on lcore %u\n", rte_lcore_id());
    fflush(stdout);

    stats_init();

    while (!force_quit) {
        unsigned int nb = rte_ring_dequeue_burst(g_rx_ring, (void **)mbufs,
                                                  BURST_SIZE, NULL);
        if (nb == 0) {
            /* Optional: reduce busy-spin by yielding or sleeping */
            continue;
        }

        /* Process each packet: stats + dump + echo TX */
        for (unsigned int i = 0; i < nb; i++) {
            port = mbufs[i]->port;
            stats_record_rx(port, &mbufs[i], 1);
        }
        pcap_dump_mbufs(mbufs, nb);

        /* Simple echo: send back to the source port.
         * In a real app you would map src_port -> dst_port.
         * Here we burst per-port to respect rte_eth_tx_burst(). */
        for (unsigned int i = 0; i < nb; i++) {
            port = mbufs[i]->port;
            uint16_t nb_tx = rte_eth_tx_burst(port, 0, &mbufs[i], 1);
            stats_record_tx(port, nb_tx, 1);
            if (unlikely(nb_tx < 1)) {
                rte_pktmbuf_free(mbufs[i]);
            }
        }

        stats_print_periodic();
    }

    printf("[PIPELINE] Worker on lcore %u stopped.\n", rte_lcore_id());
    fflush(stdout);
    return 0;
}

int pipeline_run(void)
{
    uint16_t nb_ports = get_port_count();
    if (nb_ports == 0) {
        fprintf(stderr, "[PIPELINE] No ports available.\n");
        return -1;
    }

    if (rte_lcore_count() < 2) {
        fprintf(stderr, "[PIPELINE] Need at least 2 lcores, fallback to single-core mode.\n");
        return -1;
    }

    /* Create ring on socket 0 (simplified for demo) */
    g_rx_ring = rte_ring_create("rx_ring", RING_SIZE, 0,
                                RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (!g_rx_ring) {
        fprintf(stderr, "[PIPELINE] Failed to create rx_ring: %s\n",
                rte_strerror(rte_errno));
        return -1;
    }

    /* Launch worker on the next lcore */
    unsigned int worker_lcore = rte_get_next_lcore(-1, 1, 0);
    if (worker_lcore == RTE_MAX_LCORE) {
        fprintf(stderr, "[PIPELINE] No worker lcore found.\n");
        return -1;
    }

    int ret = rte_eal_remote_launch(worker_main, NULL, worker_lcore);
    if (ret != 0) {
        fprintf(stderr, "[PIPELINE] Failed to launch worker: %d\n", ret);
        return -1;
    }

    printf("[PIPELINE] Master on lcore %u, worker on lcore %u, %u port(s)\n",
           rte_lcore_id(), worker_lcore, nb_ports);
    fflush(stdout);

    /* Master loop: RX from all ports -> enqueue to ring */
    while (!force_quit) {
        for (uint16_t port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            struct rte_mbuf *mbufs[BURST_SIZE];
            uint16_t nb_rx = rte_eth_rx_burst(port, 0, mbufs, BURST_SIZE);
            if (nb_rx == 0)
                continue;

            /* Save source port in mbuf metadata */
            for (uint16_t i = 0; i < nb_rx; i++) {
                mbufs[i]->port = port;
            }

            unsigned int enq = rte_ring_enqueue_burst(g_rx_ring,
                                                        (void **)mbufs,
                                                        nb_rx, NULL);
            /* Free mbufs that didn't fit into the ring */
            if (unlikely(enq < nb_rx)) {
                for (uint16_t i = enq; i < nb_rx; i++) {
                    rte_pktmbuf_free(mbufs[i]);
                }
            }
        }
    }

    /* Wait for worker to finish */
    rte_eal_wait_lcore(worker_lcore);
    printf("[PIPELINE] Master stopped.\n");
    fflush(stdout);
    return 0;
}
