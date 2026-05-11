#include "mbuf_clone_demo.h"

#include <stdio.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_mbuf_dyn.h>

#define DEMO_DATA_LEN 64

void
mbuf_clone_demo_run(struct rte_mempool *mp)
{
    printf("\n========== MBUF CLONE DEMO ==========\n");

    /* 1. Allocate original mbuf and fill with known pattern */
    struct rte_mbuf *orig = rte_pktmbuf_alloc(mp);
    if (!orig) {
        printf("[CLONE-DEMO] Failed to alloc orig mbuf\n");
        return;
    }

    char *data = rte_pktmbuf_append(orig, DEMO_DATA_LEN);
    if (!data) {
        printf("[CLONE-DEMO] Failed to append data\n");
        rte_pktmbuf_free(orig);
        return;
    }
    memset(data, 0xAB, DEMO_DATA_LEN);
    printf("[CLONE-DEMO] 1. Allocated orig mbuf, filled with 0xAB\n");
    printf("[CLONE-DEMO]    orig data[0] = 0x%02X  refcnt = %u\n",
           (unsigned char)data[0], rte_mbuf_refcnt_read(orig));

    /*
     * 2. Clone the mbuf.
     * rte_pktmbuf_clone() allocates a NEW mbuf header (metadata)
     * but points its data buffer to the SAME underlying memory as
     * 'orig'.  The data buffer's reference counter is incremented.
     *
     * If we mistakenly assumed clone == deep-copy and modified the
     * clone later, we would silently corrupt the original packet
     * (or any other clones sharing the buffer).
     */
    struct rte_mbuf *clone = rte_pktmbuf_clone(orig, orig->pool);
    if (!clone) {
        printf("[CLONE-DEMO] Failed to clone mbuf\n");
        rte_pktmbuf_free(orig);
        return;
    }
    printf("[CLONE-DEMO] 2. Cloned mbuf\n");
    printf("[CLONE-DEMO]    clone data[0] = 0x%02X  refcnt(data) = %u\n",
           (unsigned char)rte_pktmbuf_mtod(clone, char *)[0],
           rte_mbuf_refcnt_read(clone));

    /*
     * 3. Modify the ORIGINAL data buffer.
     * Because orig and clone share the same data buffer, the clone
     * WILL see the change.  This is the defining characteristic of
     * a clone: zero-copy at the cost of shared mutability.
     *
     * If we needed to modify one without affecting the other
     * (e.g. NAT on the original, mirror on the clone), we would
     * have to use rte_pktmbuf_copy() BEFORE modifying either.
     */
    memset(rte_pktmbuf_mtod(orig, char *), 0xCD, DEMO_DATA_LEN);
    printf("[CLONE-DEMO] 3. Modified orig buffer to 0xCD\n");
    printf("[CLONE-DEMO]    orig data[0]  = 0x%02X\n",
           (unsigned char)rte_pktmbuf_mtod(orig, char *)[0]);
    printf("[CLONE-DEMO]    clone data[0] = 0x%02X  <-- same buffer!\n",
           (unsigned char)rte_pktmbuf_mtod(clone, char *)[0]);

    /*
     * 4. Deep-copy the original into an independent mbuf.
     * rte_pktmbuf_copy() allocates BOTH a new header AND a new
     * data buffer, then copies the payload byte-by-byte.
     *
     * This is more expensive than clone (memory allocation + memcpy),
     * but it gives us an isolated copy that can be modified freely.
     */
    struct rte_mbuf *copy = rte_pktmbuf_copy(orig, orig->pool, 0, UINT32_MAX);
    if (!copy) {
        printf("[CLONE-DEMO] Failed to copy mbuf\n");
        rte_pktmbuf_free(clone);
        rte_pktmbuf_free(orig);
        return;
    }
    printf("[CLONE-DEMO] 4. Deep-copied orig to independent mbuf\n");
    printf("[CLONE-DEMO]    copy data[0] = 0x%02X  refcnt(copy) = %u\n",
           (unsigned char)rte_pktmbuf_mtod(copy, char *)[0],
           rte_mbuf_refcnt_read(copy));

    /*
     * 5. Modify the copy; original must stay unchanged now.
     * If the original also changed, it would prove the copy was
     * NOT truly independent (a bug in rte_pktmbuf_copy or our
     * understanding of it).
     */
    memset(rte_pktmbuf_mtod(copy, char *), 0xEF, DEMO_DATA_LEN);
    printf("[CLONE-DEMO] 5. Modified copy buffer to 0xEF\n");
    printf("[CLONE-DEMO]    copy data[0]  = 0x%02X\n",
           (unsigned char)rte_pktmbuf_mtod(copy, char *)[0]);
    printf("[CLONE-DEMO]    orig data[0]  = 0x%02X  <-- unchanged!\n",
           (unsigned char)rte_pktmbuf_mtod(orig, char *)[0]);

    /*
     * 6. Cleanup.
     * Order does not matter here because refcnt handles the shared
     * buffer.  When we free 'orig', its data buffer refcnt drops
     * from 2 to 1 (because 'clone' still holds a reference), so
     * the buffer is NOT released yet.  When we then free 'clone',
     * refcnt reaches 0 and the buffer is finally returned to the
     * mempool.
     *
     * If we forgot to free any of them, the mbuf header AND the
     * data buffer would leak, eventually causing the pool to run
     * dry and all future allocations to fail.
     */
    rte_pktmbuf_free(copy);
    rte_pktmbuf_free(clone);
    rte_pktmbuf_free(orig);
    printf("[CLONE-DEMO] 6. Freed all mbufs cleanly\n");
    printf("========== MBUF CLONE DEMO END ==========\n\n");
}
