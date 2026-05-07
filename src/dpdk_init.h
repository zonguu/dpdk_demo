#ifndef DPDK_INIT_H
#define DPDK_INIT_H

#include <rte_mempool.h>
#include <stdint.h>

/* Number of RX/TX queues per port */
#define NUM_RX_QUEUES 1
#define NUM_TX_QUEUES 1

/* RX/TX ring descriptor count */
#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

/* Number of mbufs in the pool */
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

/*
 * Initialize the EAL (Environment Abstraction Layer).
 * Returns 0 on success, negative on error.
 */
int eal_init(int argc, char **argv);

/*
 * Create a packet mbuf pool on the given NUMA socket.
 * Returns the mempool pointer, or NULL on error.
 */
struct rte_mempool *mbuf_pool_create(uint16_t socket_id);

/*
 * Configure and start a single port with one RX/TX queue.
 * mbuf_pool is used for the RX queue.
 * Returns 0 on success, negative on error.
 */
int port_init(uint16_t port_id, struct rte_mempool *mbuf_pool);

/*
 * Get the total number of available Ethernet ports.
 */
uint16_t get_port_count(void);

#endif /* DPDK_INIT_H */
