#ifndef TOKEN_BUCKET_H
#define TOKEN_BUCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <rte_mbuf.h>

/*
 * Simple per-core token bucket rate limiter.
 * rate_bps: desired rate in bits per second (e.g. 100*1000*1000 for 100 Mbps).
 * burst_bits: maximum burst in bits (defaults to 1 MTU if zero).
 */
struct token_bucket {
    uint64_t last_tsc;
    int64_t  tokens;      /* remaining tokens in bits */
    uint64_t rate_bps;
    uint64_t burst_bits;
    uint64_t timer_hz;
};

/*
 * Initialise bucket. Must be called on each lcore before use.
 */
void token_bucket_init(struct token_bucket *tb, uint64_t rate_bps,
                       uint64_t burst_bits);

/*
 * Try to consume `pkt_len_bytes * 8` bits from the bucket.
 * Returns 1 if packet may pass, 0 if it should be dropped.
 */
int token_bucket_consume(struct token_bucket *tb, uint32_t pkt_len_bytes);

/*
 * Convenience: apply to a whole burst. Drops are done in-place by
 * setting dropped entries to NULL and compacting the array.
 * Returns number of packets allowed through.
 */
uint16_t token_bucket_apply(struct token_bucket *tb,
                            struct rte_mbuf **pkts,
                            uint16_t nb_pkts);

#ifdef __cplusplus
}
#endif

#endif /* TOKEN_BUCKET_H */
