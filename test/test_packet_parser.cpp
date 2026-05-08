#include <gtest/gtest.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <netinet/in.h>

extern "C" {
#include "packet_parser.h"
}

class PacketParserTest : public ::testing::Test {
protected:
    struct rte_mempool* pool = nullptr;

    void SetUp() override {
        pool = rte_pktmbuf_pool_create("test_pool", 256, 0, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE, 0);
        ASSERT_NE(pool, nullptr) << "Failed to create test mempool";
    }

    void TearDown() override {
        if (pool) {
            rte_mempool_free(pool);
            pool = nullptr;
        }
    }

    struct rte_mbuf* make_packet(const uint8_t* data, size_t len) {
        struct rte_mbuf* m = rte_pktmbuf_alloc(pool);
        if (!m) return nullptr;
        void* dst = rte_pktmbuf_append(m, len);
        if (!dst) {
            rte_pktmbuf_free(m);
            return nullptr;
        }
        rte_memcpy(dst, data, len);
        return m;
    }
};

TEST_F(PacketParserTest, ParseIPv4UDP) {
    // Ethernet(14) + IPv4(20) + UDP(8) = 42 bytes
    uint8_t pkt[42] = {
        // Ethernet: dst=ff:ff:ff:ff:ff:ff, src=00:11:22:33:44:55, type=0x0800
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4: version=4, ihl=5, total_len=28, ttl=64, proto=UDP(17)
        0x45, 0x00, 0x00, 0x28,
        0x00, 0x01, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00,
        192, 168, 1, 1,
        192, 168, 1, 2,
        // UDP: src_port=12345(0x3039), dst_port=80(0x0050), len=8
        0x30, 0x39, 0x00, 0x50,
        0x00, 0x08, 0x00, 0x00
    };

    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(info.eth_type, 0x0800);
    EXPECT_EQ(info.is_ipv4, 1);
    EXPECT_EQ(info.is_udp, 1);
    EXPECT_EQ(info.is_tcp, 0);
    EXPECT_EQ(info.is_icmp, 0);
    EXPECT_EQ(info.src_port, 12345);
    EXPECT_EQ(info.dst_port, 80);
    EXPECT_EQ(info.ip_proto, IPPROTO_UDP);
    EXPECT_EQ(info.src_ip, (192U | (168U << 8) | (1U << 16) | (1U << 24)));
    EXPECT_EQ(info.dst_ip, (192U | (168U << 8) | (1U << 16) | (2U << 24)));

    // MAC addresses
    EXPECT_EQ(info.src_mac[0], 0x00);
    EXPECT_EQ(info.src_mac[5], 0x55);
    EXPECT_EQ(info.dst_mac[0], 0xff);
    EXPECT_EQ(info.dst_mac[5], 0xff);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, ParseIPv4TCP) {
    // Ethernet(14) + IPv4(20) + TCP(20) = 54 bytes
    uint8_t pkt[54] = {
        // Ethernet
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4: proto=TCP(6)
        0x45, 0x00, 0x00, 0x28,
        0x00, 0x01, 0x00, 0x00,
        0x40, 0x06, 0x00, 0x00,
        10, 0, 0, 1,
        10, 0, 0, 2,
        // TCP: src=443(0x01BB), dst=54321(0xD431)
        0x01, 0xBB, 0xD4, 0x31,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(info.is_ipv4, 1);
    EXPECT_EQ(info.is_tcp, 1);
    EXPECT_EQ(info.is_udp, 0);
    EXPECT_EQ(info.src_port, 443);
    EXPECT_EQ(info.dst_port, 54321);
    EXPECT_EQ(info.ip_proto, IPPROTO_TCP);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, ParseIPv4ICMP) {
    // Ethernet(14) + IPv4(20) + ICMP(8) = 42 bytes
    uint8_t pkt[42] = {
        // Ethernet
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4: proto=ICMP(1)
        0x45, 0x00, 0x00, 0x1c,
        0x00, 0x01, 0x00, 0x00,
        0x40, 0x01, 0x00, 0x00,
        172, 16, 0, 1,
        172, 16, 0, 2,
        // ICMP echo request type=8, code=0
        0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(info.is_ipv4, 1);
    EXPECT_EQ(info.is_icmp, 1);
    EXPECT_EQ(info.is_tcp, 0);
    EXPECT_EQ(info.is_udp, 0);
    EXPECT_EQ(info.ip_proto, IPPROTO_ICMP);
    EXPECT_EQ(info.src_port, 0);
    EXPECT_EQ(info.dst_port, 0);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, NonIPv4ReturnsError) {
    uint8_t pkt[14] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x06  // ARP
    };
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(info.eth_type, 0x0806);
    EXPECT_EQ(info.is_ipv4, 0);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, TooShortForEther) {
    uint8_t pkt[4] = {0x00, 0x11, 0x22, 0x33};
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, -1);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, FormatOutput) {
    packet_info_t info = {};
    info.is_ipv4 = 1;
    info.is_udp = 1;
    info.src_ip = (192U | (168U << 8) | (1U << 16) | (1U << 24));
    info.dst_ip = (192U | (168U << 8) | (1U << 16) | (2U << 24));
    info.src_port = 12345;
    info.dst_port = 80;
    info.ip_total_len = 64;

    char buf[256];
    int n = packet_format(&info, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "UDP 192.168.1.1:12345 -> 192.168.1.2:80 (len=64)");
}

