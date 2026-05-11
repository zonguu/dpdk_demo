#ifndef RSS_HASH_H
#define RSS_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <rte_mbuf.h>

/* Maximum number of worker rings for software RSS dispatch */
#define RSS_MAX_WORKERS 4

/**
 * @brief Compute a software RSS hash over the IPv4 5-tuple.
 *
 * Uses rte_jhash() (Bob Jenkins lookup3) so the result is
 * deterministic and evenly distributed.  Non-IPv4 packets fall
 * back to hash = 0.
 *
 * @param m  Received mbuf.
 * @return   32-bit hash value.
 */
uint32_t rss_hash_packet(struct rte_mbuf *m);

/**
 * @brief Select a worker index from a hash value.
 *
 * Simple modulo mapping.  Callers should ensure nb_workers is
 * a power of two for best distribution, but any value works.
 *
 * @param hash        Hash value from rss_hash_packet().
 * @param nb_workers  Number of worker lcores.
 * @return            Worker index in [0, nb_workers).
 */
static inline uint16_t
rss_select_worker(uint32_t hash, uint16_t nb_workers)
{
    return (uint16_t)(hash % nb_workers);
}

#ifdef __cplusplus
}
#endif

#endif /* RSS_HASH_H */
