#include "rss_hash.h"
#include "packet_parser.h"

#include <rte_byteorder.h>
#include <rte_jhash.h>

/*
 * Packed 5-tuple key used for hashing.
 * All fields are in network byte order so the hash is identical
 * regardless of host endianness.
 */
struct __attribute__((packed)) rss_key {
    uint32_t src_ip;    /* network byte order */
    uint32_t dst_ip;    /* network byte order */
    uint16_t src_port;  /* network byte order */
    uint16_t dst_port;  /* network byte order */
    uint8_t  proto;
};

uint32_t
rss_hash_packet(struct rte_mbuf *m)
{
    packet_info_t info;

    /* Parse the packet; if it is not IPv4 we return 0 as a fallback. */
    if (unlikely(packet_parse(m, &info) != 0 || !info.is_ipv4))
        return 0;

    struct rss_key key;
    key.src_ip = info.src_ip;               /* already network byte order */
    key.dst_ip = info.dst_ip;               /* already network byte order */
    key.src_port = rte_cpu_to_be_16(info.src_port); /* parser gives host order */
    key.dst_port = rte_cpu_to_be_16(info.dst_port);
    key.proto    = info.ip_proto;

    /*
     * rte_jhash is fast, has good avalanche properties, and does not
     * require any secret key like Toeplitz (rte_thash).
     * For a learning demo this is more than sufficient.
     */
    return rte_jhash(&key, sizeof(key), 0);
}
