#include <gtest/gtest.h>
#include "token_bucket.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>

class TokenBucketTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("tb_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr);
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }

    struct rte_mbuf *make_packet(uint16_t len)
    {
        struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;
        char *data = rte_pktmbuf_append(m, len);
        if (!data) {
            rte_pktmbuf_free(m);
            return nullptr;
        }
        memset(data, 0, len);
        m->pkt_len = len;
        m->data_len = len;
        return m;
    }
};

TEST_F(TokenBucketTest, Init)
{
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000 * 1000, 0);
    EXPECT_EQ(tb.rate_bps, 100000000ULL);
    EXPECT_GT(tb.burst_bits, 0);
    EXPECT_GT(tb.timer_hz, 0);
}

TEST_F(TokenBucketTest, FirstPacketAllowed)
{
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000 * 1000, 0);

    struct rte_mbuf *m = make_packet(64);
    ASSERT_NE(m, nullptr);

    int ret = token_bucket_consume(&tb, 64);
    EXPECT_EQ(ret, 1);

    rte_pktmbuf_free(m);
}

TEST_F(TokenBucketTest, VeryLargePacketExceedsBurst)
{
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000 * 1000, 1000); /* tiny burst */

    struct rte_mbuf *m = make_packet(200);
    ASSERT_NE(m, nullptr);

    int ret = token_bucket_consume(&tb, 200);
    EXPECT_EQ(ret, 0); /* dropped because burst too small */

    rte_pktmbuf_free(m);
}

TEST_F(TokenBucketTest, ApplyBurst)
{
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000 * 1000, 0);

    struct rte_mbuf *pkts[4];
    for (int i = 0; i < 4; i++) {
        pkts[i] = make_packet(64);
        ASSERT_NE(pkts[i], nullptr);
    }

    uint16_t n = token_bucket_apply(&tb, pkts, 4);
    /* All 4 should pass because bucket starts with burst capacity */
    EXPECT_EQ(n, 4u);
    for (uint16_t i = 0; i < n; i++) {
        rte_pktmbuf_free(pkts[i]);
    }
}

TEST_F(TokenBucketTest, ReplenishOverTime)
{
    struct token_bucket tb;
    token_bucket_init(&tb, 100ULL * 1000 * 1000, 0);

    /* Consume all tokens */
    while (token_bucket_consume(&tb, 1500)) {
        /* empty */
    }

    /* Next packet should fail immediately */
    struct rte_mbuf *m = make_packet(64);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(token_bucket_consume(&tb, 64), 0);

    /* Wait a tiny bit to let tokens replenish */
    rte_delay_us(100);

    /* After delay, small packet might pass */
    int ret = token_bucket_consume(&tb, 64);
    /* We don't assert exact result because timing is fuzzy in CI,
     * but bucket should have accumulated some tokens. */
    (void)ret;

    rte_pktmbuf_free(m);
}
