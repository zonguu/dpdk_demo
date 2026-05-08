#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <rte_memzone.h>

#include <signal.h>

#include "dpdk_init.h"
#include "packet_worker.h"

#ifdef ENABLE_PIPELINE
#include "pipeline.h"
#endif
#ifdef ENABLE_MULTICORE
#include "multi_queue_worker.h"
#endif
#ifdef ENABLE_L2FWD
#include "l2fwd_worker.h"
#endif

/* Print system hugepage info via /proc/meminfo */
static void print_system_memory_info(void)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    printf("[SYSTEM] /proc/meminfo hugepage info:\n");
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Huge", 4) == 0 ||
            strncmp(line, "AnonHuge", 8) == 0) {
            printf("[SYSTEM]   %s", line);
        }
    }
    fclose(fp);
}

/* Print DPDK memory usage summary */
static void print_dpdk_memory_info(void)
{
    printf("[DPDK-MEM] Malloc statistics:\n");
    rte_malloc_dump_stats(stdout, NULL);
}

/*
 * This demo initializes DPDK EAL, creates mbuf pools on each NUMA socket,
 * configures available ports (including net_pcap vdevs), and runs a simple
 * RX/TX packet loop.
 *
 * Example usage with net_pcap on loopback:
 *   sudo ./dpdk_pcap_demo -l 0 \
 *       --vdev=net_pcap0,iface=lo \
 *       --vdev=net_pcap1,rx_pcap=input.pcap,tx_pcap=output.pcap
 */

int main(int argc, char **argv)
{
    int ret;
    uint16_t nb_ports;
    struct rte_mempool *mbuf_pools[RTE_MAX_NUMA_NODES] = {NULL};

    /* Register signal handlers early so both single-core and pipeline modes
     * can catch SIGINT/SIGTERM for graceful shutdown. */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 1. Initialize EAL (hugepages, lcores, PCI/vdev probes, etc.) */
    ret = eal_init(argc, argv);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    argc -= ret;
    argv += ret;

    /* 2. Detect available ports (vdevs like net_pcap appear here) */
    nb_ports = get_port_count();
    if (nb_ports == 0) {
        rte_exit(EXIT_FAILURE,
                 "No Ethernet ports detected. "
                 "Try adding --vdev=net_pcap0,iface=lo\n");
    }
    printf("[MAIN] Detected %u port(s)\n", nb_ports);

    /* 3. Create mbuf pools on each socket that has a port */
    for (uint16_t port = 0; port < nb_ports; port++) {
        int socket_id = rte_eth_dev_socket_id(port);
        if (socket_id < 0 || socket_id >= RTE_MAX_NUMA_NODES) {
            socket_id = 0; /* Fallback to socket 0 */
        }

        if (mbuf_pools[socket_id] == NULL) {
            mbuf_pools[socket_id] = mbuf_pool_create((uint16_t)socket_id);
            if (mbuf_pools[socket_id] == NULL) {
                rte_exit(EXIT_FAILURE,
                         "Cannot create mbuf pool on socket %d\n", socket_id);
            }
        }
    }

    /* 4. Initialize each port: configure, setup RX/TX queues, start */
    for (uint16_t port = 0; port < nb_ports; port++) {
        int socket_id = rte_eth_dev_socket_id(port);
        if (socket_id < 0 || socket_id >= RTE_MAX_NUMA_NODES) {
            socket_id = 0;
        }

        ret = port_init(port, mbuf_pools[socket_id]);
        if (ret != 0) {
            rte_exit(EXIT_FAILURE,
                     "Cannot init port %u (socket %d)\n", port, socket_id);
        }
    }

    /* 5. Print memory info after initialization */
    print_system_memory_info();
    print_dpdk_memory_info();

    /* Print mempool availability summary */
    printf("[MEMPOOL] Availability summary:\n");
    for (int s = 0; s < RTE_MAX_NUMA_NODES; s++) {
        if (mbuf_pools[s]) {
            printf("[MEMPOOL]   Socket %d '%s': avail=%u in-use=%u\n",
                   s, mbuf_pools[s]->name,
                   rte_mempool_avail_count(mbuf_pools[s]),
                   rte_mempool_in_use_count(mbuf_pools[s]));
        }
    }

    /* 6. Check port link status (informative, net_pcap may not have real link) */
    for (uint16_t port = 0; port < nb_ports; port++) {
        struct rte_eth_link link;
        ret = rte_eth_link_get_nowait(port, &link);
        if (ret < 0) {
            printf("[MAIN] Port %u link get error\n", port);
            continue;
        }
        printf("[MAIN] Port %u link: speed=%u duplex=%s status=%s\n",
               port,
               link.link_speed,
               (link.link_duplex == RTE_ETH_LINK_FULL_DUPLEX) ? "full" : "half",
               (link.link_status == RTE_ETH_LINK_UP) ? "UP" : "DOWN");
    }

    /* 7. Run packet processing loop (RX -> process -> TX) */
#if defined(ENABLE_L2FWD)
    printf("[MAIN] Running L2 forwarding mode.\n");
    l2fwd_loop();
#elif defined(ENABLE_MULTICORE)
    printf("[MAIN] Running multi-lcore mode.\n");
    multi_lcore_loop();
#elif defined(ENABLE_PIPELINE)
    if (rte_lcore_count() >= 2) {
        printf("[MAIN] Running in pipeline mode (multi-lcore)\n");
        ret = pipeline_run();
        if (ret != 0) {
            printf("[MAIN] Pipeline failed, fallback to single-core mode.\n");
            packet_loop();
        }
    } else {
        printf("[MAIN] Only 1 lcore, running single-core mode.\n");
        packet_loop();
    }
#else
    printf("[MAIN] Running single-core mode.\n");
    packet_loop();
#endif

    /* 8. Cleanup: stop and close ports */
    printf("[MAIN] Closing ports...\n");
    for (uint16_t port = 0; port < nb_ports; port++) {
        rte_eth_dev_stop(port);
        rte_eth_dev_close(port);
    }

    /* 8. EAL cleanup handled by rte_eal_cleanup() on process exit */
    return 0;
}
