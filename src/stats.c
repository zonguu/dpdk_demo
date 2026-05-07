#include "stats.h"
#include "packet_parser.h"

#include <stdio.h>
#include <string.h>

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>

static port_stats_t g_stats[STATS_MAX_PORTS];
static port_stats_t g_last_stats[STATS_MAX_PORTS];
static uint64_t g_last_time;
static uint64_t g_timer_hz;
static uint16_t g_nb_ports;

void stats_init(void)
{
    memset(g_stats, 0, sizeof(g_stats));
    memset(g_last_stats, 0, sizeof(g_last_stats));
    g_timer_hz = rte_get_timer_hz();
    g_last_time = rte_get_timer_cycles();
    g_nb_ports = rte_eth_dev_count_avail();
    if (g_nb_ports > STATS_MAX_PORTS)
        g_nb_ports = STATS_MAX_PORTS;
}

void stats_record_rx(uint16_t port_id, struct rte_mbuf **mbufs, uint16_t nb_rx)
{
    if (port_id >= STATS_MAX_PORTS)
        return;

    for (uint16_t i = 0; i < nb_rx; i++) {
        uint64_t len = rte_pktmbuf_pkt_len(mbufs[i]);
        g_stats[port_id].rx_pkts++;
        g_stats[port_id].rx_bytes += len;

        packet_info_t info;
        if (packet_parse(mbufs[i], &info) == 0 && info.is_ipv4) {
            g_stats[port_id].ipv4_pkts++;
            if (info.is_tcp)
                g_stats[port_id].tcp_pkts++;
            else if (info.is_udp)
                g_stats[port_id].udp_pkts++;
            else if (info.is_icmp)
                g_stats[port_id].icmp_pkts++;
        } else {
            g_stats[port_id].other_pkts++;
        }
    }
}

void stats_record_tx(uint16_t port_id, uint16_t nb_tx, uint16_t nb_total)
{
    if (port_id >= STATS_MAX_PORTS)
        return;

    /* For TX bytes we don't have per-packet lengths after tx_burst,
     * so we approximate using the total count. For accurate TX bytes,
     * record before calling tx_burst. This demo uses pkt count only.
     */
    (void)nb_total;
    g_stats[port_id].tx_pkts += nb_tx;
    /* bytes are skipped here for simplicity; in production you would
     * sum lengths before tx_burst. */
}

void stats_print_periodic(void)
{
    uint64_t now = rte_get_timer_cycles();
    if (likely((now - g_last_time) < g_timer_hz))
        return;

    uint64_t total_rx_pps = 0;
    uint64_t total_tx_pps = 0;
    uint64_t total_rx_bps = 0;

    printf("\n========== STATS (last 1s) ==========\n");
    for (uint16_t p = 0; p < g_nb_ports; p++) {
        uint64_t rx_pps = g_stats[p].rx_pkts - g_last_stats[p].rx_pkts;
        uint64_t tx_pps = g_stats[p].tx_pkts - g_last_stats[p].tx_pkts;
        uint64_t rx_bps = (g_stats[p].rx_bytes - g_last_stats[p].rx_bytes) * 8;

        total_rx_pps += rx_pps;
        total_tx_pps += tx_pps;
        total_rx_bps += rx_bps;

        if (rx_pps > 0 || tx_pps > 0) {
            printf("Port %u  RX: %8lu pps  TX: %8lu pps  "
                   "IPv4=%lu TCP=%lu UDP=%lu ICMP=%lu Other=%lu\n",
                   p,
                   (unsigned long)rx_pps,
                   (unsigned long)tx_pps,
                   (unsigned long)(g_stats[p].ipv4_pkts - g_last_stats[p].ipv4_pkts),
                   (unsigned long)(g_stats[p].tcp_pkts - g_last_stats[p].tcp_pkts),
                   (unsigned long)(g_stats[p].udp_pkts - g_last_stats[p].udp_pkts),
                   (unsigned long)(g_stats[p].icmp_pkts - g_last_stats[p].icmp_pkts),
                   (unsigned long)(g_stats[p].other_pkts - g_last_stats[p].other_pkts));
        }
    }

    printf("TOTAL   RX: %8lu pps  TX: %8lu pps  RX: %.3f Gbps\n",
           (unsigned long)total_rx_pps,
           (unsigned long)total_tx_pps,
           (double)total_rx_bps / 1e9);
    printf("=====================================\n");

    memcpy(g_last_stats, g_stats, sizeof(g_stats));
    g_last_time = now;
}
