#include <gtest/gtest.h>
#include "packet_modify.h"
#include "packet_parser.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>

class PacketModifyTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("pmod_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr);
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }

    struct rte_mbuf *make_udp_packet(uint32_t src_ip, uint32_t dst_ip,
                                     uint16_t src_port, uint16_t dst_port)
    {
        struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;

        size_t pkt_len = sizeof(struct rte_ether_hdr) +
                         sizeof(struct rte_ipv4_hdr) +
                         sizeof(struct rte_udp_hdr) + 4;
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
        ip->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                                            sizeof(struct rte_udp_hdr) + 4);
        ip->next_proto_id = IPPROTO_UDP;
        ip->src_addr = src_ip;
        ip->dst_addr = dst_ip;

        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip + 1);
        udp->src_port = src_port;
        udp->dst_port = dst_port;
        udp->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + 4);

        return m;
    }
};

TEST_F(PacketModifyTest, SwapIp)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);
    struct rte_mbuf *m = make_udp_packet(s, d,
                                         rte_cpu_to_be_16(1234),
                                         rte_cpu_to_be_16(5678));
    ASSERT_NE(m, nullptr);

    packet_modify_swap_ip(m);

    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    EXPECT_EQ(ip->src_addr, d);
    EXPECT_EQ(ip->dst_addr, s);

    rte_pktmbuf_free(m);
}

TEST_F(PacketModifyTest, SwapPort)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);
    uint16_t sp = rte_cpu_to_be_16(1234);
    uint16_t dp = rte_cpu_to_be_16(5678);
    struct rte_mbuf *m = make_udp_packet(s, d, sp, dp);
    ASSERT_NE(m, nullptr);

    packet_modify_swap_port(m);

    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((char *)ip +
                             ((ip->version_ihl & 0x0F) * 4));
    EXPECT_EQ(udp->src_port, dp);
    EXPECT_EQ(udp->dst_port, sp);

    rte_pktmbuf_free(m);
}

TEST_F(PacketModifyTest, RecalcChecksum)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);
    struct rte_mbuf *m = make_udp_packet(s, d,
                                         rte_cpu_to_be_16(1234),
                                         rte_cpu_to_be_16(5678));
    ASSERT_NE(m, nullptr);

    /* Corrupt checksums first */
    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    ip->hdr_checksum = 0xDEAD;
    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((char *)ip +
                             ((ip->version_ihl & 0x0F) * 4));
    udp->dgram_cksum = 0xBEEF;

    packet_modify_recalc_checksum(m);

    /* After recalc, IP checksum should be valid */
    EXPECT_NE(ip->hdr_checksum, 0);
    uint16_t saved_cksum = ip->hdr_checksum;
    ip->hdr_checksum = 0;
    EXPECT_EQ(rte_ipv4_cksum(ip), saved_cksum);
    ip->hdr_checksum = saved_cksum;

    rte_pktmbuf_free(m);
}

TEST_F(PacketModifyTest, SwapAll)
{
    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);
    uint16_t sp = rte_cpu_to_be_16(1234);
    uint16_t dp = rte_cpu_to_be_16(5678);
    struct rte_mbuf *m = make_udp_packet(s, d, sp, dp);
    ASSERT_NE(m, nullptr);

    packet_modify_swap_all(m);

    struct rte_ipv4_hdr *ip = rte_pktmbuf_mtod_offset(
        m, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
    EXPECT_EQ(ip->src_addr, d);
    EXPECT_EQ(ip->dst_addr, s);

    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((char *)ip +
                             ((ip->version_ihl & 0x0F) * 4));
    EXPECT_EQ(udp->src_port, dp);
    EXPECT_EQ(udp->dst_port, sp);

    /* MAC should also be swapped */
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    uint8_t expected_src[6] = {0};
    uint8_t expected_dst[6] = {0};
    /* originally all zeros for both src and dst in make_udp_packet */
    EXPECT_EQ(memcmp(eth->src_addr.addr_bytes, expected_src, 6), 0);
    EXPECT_EQ(memcmp(eth->dst_addr.addr_bytes, expected_dst, 6), 0);

    rte_pktmbuf_free(m);
}
