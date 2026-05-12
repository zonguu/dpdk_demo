#include <gtest/gtest.h>
#include "port_mirror.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>

class PortMirrorTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("pm_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr);
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }
};

TEST_F(PortMirrorTest, CloneDoesNotModifyOriginal)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
    ASSERT_NE(m, nullptr);

    char *data = rte_pktmbuf_append(m, 64);
    ASSERT_NE(data, nullptr);
    memset(data, 0xAB, 64);

    unsigned int inuse_before = rte_mempool_in_use_count(pool);

    /*
     * port_mirror_send clones each mbuf and tries to transmit.
     * In the test environment tx_burst fails (no real port), so the
     * clone is freed internally.  The original mbuf must survive.
     */
    uint16_t sent = port_mirror_send(0, &m, 1);
    (void)sent;

    /* After port_mirror_send the original mbuf must still be valid */
    EXPECT_EQ(rte_pktmbuf_pkt_len(m), 64u);
    EXPECT_EQ((unsigned char)rte_pktmbuf_mtod(m, char *)[0], 0xAB);

    /* inuse count should be unchanged: clone allocated + freed = net 0 */
    unsigned int inuse_after = rte_mempool_in_use_count(pool);
    EXPECT_EQ(inuse_before, inuse_after);

    rte_pktmbuf_free(m);
}

TEST_F(PortMirrorTest, MultipleMbufs)
{
    struct rte_mbuf *mbufs[4];
    for (int i = 0; i < 4; i++) {
        mbufs[i] = rte_pktmbuf_alloc(pool);
        ASSERT_NE(mbufs[i], nullptr);
        void *d = rte_pktmbuf_append(mbufs[i], 32);
        ASSERT_NE(d, nullptr);
        memset(d, (unsigned char)i, 32);
    }

    unsigned int inuse_before = rte_mempool_in_use_count(pool);

    port_mirror_send(0, mbufs, 4);

    /* port_mirror_send should not leak clones even when tx_burst fails */
    unsigned int inuse_after_send = rte_mempool_in_use_count(pool);
    EXPECT_EQ(inuse_before, inuse_after_send);

    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(rte_pktmbuf_pkt_len(mbufs[i]), 32u);
        rte_pktmbuf_free(mbufs[i]);
    }
}
