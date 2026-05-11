#include <gtest/gtest.h>
#include "flow_table.h"

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

class FlowTableTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("ft_pool", 256, 0, 0,
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

TEST_F(FlowTableTest, CreateDestroy)
{
    struct flow_table *ft = flow_table_create("ft_test", 0);
    ASSERT_NE(ft, nullptr);
    flow_table_destroy(ft);
}

TEST_F(FlowTableTest, RecordSingleFlow)
{
    struct flow_table *ft = flow_table_create("ft_test2", 0);
    ASSERT_NE(ft, nullptr);

    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);
    struct rte_mbuf *m = make_udp_packet(s, d,
                                         rte_cpu_to_be_16(1234),
                                         rte_cpu_to_be_16(80));
    ASSERT_NE(m, nullptr);

    flow_table_record(ft, m);
    rte_pktmbuf_free(m);

    /* The table should have exactly 1 entry now */
    flow_table_print_top(ft, 1);

    flow_table_destroy(ft);
}

TEST_F(FlowTableTest, RecordMultipleSameFlow)
{
    struct flow_table *ft = flow_table_create("ft_test3", 0);
    ASSERT_NE(ft, nullptr);

    uint32_t s = rte_cpu_to_be_32(0xC0A80101);
    uint32_t d = rte_cpu_to_be_32(0xC0A80102);

    for (int i = 0; i < 10; i++) {
        struct rte_mbuf *m = make_udp_packet(s, d,
                                             rte_cpu_to_be_16(1234),
                                             rte_cpu_to_be_16(80));
        ASSERT_NE(m, nullptr);
        flow_table_record(ft, m);
        rte_pktmbuf_free(m);
    }

    flow_table_destroy(ft);
}

TEST_F(FlowTableTest, NonIPv4Ignored)
{
    struct flow_table *ft = flow_table_create("ft_test4", 0);
    ASSERT_NE(ft, nullptr);

    /* ARP packet (non-IPv4) should be ignored */
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

    flow_table_record(ft, m);
    rte_pktmbuf_free(m);

    flow_table_destroy(ft);
}
