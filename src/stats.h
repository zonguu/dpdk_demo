#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <rte_mbuf.h>

#define STATS_MAX_PORTS 16

typedef struct {
    uint64_t rx_pkts;
    uint64_t rx_bytes;
    uint64_t tx_pkts;
    uint64_t tx_bytes;

    /* Protocol distribution */
    uint64_t ipv4_pkts;
    uint64_t tcp_pkts;
    uint64_t udp_pkts;
    uint64_t icmp_pkts;
    uint64_t other_pkts;
} port_stats_t;

/* Reset all counters */
void stats_init(void);

/*
 * Record RX burst. Parses each packet to update protocol counters.
 */
void stats_record_rx(uint16_t port_id, struct rte_mbuf **mbufs, uint16_t nb_rx);

/*
 * Record TX burst. nb_total is the original number tried to send.
 */
void stats_record_tx(uint16_t port_id, uint16_t nb_tx, uint16_t nb_total);

/*
 * Check if one second has elapsed and print PPS/BPS stats.
 * Call this in the main loop; it uses rte_get_timer_cycles().
 */
void stats_print_periodic(void);

#endif /* STATS_H */
