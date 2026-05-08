#include "l2fwd_worker.h"
#include "dpdk_init.h"
#include "packet_worker.h"
#include "stats.h"

#include <stdio.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>

static inline void
swap_mac_addresses(struct rte_mbuf *mbuf)
{
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    struct rte_ether_addr tmp;

    rte_ether_addr_copy(&eth_hdr->src_addr, &tmp);
    rte_ether_addr_copy(&eth_hdr->dst_addr, &eth_hdr->src_addr);
    rte_ether_addr_copy(&tmp, &eth_hdr->dst_addr);
}

static inline uint16_t
get_dst_port(uint16_t src_port, uint16_t nb_ports)
{
    /* Simple pairing: 0<->1, 2<->3, ... */
    if (nb_ports == 1)
        return 0;
    return (src_port ^ 1) % nb_ports;
}

void l2fwd_loop(void)
{
    uint16_t port;
    uint16_t nb_ports = get_port_count();

    if (nb_ports == 0) {
        printf("[L2FWD] No available ports.\n");
        return;
    }

    if (nb_ports < 2) {
        printf("[L2FWD] Warning: only 1 port, forwarding will echo back.\n");
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    stats_init();

    printf("[L2FWD] Starting L2 forwarding on %u port(s)... Press Ctrl-C to stop.\n",
           nb_ports);
    printf("[L2FWD] Port pairing:\n");
    for (port = 0; port < nb_ports; port++) {
        printf("[L2FWD]   Port %u -> Port %u\n", port, get_dst_port(port, nb_ports));
    }

    while (!force_quit) {
        for (port = 0; port < nb_ports && port < MAX_PORTS; port++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
                continue;

            stats_record_rx(port, bufs, nb_rx);

            /* Swap MAC addresses for true L2 forwarding */
            for (uint16_t i = 0; i < nb_rx; i++) {
                swap_mac_addresses(bufs[i]);
            }

            uint16_t dst_port = get_dst_port(port, nb_ports);
            const uint16_t nb_tx = rte_eth_tx_burst(dst_port, 0, bufs, nb_rx);
            stats_record_tx(dst_port, nb_tx, nb_rx);

            /* Free unsent packets */
            if (unlikely(nb_tx < nb_rx)) {
                for (uint16_t i = nb_tx; i < nb_rx; i++) {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
        }

        stats_print_periodic();
    }

    printf("[L2FWD] Stopped.\n");
}
