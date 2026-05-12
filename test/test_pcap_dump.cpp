#include <gtest/gtest.h>
extern "C" {
#include "pcap_dump.h"
}

#include <sys/stat.h>
#include <unistd.h>

/*
 * pcap_dump is only functional when ENABLE_PCAP_DUMP is ON.
 * In the default build it is a no-op stub, so these tests verify
 * the stub does not crash and returns success.
 */

TEST(PcapDump, OpenCloseNoCrash)
{
    int ret = pcap_dump_open("/tmp/test_pcap_dump.pcap");
    EXPECT_EQ(ret, 0);
    pcap_dump_close();
    unlink("/tmp/test_pcap_dump.pcap");
}

TEST(PcapDump, WriteMbufsNoCrash)
{
    struct rte_mempool *pool = rte_pktmbuf_pool_create(
        "pd_test_pool", 64, 0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, 0);
    ASSERT_NE(pool, nullptr);

    struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
    ASSERT_NE(m, nullptr);
    void *data = rte_pktmbuf_append(m, 64);
    ASSERT_NE(data, nullptr);
    memset(data, 0xAB, 64);

    /* Should not crash even when pcap dump is disabled */
    pcap_dump_mbufs(&m, 1);

    rte_pktmbuf_free(m);
    rte_mempool_free(pool);
}
