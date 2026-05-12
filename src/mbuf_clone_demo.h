#ifndef MBUF_CLONE_DEMO_H
#define MBUF_CLONE_DEMO_H

#include <rte_mempool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run a standalone demonstration of mbuf clone / copy semantics.
 *
 * This is NOT part of the main forwarding path; it is a one-shot
 * educational routine that shows:
 *
 *   1. Allocate an mbuf and fill it with known data.
 *   2. Clone it (shares data buffer, refcnt++).
 *   3. Verify the clone sees the same data.
 *   4. Modify the original mbuf data.
 *   5. Verify the clone ALSO sees the modified data (shared buffer!).
 *   6. Deep-copy the original (independent data buffer).
 *   7. Modify the copy and verify the original is unchanged.
 *   8. Free everything cleanly.
 *
 * Call this once at startup (e.g. from main.c after port init) to
 * observe the behaviour in the console output.
 *
 * @param mp  An initialized mbuf pool.
 */
void mbuf_clone_demo_run(struct rte_mempool *mp);

#ifdef __cplusplus
}
#endif

#endif /* MBUF_CLONE_DEMO_H */
