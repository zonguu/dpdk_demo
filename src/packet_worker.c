#include "packet_worker.h"
#include "dpdk_init.h"
#include "stats.h"

#include <stdio.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

volatile int force_quit = 0;

void signal_handler(int sig)
{
    (void)sig;
    printf("\n[SIGNAL] Caught signal, stopping...\n");
    fflush(stdout);
    force_quit = 1;
}

static inline void
process_packet(struct rte_mbuf *mbuf, uint16_t port_id)
{
    /* Placeholder for per-packet processing.
     * For demo purposes we do nothing here; stats and parser
     * are handled inside stats_record_rx(). */
    (void)mbuf;
    (void)port_id;
}

void packet_loop(void)
{
    uint16_t port;
    uint16_t nb_ports = get_port_count();

    if (nb_ports == 0) {
        printf("[WORKER] No available ports to process.\n");
        return;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    stats_init();

    printf("[WORKER] Starting loop on %u port(s)... Press Ctrl-C to stop.\n",
           nb_ports);

    while (!force_quit) {
        for (port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
                continue;

            stats_record_rx(port, bufs, nb_rx);

            for (uint16_t i = 0; i < nb_rx; i++) {
                process_packet(bufs[i], port);
            }

            const uint16_t nb_tx = rte_eth_tx_burst(port, 0, bufs, nb_rx);
            stats_record_tx(port, nb_tx, nb_rx);

            if (unlikely(nb_tx < nb_rx)) {
                for (uint16_t i = nb_tx; i < nb_rx; i++) {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
        }

        stats_print_periodic();
    }

    printf("[WORKER] Stopped.\n");
}
