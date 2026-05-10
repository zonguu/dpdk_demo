#include "token_bucket.h"
#include <rte_cycles.h>
#include <rte_mbuf.h>

#define BITS_PER_BYTE 8

void token_bucket_init(struct token_bucket *tb, uint64_t rate_bps,
                       uint64_t burst_bits)
{
    tb->last_tsc = rte_get_timer_cycles();
    tb->tokens = (int64_t)(burst_bits ? burst_bits : (1514 * BITS_PER_BYTE));
    tb->rate_bps = rate_bps;
    tb->burst_bits = burst_bits ? burst_bits : (1514 * BITS_PER_BYTE);
    tb->timer_hz = rte_get_timer_hz();
}

int token_bucket_consume(struct token_bucket *tb, uint32_t pkt_len_bytes)
{
    uint64_t now = rte_get_timer_cycles();
    uint64_t delta = now - tb->last_tsc;
    tb->last_tsc = now;

    /* Replenish tokens: bits = delta * rate_bps / timer_hz */
    int64_t add = (int64_t)((delta * tb->rate_bps) / tb->timer_hz);
    tb->tokens += add;
    if (tb->tokens > (int64_t)tb->burst_bits)
        tb->tokens = (int64_t)tb->burst_bits;

    int64_t need = (int64_t)pkt_len_bytes * BITS_PER_BYTE;
    if (tb->tokens >= need) {
        tb->tokens -= need;
        return 1; /* pass */
    }
    return 0; /* drop */
}

uint16_t token_bucket_apply(struct token_bucket *tb,
                            struct rte_mbuf **pkts,
                            uint16_t nb_pkts)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < nb_pkts; i++) {
        if (token_bucket_consume(tb, pkts[i]->pkt_len)) {
            pkts[n++] = pkts[i];
        } else {
            rte_pktmbuf_free(pkts[i]);
        }
    }
    return n;
}
