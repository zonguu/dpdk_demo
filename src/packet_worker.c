#include "packet_worker.h"
#include "dpdk_init.h"
#include "stats.h"
#include "pcap_dump.h"
#include "acl_filter.h"
#include "token_bucket.h"
#include "packet_prefetch.h"
#include "port_mirror.h"
#include "flow_table.h"
#include "icmp_reply.h"
#include "tx_retry.h"

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

    /* Per-lcore flow table (rte_hash) for tracking 5-tuple flows */
    struct flow_table *ft = flow_table_create("flow_tbl_0", 0);
    if (ft) {
        printf("[WORKER] Flow table enabled (max %u entries).\n",
               FLOW_TABLE_MAX_ENTRIES);
    }

    /* Per-lcore token bucket: 100 Mbps default, 1 MTU burst */
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000ULL * 1000ULL, 0);

    printf("[WORKER] Starting loop on %u port(s)... Press Ctrl-C to stop.\n",
           nb_ports);
    printf("[WORKER] ACL + Token Bucket + Prefetch + Mirror + Flow Table + ICMP Reply enabled.\n");

    while (!force_quit) {
        for (port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
                continue;

            /* 1. Prefetch packet data before parsing/filtering */
            packet_prefetch_burst(bufs, nb_rx);

            /* 2. Record RX stats and optionally dump to pcap */
            stats_record_rx(port, bufs, nb_rx);
            pcap_dump_mbufs(bufs, nb_rx);

            /* 3. Port mirroring to the paired port */
            if (nb_ports >= 2) {
                uint16_t mirror_port = port ^ 1;
                port_mirror_send(mirror_port, bufs, nb_rx);
            }

            /*
             * 4. ICMP Echo Reply: if a packet is an ICMP Echo Request,
             *    reply to it directly and do NOT pass it down the normal
             *    TX path.  The mbuf is consumed by icmp_reply_send().
             */
            uint16_t nb_normal = 0;
            for (uint16_t i = 0; i < nb_rx; i++) {
                if (icmp_reply_send(bufs[i], port) == 0) {
                    /* Not an ICMP echo request; keep for normal processing */
                    bufs[nb_normal++] = bufs[i];
                }
                /* If icmp_reply_send() returned 1, the mbuf was already
                 * transmitted or freed, so we drop it from this burst. */
            }
            if (nb_normal == 0)
                continue;

            /* 5. Update flow table for all remaining packets */
            if (ft) {
                for (uint16_t i = 0; i < nb_normal; i++) {
                    flow_table_record(ft, bufs[i]);
                }
            }

            /* 6. ACL filter: drop packets matching hard-coded rules */
            uint16_t nb_acl = 0;
            for (uint16_t i = 0; i < nb_normal; i++) {
                if (acl_filter_evaluate(bufs[i])) {
                    bufs[nb_acl++] = bufs[i];
                } else {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
            if (nb_acl == 0)
                continue;

            /* 7. Token bucket rate limiter */
            uint16_t nb_allowed = token_bucket_apply(&tb, bufs, nb_acl);
            if (nb_allowed == 0)
                continue;

            for (uint16_t i = 0; i < nb_allowed; i++) {
                process_packet(bufs[i], port);
            }

            /* 8. Transmit with congestion retry instead of direct free */
            uint16_t nb_tx = tx_retry_burst(port, 0, bufs, nb_allowed);
            stats_record_tx(port, nb_tx, nb_allowed);
        }

        stats_print_periodic();

        /* 9. Print top flows every second (piggyback on stats timer) */
        if (ft) {
            flow_table_print_top(ft, 5);
        }
    }

    if (ft)
        flow_table_destroy(ft);

    printf("[WORKER] Stopped.\n");
}
