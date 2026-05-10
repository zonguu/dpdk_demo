#include "acl_filter.h"
#include "packet_parser.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_atomic.h>

/* Hard-coded demo rules — kept intentionally simple (no rte_acl). */
static struct acl_rule g_rules[] = {
    /* Example: drop traffic to dst port 9 (discard protocol) */
    {0, 0, 0, 0, IPPROTO_UDP, 0},
    /* Example: drop traffic to dst port 19 (chargen) */
    {0, 0, 0, 0, IPPROTO_UDP, 0},
};

static void acl_rules_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;
    g_rules[0].dst_port = rte_cpu_to_be_16(9);
    g_rules[1].dst_port = rte_cpu_to_be_16(19);
    initialized = 1;
}

static rte_atomic64_t g_accepted;
static rte_atomic64_t g_dropped;

int acl_filter_evaluate(struct rte_mbuf *m)
{
    packet_info_t info;
    if (packet_parse(m, &info) != 0)
        goto accept; /* non-IPv4 or malformed: let it through */

    if (!info.is_ipv4)
        goto accept;

    acl_rules_init();

    for (size_t i = 0; i < sizeof(g_rules)/sizeof(g_rules[0]); i++) {
        const struct acl_rule *r = &g_rules[i];
        if (r->proto != 0 && r->proto != info.ip_proto)
            continue;
        if (r->src_ip != 0 && r->src_ip != info.src_ip)
            continue;
        if (r->dst_ip != 0 && r->dst_ip != info.dst_ip)
            continue;
        if (r->src_port != 0 && r->src_port != rte_cpu_to_be_16(info.src_port))
            continue;
        if (r->dst_port != 0 && r->dst_port != rte_cpu_to_be_16(info.dst_port))
            continue;

        if (r->action == 0) {
            rte_atomic64_inc(&g_dropped);
            return 0;
        }
        /* action == 1 handled like accept */
    }

accept:
    rte_atomic64_inc(&g_accepted);
    return 1;
}

void acl_filter_get_stats(uint64_t *accepted, uint64_t *dropped)
{
    if (accepted)
        *accepted = (uint64_t)rte_atomic64_read(&g_accepted);
    if (dropped)
        *dropped = (uint64_t)rte_atomic64_read(&g_dropped);
}

void acl_filter_reset_stats(void)
{
    rte_atomic64_set(&g_accepted, 0);
    rte_atomic64_set(&g_dropped, 0);
}
