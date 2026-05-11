#include <gtest/gtest.h>
#include "icmp_reply.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_icmp.h>

class IcmpReplyTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("icmp_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr);
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }

    struct rte_mbuf *make_icmp_echo_request(uint32_t src_ip, uint32_t dst_ip,
                                            uint16_t ident, uint16_t seq)
    {
        struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;

        size_t pkt_len = sizeof(struct rte_ether_hdr) +
                         sizeof(struct rte_ipv4_hdr) +
                         sizeof(struct rte_icmp_hdr) + 8;
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
                                            sizeof(struct rte_icmp_hdr) + 8);
        ip->next_proto_id = IPPROTO_ICMP;
        ip->src_addr = src_ip;
        ip->dst_addr = dst_ip;

        struct rte_icmp_hdr *icmp = (struct rte_icmp_hdr *)(ip + 1);
        icmp->icmp_type = RTE_IP_ICMP_ECHO_REQUEST; /* 8 */
        icmp->icmp_code = 0;
        icmp->icmp_ident = ident;
        icmp->icmp_seq_nb = seq;

        return m;
    }

    struct rte_mbuf *make_udp_packet(uint32_t src_ip, uint32_t dst_ip)
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

        return m;
    }
};

TEST_F(IcmpReplyTest, TooShortPacketDropped)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
    ASSERT_NE(m, nullptr);
    /* Do not append enough data */
    int ret = icmp_reply_send(m, 0);
    EXPECT_EQ(ret, 0);
    /* mbuf is freed inside icmp_reply_send on failure */
}

TEST_F(IcmpReplyTest, NonIPv4Dropped)
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

    int ret = icmp_reply_send(m, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(IcmpReplyTest, NonIcmpDropped)
{
    struct rte_mbuf *m = make_udp_packet(
        rte_cpu_to_be_32(0xC0A80101),
        rte_cpu_to_be_32(0xC0A80102));
    ASSERT_NE(m, nullptr);

    int ret = icmp_reply_send(m, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(IcmpReplyTest, IcmpEchoRequestRecognized)
{
    struct rte_mbuf *m = make_icmp_echo_request(
        rte_cpu_to_be_32(0xC0A80101),
        rte_cpu_to_be_32(0xC0A80102),
        rte_cpu_to_be_16(0x1234),
        rte_cpu_to_be_16(1));
    ASSERT_NE(m, nullptr);

    /* Verify pre-conditions */
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    struct rte_icmp_hdr *icmp = (struct rte_icmp_hdr *)(ip + 1);
    EXPECT_EQ(icmp->icmp_type, RTE_IP_ICMP_ECHO_REQUEST);

    /*
     * Call icmp_reply_send.  In the test environment there is no
     * real port, so rte_eth_tx_burst will fail and the mbuf will be
     * freed.  We can still verify the function correctly identified
     * the packet (returned 1) before the transmit attempt.
     */
    int ret = icmp_reply_send(m, 0);
    EXPECT_EQ(ret, 1);
}
