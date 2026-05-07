#ifndef PACKET_WORKER_H
#define PACKET_WORKER_H

#include <stdint.h>

/* Max burst size for RX/TX */
#define BURST_SIZE 32

/* Max number of ports we handle */
#define MAX_PORTS 16

/*
 * Main packet processing loop.
 * For each port, receive packets and echo them back (TX on same port).
 * Runs until 'force_quit' is set.
 */
void packet_loop(void);

/* Signal handler sets this to 1 to stop the loop */
extern volatile int force_quit;

/* Common signal handler (can be registered from main or worker) */
void signal_handler(int sig);

#endif /* PACKET_WORKER_H */
