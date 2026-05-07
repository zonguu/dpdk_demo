#include "dpdk_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

int eal_init(int argc, char **argv)
{
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init: %d\n", ret);
    }

    if (rte_lcore_count() < 1) {
        rte_exit(EXIT_FAILURE, "No available lcores.\n");
    }

    printf("[EAL] Initialized.\n");
    printf("[EAL]   Lcores : %u\n", rte_lcore_count());
    printf("[EAL]   Sockets: %u\n", rte_socket_count());

    /* Print per-lcore socket binding */
    unsigned int lcore_id;
    RTE_LCORE_FOREACH(lcore_id) {
        printf("[EAL]   Lcore %u -> Socket %d\n",
               lcore_id, rte_lcore_to_socket_id(lcore_id));
    }
    return ret;
}

struct rte_mempool *mbuf_pool_create(uint16_t socket_id)
{
    char pool_name[RTE_MEMPOOL_NAMESIZE];
    snprintf(pool_name, sizeof(pool_name), "mbuf_pool_%u", socket_id);

    struct rte_mempool *pool = rte_pktmbuf_pool_create(
        pool_name,
        NUM_MBUFS,
        MBUF_CACHE_SIZE,
        0,                                 /* private data size */
        RTE_MBUF_DEFAULT_BUF_SIZE,
        socket_id);

    if (pool == NULL) {
        fprintf(stderr, "Cannot create mbuf pool on socket %u: %s\n",
                socket_id, rte_strerror(rte_errno));
    } else {
        printf("[MEMPOOL] Created '%s' on socket %u with %u mbufs\n",
               pool_name, socket_id, NUM_MBUFS);
        /* Dump detailed mempool statistics */
        rte_mempool_dump(stdout, pool);
    }

    return pool;
}

static inline int
port_configure(uint16_t port_id, struct rte_mempool *mbuf_pool)
{
    const uint16_t rx_rings = NUM_RX_QUEUES;
    const uint16_t tx_rings = NUM_TX_QUEUES;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_conf port_conf = {0};
    struct rte_eth_rxconf rxconf = {0};
    struct rte_eth_txconf txconf = {0};

    /* Get device info to know driver capabilities */
    retval = rte_eth_dev_info_get(port_id, &dev_info);
    if (retval != 0) {
        fprintf(stderr, "Error getting dev info for port %u: %s\n",
                port_id, strerror(-retval));
        return retval;
    }

    printf("[PORT %u] Driver: %s\n",
           port_id, dev_info.driver_name);

    /* Configure RX/TX queue limits if needed */
    if (nb_rxd > dev_info.rx_desc_lim.nb_max ||
        nb_rxd < dev_info.rx_desc_lim.nb_min) {
        nb_rxd = dev_info.rx_desc_lim.nb_max;
    }
    if (nb_txd > dev_info.tx_desc_lim.nb_max ||
        nb_txd < dev_info.tx_desc_lim.nb_min) {
        nb_txd = dev_info.tx_desc_lim.nb_max;
    }

    /* Configure the Ethernet device */
    retval = rte_eth_dev_configure(port_id, rx_rings, tx_rings, &port_conf);
    if (retval != 0) {
        fprintf(stderr, "Port %u configuration error: %s\n",
                port_id, strerror(-retval));
        return retval;
    }

    /* Adjust descriptor numbers */
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd);
    if (retval != 0) {
        fprintf(stderr, "Port %u adjust nb desc error: %s\n",
                port_id, strerror(-retval));
        return retval;
    }

    /* Setup RX queue */
    rxconf = dev_info.default_rxconf;
    rxconf.offloads = port_conf.rxmode.offloads;
    retval = rte_eth_rx_queue_setup(
        port_id, 0, nb_rxd,
        rte_eth_dev_socket_id(port_id),
        &rxconf,
        mbuf_pool);
    if (retval < 0) {
        fprintf(stderr, "Port %u RX queue setup error: %s\n",
                port_id, strerror(-retval));
        return retval;
    }

    /* Setup TX queue */
    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    retval = rte_eth_tx_queue_setup(
        port_id, 0, nb_txd,
        rte_eth_dev_socket_id(port_id),
        &txconf);
    if (retval < 0) {
        fprintf(stderr, "Port %u TX queue setup error: %s\n",
                port_id, strerror(-retval));
        return retval;
    }

    /* Start the device */
    retval = rte_eth_dev_start(port_id);
    if (retval < 0) {
        fprintf(stderr, "Port %u start error: %s\n",
                port_id, strerror(-retval));
        return retval;
    }

    /* Enable promiscuous mode (optional, useful for pcap) */
    retval = rte_eth_promiscuous_enable(port_id);
    if (retval != 0) {
        fprintf(stderr, "Port %u promiscuous enable warning: %s\n",
                port_id, strerror(-retval));
    }

    printf("[PORT %u] Started (RX:%u TX:%u desc, socket:%d)\n",
           port_id, nb_rxd, nb_txd, rte_eth_dev_socket_id(port_id));
    return 0;
}

int port_init(uint16_t port_id, struct rte_mempool *mbuf_pool)
{
    return port_configure(port_id, mbuf_pool);
}

uint16_t get_port_count(void)
{
    return rte_eth_dev_count_avail();
}
