#include <gtest/gtest.h>
#include "mbuf_clone_demo.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>

class MbufCloneDemoTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("mcd_pool", 256, 0, 0,
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

TEST_F(MbufCloneDemoTest, RunDoesNotCrash)
{
    /*
     * The demo function is primarily educational (prints to stdout).
     * Our only hard requirement is that it does not crash or leak
     * mbufs.  We verify this by checking the mempool counts before
     * and after the call.
     */
    unsigned int avail_before = rte_mempool_avail_count(pool);
    unsigned int inuse_before = rte_mempool_in_use_count(pool);

    mbuf_clone_demo_run(pool);

    unsigned int avail_after = rte_mempool_avail_count(pool);
    unsigned int inuse_after = rte_mempool_in_use_count(pool);

    EXPECT_EQ(avail_before, avail_after);
    EXPECT_EQ(inuse_before, inuse_after);
}
