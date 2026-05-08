#ifndef MULTI_QUEUE_WORKER_H
#define MULTI_QUEUE_WORKER_H

/*
 * Multi-lcore packet processing.
 * Each lcore (including master) runs a polling loop on a subset of ports.
 * This demonstrates DPDK's natural multi-core parallelism without
 * explicit ring passing.
 *
 * Port assignment is round-robin across available lcores:
 *   lcore N handles ports [N, N+num_lcores, N+2*num_lcores, ...]
 */
void multi_lcore_loop(void);

#endif /* MULTI_QUEUE_WORKER_H */
