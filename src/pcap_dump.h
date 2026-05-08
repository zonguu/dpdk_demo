#ifndef PCAP_DUMP_H
#define PCAP_DUMP_H

#include <rte_mbuf.h>
#include <stdint.h>

#ifdef ENABLE_PCAP_DUMP
int  pcap_dump_open(const char *filename);
void pcap_dump_close(void);
void pcap_dump_mbufs(struct rte_mbuf **mbufs, uint16_t nb_pkts);
#else
static inline int pcap_dump_open(const char *filename)
{
    (void)filename;
    return 0;
}
static inline void pcap_dump_close(void) {}
static inline void pcap_dump_mbufs(struct rte_mbuf **mbufs, uint16_t nb_pkts)
{
    (void)mbufs;
    (void)nb_pkts;
}
#endif

#endif /* PCAP_DUMP_H */
