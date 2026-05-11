#ifndef FLOW_TABLE_H
#define FLOW_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <rte_mbuf.h>

/*
 * Simple per-flow statistics using DPDK rte_hash.
 * Each lcore should have its own flow table to avoid locking.
 */

#define FLOW_TABLE_MAX_ENTRIES 1024

typedef struct {
    uint32_t src_ip;      /* network byte order */
    uint32_t dst_ip;      /* network byte order */
    uint16_t src_port;    /* network byte order */
    uint16_t dst_port;    /* network byte order */
    uint8_t  proto;
} __attribute__((packed)) flow_key_t;

typedef struct {
    uint64_t pkt_count;
    uint64_t byte_count;
    uint64_t last_tsc;    /* last seen timestamp */
} flow_data_t;

/* Opaque handle to a per-lcore flow table */
struct flow_table;

/**
 * @brief Create a flow table on the given socket.
 *
 * @param name       Unique name (used for rte_hash internal naming).
 * @param socket_id  NUMA socket for memory allocation.
 * @return           Flow table handle, or NULL on error.
 */
struct flow_table *flow_table_create(const char *name, int socket_id);

/**
 * @brief Record a packet into the flow table.
 *
 * Looks up the 5-tuple; if the flow exists its counters are updated,
 * otherwise a new entry is inserted.
 *
 * @param ft  Flow table handle.
 * @param m   Received mbuf (must contain Ethernet + IPv4 header).
 */
void flow_table_record(struct flow_table *ft, struct rte_mbuf *m);

/**
 * @brief Print the top-N flows by packet count.
 *
 * Iterates over the entire hash table.  Called from the stats loop.
 *
 * @param ft  Flow table handle.
 * @param n   Number of top flows to print.
 */
void flow_table_print_top(struct flow_table *ft, uint32_t n);

/**
 * @brief Destroy the flow table and free underlying hash memory.
 */
void flow_table_destroy(struct flow_table *ft);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_TABLE_H */