TEST_F(PacketParserTest, MalformedIHLTooSmall) {
    // Ethernet(14) + IPv4 with IHL=4 (16 bytes, invalid)
    uint8_t pkt[34] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4: version=4, IHL=4 -> invalid
        0x44, 0x00, 0x00, 0x14,
        0x00, 0x00, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00,
        192, 168, 1, 1,
        192, 168, 1, 2,
    };
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, -1);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, TotalLengthSmallerThanHeader) {
    // Ethernet(14) + IPv4(20) with total_length = 10 (< 20)
    uint8_t pkt[34] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4: total_length = 10
        0x45, 0x00, 0x00, 0x0a,
        0x00, 0x00, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00,
        192, 168, 1, 1,
        192, 168, 1, 2,
    };
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, -1);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, FragmentedUDPNoL4Ports) {
    // Ethernet(14) + IPv4(20) + UDP(8) = 42 bytes
    // Set MF flag and fragment offset > 0
    uint8_t pkt[42] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        // IPv4: fragment_offset has MF=1, offset=8 (0x2040 in BE)
        0x45, 0x00, 0x00, 0x1c,
        0x00, 0x00, 0x20, 0x40, // flags/frag offset
        0x40, 0x11, 0x00, 0x00,
        192, 168, 1, 1,
        192, 168, 1, 2,
        // UDP header present but should be ignored due to fragmentation
        0x30, 0x39, 0x00, 0x50,
        0x00, 0x08, 0x00, 0x00
    };
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(info.is_ipv4, 1);
    EXPECT_EQ(info.ip_proto, IPPROTO_UDP);
    // L4 ports should NOT be parsed for fragmented packets
    EXPECT_EQ(info.is_udp, 0);
    EXPECT_EQ(info.src_port, 0);
    EXPECT_EQ(info.dst_port, 0);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, FragmentedTCPNoL4Ports) {
    // Ethernet(14) + IPv4(20) + TCP(20) = 54 bytes
    // MF flag set
    uint8_t pkt[54] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        0x45, 0x00, 0x00, 0x28,
        0x00, 0x00, 0x20, 0x00, // MF=1
        0x40, 0x06, 0x00, 0x00,
        10, 0, 0, 1,
        10, 0, 0, 2,
        0x01, 0xBB, 0xD4, 0x31,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(info.is_ipv4, 1);
    EXPECT_EQ(info.ip_proto, IPPROTO_TCP);
    EXPECT_EQ(info.is_tcp, 0);
    EXPECT_EQ(info.src_port, 0);
    EXPECT_EQ(info.dst_port, 0);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, EmptyMbuf) {
    struct rte_mbuf* m = rte_pktmbuf_alloc(pool);
    ASSERT_NE(m, nullptr);
    // Do not append any data -> data_len == 0

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, -1);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, ZeroPortNumbers) {
    // UDP with both ports = 0
    uint8_t pkt[42] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x08, 0x00,
        0x45, 0x00, 0x00, 0x1c,
        0x00, 0x01, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00,
        192, 168, 1, 1,
        192, 168, 1, 2,
        0x00, 0x00, 0x00, 0x00, // src=0, dst=0
        0x00, 0x08, 0x00, 0x00
    };
    struct rte_mbuf* m = make_packet(pkt, sizeof(pkt));
    ASSERT_NE(m, nullptr);

    packet_info_t info;
    int ret = packet_parse(m, &info);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(info.is_udp, 1);
    EXPECT_EQ(info.src_port, 0);
    EXPECT_EQ(info.dst_port, 0);

    rte_pktmbuf_free(m);
}

TEST_F(PacketParserTest, FormatICMP) {
    packet_info_t info = {};
    info.is_ipv4 = 1;
    info.is_icmp = 1;
    info.src_ip = (10U | (0U << 8) | (0U << 16) | (1U << 24));
    info.dst_ip = (10U | (0U << 8) | (0U << 16) | (2U << 24));
    info.ip_total_len = 32;

    char buf[256];
    int n = packet_format(&info, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "ICMP 10.0.0.1 -> 10.0.0.2 (len=32)");
}

TEST_F(PacketParserTest, FormatNonTcpUdpIcmp) {
    packet_info_t info = {};
    info.is_ipv4 = 1;
    info.ip_proto = 47; // GRE
    info.src_ip = (1U | (2U << 8) | (3U << 16) | (4U << 24)); // 1.2.3.4
    info.dst_ip = (5U | (6U << 8) | (7U << 16) | (8U << 24)); // 5.6.7.8
    info.ip_total_len = 100;

    char buf[256];
    int n = packet_format(&info, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_STREQ(buf, "IP proto=47 1.2.3.4 -> 5.6.7.8 (len=100)");
}
