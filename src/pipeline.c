#include "pipeline.h"
#include "dpdk_init.h"
#include "packet_parser.h"
#include "packet_worker.h"
#include "stats.h"
#include "pcap_dump.h"
#include "acl_filter.h"
#include "token_bucket.h"
#include "packet_prefetch.h"
#include "rss_hash.h"
#include "tx_retry.h"

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

/* One ring per worker for software RSS dispatch */
static struct rte_ring *g_rss_rings[RSS_MAX_WORKERS];
static uint16_t         g_nb_workers;

/* Worker lcore main function */
static int worker_main(void *arg)
{
    uint16_t worker_id = (uint16_t)(uintptr_t)arg;
    struct rte_ring *my_ring = g_rss_rings[worker_id];
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint16_t port;

    printf("[PIPELINE] Worker %u started on lcore %u\n",
           worker_id, rte_lcore_id());
    fflush(stdout);

    stats_init();

    /* Per-lcore token bucket */
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000ULL * 1000ULL, 0);

    while (!force_quit) {
        unsigned int nb = rte_ring_dequeue_burst(my_ring, (void **)mbufs,
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

        /* ACL filter */
        unsigned int nb_acl = 0;
        for (unsigned int i = 0; i < nb; i++) {
            if (acl_filter_evaluate(mbufs[i])) {
                mbufs[nb_acl++] = mbufs[i];
            } else {
                rte_pktmbuf_free(mbufs[i]);
            }
        }

        /* Token bucket rate limiter */
        uint16_t nb_allowed = token_bucket_apply(&tb, mbufs, (uint16_t)nb_acl);

        /* Echo back to the source port with retry on congestion */
        for (uint16_t i = 0; i < nb_allowed; i++) {
            port = mbufs[i]->port;
            uint16_t nb_tx = tx_retry_burst(port, 0, &mbufs[i], 1);
            stats_record_tx(port, nb_tx, 1);
        }

        stats_print_periodic();
    }

    printf("[PIPELINE] Worker %u on lcore %u stopped.\n",
           worker_id, rte_lcore_id());
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

    uint16_t nb_lcores = rte_lcore_count();
    if (nb_lcores < 2) {
        fprintf(stderr, "[PIPELINE] Need at least 2 lcores.\n");
        return -1;
    }

    /*
     * Use all worker lcores.  Cap at RSS_MAX_WORKERS so we do not
     * exhaust the statically allocated ring array.
     */
    g_nb_workers = nb_lcores - 1;
    if (g_nb_workers > RSS_MAX_WORKERS)
        g_nb_workers = RSS_MAX_WORKERS;

    /* Create one ring per worker */
    for (uint16_t i = 0; i < g_nb_workers; i++) {
        char ring_name[RTE_RING_NAMESIZE];
        snprintf(ring_name, sizeof(ring_name), "rss_ring_%u", i);
        g_rss_rings[i] = rte_ring_create(ring_name, RING_SIZE, 0,
                                          RING_F_SP_ENQ | RING_F_SC_DEQ);
        if (!g_rss_rings[i]) {
            fprintf(stderr, "[PIPELINE] Failed to create %s: %s\n",
                    ring_name, rte_strerror(rte_errno));
            return -1;
        }
    }

    /* Launch workers */
    uint16_t worker_id = 0;
    unsigned int lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (worker_id >= g_nb_workers)
            break;
        int ret = rte_eal_remote_launch(worker_main,
                                        (void *)(uintptr_t)worker_id,
                                        lcore_id);
        if (ret != 0) {
            fprintf(stderr, "[PIPELINE] Failed to launch worker %u: %d\n",
                    worker_id, ret);
            return -1;
        }
        worker_id++;
    }

    printf("[PIPELINE] Master on lcore %u, %u worker(s), %u port(s)\n",
           rte_lcore_id(), g_nb_workers, nb_ports);
    printf("[PIPELINE] Software RSS enabled: 5-tuple hash -> worker ring\n");
    fflush(stdout);

    /*
     * Master loop: RX from all ports -> software RSS -> enqueue to
     * the appropriate worker ring.
     */
    while (!force_quit) {
        for (uint16_t port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            struct rte_mbuf *mbufs[BURST_SIZE];
            uint16_t nb_rx = rte_eth_rx_burst(port, 0, mbufs, BURST_SIZE);
            if (nb_rx == 0)
                continue;

            /* Prefetch packet data to hide cache-miss latency */
            packet_prefetch_burst(mbufs, nb_rx);

            /*
             * Sort received packets into per-worker buckets.
             * We do this in software so that the same 5-tuple always
             * lands on the same worker, preserving flow order.
             */
            struct rte_mbuf *worker_bufs[RSS_MAX_WORKERS][BURST_SIZE];
            uint16_t worker_cnts[RSS_MAX_WORKERS] = {0};

            for (uint16_t i = 0; i < nb_rx; i++) {
                mbufs[i]->port = port;

                uint32_t hash = rss_hash_packet(mbufs[i]);
                uint16_t w = rss_select_worker(hash, g_nb_workers);

                /*
                 * If the packet is not IPv4 (or parsing failed) hash
                 * returns 0, which maps to worker 0.  This is a
                 * reasonable fallback.
                 */
                worker_bufs[w][worker_cnts[w]++] = mbufs[i];
            }

            /* Now burst enqueue each worker's bucket into its ring */
            for (uint16_t w = 0; w < g_nb_workers; w++) {
                if (worker_cnts[w] == 0)
                    continue;

                unsigned int enq = rte_ring_enqueue_burst(
                    g_rss_rings[w],
                    (void **)worker_bufs[w],
                    worker_cnts[w], NULL);

                /* Free mbufs that did not fit into the ring */
                if (unlikely(enq < worker_cnts[w])) {
                    for (uint16_t i = enq; i < worker_cnts[w]; i++) {
                        rte_pktmbuf_free(worker_bufs[w][i]);
                    }
                }
            }
        }
    }

    /* Wait for all workers to finish */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_wait_lcore(lcore_id);
    }
    printf("[PIPELINE] Master stopped.\n");
    fflush(stdout);
    return 0;
}
