#ifndef PIPELINE_H
#define PIPELINE_H

/*
 * Multi-lcore pipeline using rte_ring:
 *   Master lcore : rx_burst() -> enqueue ring
 *   Worker lcore : dequeue ring -> parse/stats -> tx_burst()
 *
 * Returns 0 on success, -1 on error.
 */
int pipeline_run(void);

#endif /* PIPELINE_H */
