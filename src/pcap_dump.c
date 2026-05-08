#ifdef ENABLE_PCAP_DUMP

#include "pcap_dump.h"

#include <stdio.h>
#include <time.h>

#include <rte_mbuf.h>
#include <rte_spinlock.h>

/* pcap file format structures */
struct pcap_global_hdr {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} __attribute__((packed));

struct pcap_packet_hdr {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} __attribute__((packed));

static FILE *g_pcap_fp = NULL;
static rte_spinlock_t g_pcap_lock;

int pcap_dump_open(const char *filename)
{
    g_pcap_fp = fopen(filename, "wb");
    if (!g_pcap_fp) {
        fprintf(stderr, "[PCAP] Failed to open %s for writing\n", filename);
        return -1;
    }

    struct pcap_global_hdr hdr = {
        .magic_number  = 0xa1b2c3d4,
        .version_major = 2,
        .version_minor = 4,
        .thiszone      = 0,
        .sigfigs       = 0,
        .snaplen       = 65535,
        .network       = 1, /* Ethernet */
    };

    fwrite(&hdr, sizeof(hdr), 1, g_pcap_fp);
    fflush(g_pcap_fp);

    rte_spinlock_init(&g_pcap_lock);
    printf("[PCAP] Dumping packets to %s\n", filename);
    return 0;
}

void pcap_dump_close(void)
{
    if (g_pcap_fp) {
        fclose(g_pcap_fp);
        g_pcap_fp = NULL;
        printf("[PCAP] Dump file closed.\n");
    }
}

void pcap_dump_mbufs(struct rte_mbuf **mbufs, uint16_t nb_pkts)
{
    struct timespec ts;
    struct rte_mbuf *m, *seg;

    if (!g_pcap_fp || nb_pkts == 0)
        return;

    clock_gettime(CLOCK_REALTIME, &ts);

    rte_spinlock_lock(&g_pcap_lock);
    for (uint16_t i = 0; i < nb_pkts; i++) {
        m = mbufs[i];
        uint32_t pkt_len = rte_pktmbuf_pkt_len(m);

        struct pcap_packet_hdr pkt_hdr = {
            .ts_sec   = (uint32_t)ts.tv_sec,
            .ts_usec  = (uint32_t)(ts.tv_nsec / 1000),
            .incl_len = pkt_len,
            .orig_len = pkt_len,
        };
        fwrite(&pkt_hdr, sizeof(pkt_hdr), 1, g_pcap_fp);

        /* Write all segments (handle chained mbufs) */
        for (seg = m; seg != NULL; seg = seg->next) {
            fwrite(rte_pktmbuf_mtod(seg, uint8_t *),
                   rte_pktmbuf_data_len(seg), 1, g_pcap_fp);
        }
    }
    fflush(g_pcap_fp);
    rte_spinlock_unlock(&g_pcap_lock);
}

#endif /* ENABLE_PCAP_DUMP */
