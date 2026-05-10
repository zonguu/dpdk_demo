#include "packet_worker.h"
#include "dpdk_init.h"
#include "stats.h"
#include "pcap_dump.h"
#include "acl_filter.h"
#include "token_bucket.h"

#include <stdio.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>

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

    /* Per-lcore token bucket: 100 Mbps default, 1 MTU burst */
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000ULL * 1000ULL, 0);

    printf("[WORKER] Starting loop on %u port(s)... Press Ctrl-C to stop.\n",
           nb_ports);
    printf("[WORKER] ACL + Token Bucket (100 Mbps) enabled in single-core mode.\n");

    while (!force_quit) {
        for (port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
                continue;

            stats_record_rx(port, bufs, nb_rx);
            pcap_dump_mbufs(bufs, nb_rx);

            /* ACL filter: drop packets matching hard-coded rules */
            uint16_t nb_acl = 0;
            for (uint16_t i = 0; i < nb_rx; i++) {
                if (acl_filter_evaluate(bufs[i])) {
                    bufs[nb_acl++] = bufs[i];
                } else {
                    rte_pktmbuf_free(bufs[i]);
                }
            }

            if (nb_acl == 0)
                continue;

            /* Token bucket rate limiter */
            uint16_t nb_allowed = token_bucket_apply(&tb, bufs, nb_acl);
            if (nb_allowed == 0)
                continue;

            for (uint16_t i = 0; i < nb_allowed; i++) {
                process_packet(bufs[i], port);
            }

            const uint16_t nb_tx = rte_eth_tx_burst(port, 0, bufs, nb_allowed);
            stats_record_tx(port, nb_tx, nb_allowed);

            if (unlikely(nb_tx < nb_allowed)) {
                for (uint16_t i = nb_tx; i < nb_allowed; i++) {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
        }

        stats_print_periodic();
    }

    printf("[WORKER] Stopped.\n");
}
