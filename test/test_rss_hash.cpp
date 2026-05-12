#include <gtest/gtest.h>
#include "rss_hash.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>

class RssHashTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("rss_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr);
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }

    struct rte_mbuf *make_packet(uint32_t src_ip, uint32_t dst_ip,
                                 uint16_t src_port, uint16_t dst_port,
                                 uint8_t proto)
    {
        struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;

        size_t l4_len = (proto == IPPROTO_UDP) ? sizeof(struct rte_udp_hdr)
                                                : sizeof(struct rte_tcp_hdr);
        size_t pkt_len = sizeof(struct rte_ether_hdr) +
                         sizeof(struct rte_ipv4_hdr) + l4_len + 4;
        char *data = rte_pktmbuf_append(m, pkt_len);
        if (!data) {
            rte_pktmbuf_free(m);
            return nullptr;
        }
        memset(data, 0, pkt_len);

        struct rte_ether_hdr *eth = (struct rte_ether_hdr *)data;
        eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

        struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
        ip->version_ihl = 0x45;
        ip->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + l4_len + 4);
        ip->next_proto_id = proto;
        ip->src_addr = src_ip;
        ip->dst_addr = dst_ip;

        if (proto == IPPROTO_UDP) {
            struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip + 1);
            udp->src_port = src_port;
            udp->dst_port = dst_port;
        } else if (proto == IPPROTO_TCP) {
            struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)(ip + 1);
            tcp->src_port = src_port;
            tcp->dst_port = dst_port;
        }

        return m;
    }
};

TEST_F(RssHashTest, SameFlowSameHash)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);
    uint16_t sp = rte_cpu_to_be_16(1234);
    uint16_t dp = rte_cpu_to_be_16(80);

    struct rte_mbuf *m1 = make_packet(s, d, sp, dp, IPPROTO_UDP);
    struct rte_mbuf *m2 = make_packet(s, d, sp, dp, IPPROTO_UDP);
    ASSERT_NE(m1, nullptr);
    ASSERT_NE(m2, nullptr);

    uint32_t h1 = rss_hash_packet(m1);
    uint32_t h2 = rss_hash_packet(m2);

    EXPECT_EQ(h1, h2);

    rte_pktmbuf_free(m1);
    rte_pktmbuf_free(m2);
}

TEST_F(RssHashTest, Different5TupleDifferentHash)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);

    struct rte_mbuf *m1 = make_packet(s, d,
                                      rte_cpu_to_be_16(1234),
                                      rte_cpu_to_be_16(80),
                                      IPPROTO_UDP);
    struct rte_mbuf *m2 = make_packet(s, d,
                                      rte_cpu_to_be_16(1235),  /* different src_port */
                                      rte_cpu_to_be_16(80),
                                      IPPROTO_UDP);
    ASSERT_NE(m1, nullptr);
    ASSERT_NE(m2, nullptr);

    uint32_t h1 = rss_hash_packet(m1);
    uint32_t h2 = rss_hash_packet(m2);

    /* Different 5-tuples should almost certainly produce different hashes */
    EXPECT_NE(h1, h2);

    rte_pktmbuf_free(m1);
    rte_pktmbuf_free(m2);
}

TEST_F(RssHashTest, SelectWorkerDistribution)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);

    uint16_t workers[4] = {0};
    for (int i = 0; i < 100; i++) {
        struct rte_mbuf *m = make_packet(s, d,
                                         rte_cpu_to_be_16(1000 + i),
                                         rte_cpu_to_be_16(80),
                                         IPPROTO_UDP);
        ASSERT_NE(m, nullptr);
        uint32_t h = rss_hash_packet(m);
        uint16_t w = rss_select_worker(h, 4);
        EXPECT_LT(w, 4u);
        workers[w]++;
        rte_pktmbuf_free(m);
    }

    /* All workers should have received at least some traffic */
    for (int i = 0; i < 4; i++) {
        EXPECT_GT(workers[i], 0u) << "Worker " << i << " got zero packets";
    }
}

TEST_F(RssHashTest, NonIPv4ReturnsZero)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
    ASSERT_NE(m, nullptr);
    uint8_t arp[14] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x06
    };
    void *dst = rte_pktmbuf_append(m, sizeof(arp));
    ASSERT_NE(dst, nullptr);
    memcpy(dst, arp, sizeof(arp));

    uint32_t h = rss_hash_packet(m);
    EXPECT_EQ(h, 0u);

    rte_pktmbuf_free(m);
}
