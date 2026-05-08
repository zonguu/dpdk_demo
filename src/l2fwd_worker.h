#ifndef L2FWD_WORKER_H
#define L2FWD_WORKER_H

/*
 * L2 Forwarding demo.
 * Receives packets on one port and transmits them on another.
 * Swaps source/destination MAC addresses before forwarding.
 *
 * Port pairing: port 0 <-> port 1, port 2 <-> port 3, etc.
 * If there is an odd number of ports, the last one echoes back.
 */
void l2fwd_loop(void);

#endif /* L2FWD_WORKER_H */
