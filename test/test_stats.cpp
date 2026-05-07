#include <gtest/gtest.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <netinet/in.h>

extern "C" {
#include "stats.h"
}

class StatsTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("stats_test_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr);
        stats_init();
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }

    struct rte_mbuf* make_ipv4_udp_packet(size_t total_len) {
        struct rte_mbuf* m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;
        void* dst = rte_pktmbuf_append(m, total_len);
        if (!dst) {
            rte_pktmbuf_free(m);
            return nullptr;
        }
        uint8_t* p = static_cast<uint8_t*>(dst);
        // Ethernet header (14 bytes)
        memset(p, 0, 14);
        p[12] = 0x08; p[13] = 0x00;
        // IPv4 header (20 bytes)
        p[14] = 0x45; // version+ihl
        p[15] = 0x00;
        p[16] = (total_len >> 8) & 0xFF;
        p[17] = total_len & 0xFF;
        p[22] = 0x40; // TTL
        p[23] = IPPROTO_UDP;
        // UDP header (8 bytes)
        p[34] = 0x30; p[35] = 0x39; // src port 12345
        p[36] = 0x00; p[37] = 0x50; // dst port 80
        return m;
    }

    struct rte_mbuf* make_arp_packet() {
        struct rte_mbuf* m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;
        void* dst = rte_pktmbuf_append(m, 14);
        if (!dst) {
            rte_pktmbuf_free(m);
            return nullptr;
        }
        uint8_t* p = static_cast<uint8_t*>(dst);
        memset(p, 0, 14);
        p[12] = 0x08; p[13] = 0x06; // ARP
        return m;
    }
};

TEST_F(StatsTest, RecordRxCountsPackets) {
    struct rte_mbuf* m1 = make_ipv4_udp_packet(42);
    struct rte_mbuf* m2 = make_ipv4_udp_packet(42);
    ASSERT_NE(m1, nullptr);
    ASSERT_NE(m2, nullptr);

    struct rte_mbuf* bufs[2] = {m1, m2};
    stats_record_rx(0, bufs, 2);

    EXPECT_EQ(stats_get_rx_pkts(0), 2);
    EXPECT_EQ(stats_get_rx_bytes(0), 84);
    EXPECT_EQ(stats_get_ipv4_pkts(0), 2);
    EXPECT_EQ(stats_get_udp_pkts(0), 2);

    rte_pktmbuf_free(m1);
    rte_pktmbuf_free(m2);
}

TEST_F(StatsTest, RecordTxCountsPackets) {
    stats_record_tx(0, 3, 5);
    EXPECT_EQ(stats_get_tx_pkts(0), 3);
}

TEST_F(StatsTest, MultiPortIsolation) {
    struct rte_mbuf* m = make_ipv4_udp_packet(42);
    ASSERT_NE(m, nullptr);

    struct rte_mbuf* bufs[1] = {m};
    stats_record_rx(0, bufs, 1);
    stats_record_rx(1, bufs, 1);

    EXPECT_EQ(stats_get_rx_pkts(0), 1);
    EXPECT_EQ(stats_get_rx_pkts(1), 1);

    rte_pktmbuf_free(m);
}

TEST_F(StatsTest, NonIPv4CountedAsOther) {
    struct rte_mbuf* m = make_arp_packet();
    ASSERT_NE(m, nullptr);

    struct rte_mbuf* bufs[1] = {m};
    stats_record_rx(0, bufs, 1);

    EXPECT_EQ(stats_get_rx_pkts(0), 1);
    EXPECT_EQ(stats_get_ipv4_pkts(0), 0);

    rte_pktmbuf_free(m);
}
