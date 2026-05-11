#include <gtest/gtest.h>
#include "tx_retry.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>

class TxRetryTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("txr_pool", 256, 0, 0,
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

TEST_F(TxRetryTest, ZeroPktsReturnsZero)
{
    struct rte_mbuf *pkts[1] = { nullptr };
    uint16_t n = tx_retry_burst(0, 0, pkts, 0);
    EXPECT_EQ(n, 0u);
}

TEST_F(TxRetryTest, NoPortDropsAfterRetries)
{
    /*
     * In the test environment there is no real port, so
     * rte_eth_tx_burst returns 0.  tx_retry_burst should retry
     * TX_RETRY_MAX (3) times, then free the mbuf and return 0.
     */
    struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
    ASSERT_NE(m, nullptr);

    struct rte_mbuf *pkts[1] = { m };
    uint16_t n = tx_retry_burst(0, 0, pkts, 1);

    /* Nothing sent because port 0 does not exist */
    EXPECT_EQ(n, 0u);

    /* mbuf was freed by tx_retry_burst after retries exhausted */
}

TEST_F(TxRetryTest, MultiplePktsAllDropped)
{
    struct rte_mbuf *pkts[4];
    for (int i = 0; i < 4; i++) {
        pkts[i] = rte_pktmbuf_alloc(pool);
        ASSERT_NE(pkts[i], nullptr);
    }

    uint16_t n = tx_retry_burst(0, 0, pkts, 4);
    EXPECT_EQ(n, 0u);

    /* All mbufs freed internally */
}
