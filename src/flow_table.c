#include "flow_table.h"
#include "packet_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <rte_byteorder.h>
#include <rte_cycles.h>
#include <rte_errno.h>
#include <rte_hash.h>
#include <rte_jhash.h>

struct flow_table {
    struct rte_hash *hash;
    /*
     * We store flow_data inline in a separate array indexed by hash entry.
     * rte_hash returns an integer position ("pos") on add/lookup; we use
     * that pos as a direct index into this array.
     *
     * NOTE: This only works because we never delete entries.  If we did,
     * the positions would be recycled by rte_hash and we would need a
     * more robust mapping (e.g. pointer stored via add_key_data).
     */
    flow_data_t data[FLOW_TABLE_MAX_ENTRIES];
};

struct flow_table *
flow_table_create(const char *name, int socket_id)
{
    struct flow_table *ft = calloc(1, sizeof(*ft));
    if (!ft) {
        fprintf(stderr, "[FLOW] Failed to allocate flow_table\n");
        return NULL;
    }

    struct rte_hash_parameters params = {
        .name           = name,
        .entries        = FLOW_TABLE_MAX_ENTRIES,
        .reserved       = 0,
        .key_len        = sizeof(flow_key_t),
        .hash_func      = rte_jhash,
        .hash_func_init_val = 0,
        .socket_id      = socket_id,
    };

    ft->hash = rte_hash_create(&params);
    if (!ft->hash) {
        fprintf(stderr, "[FLOW] rte_hash_create failed: %s\n",
                rte_strerror(rte_errno));
        free(ft);
        return NULL;
    }

    return ft;
}

void
flow_table_record(struct flow_table *ft, struct rte_mbuf *m)
{
    packet_info_t info;
    if (packet_parse(m, &info) != 0 || !info.is_ipv4)
        return;

    flow_key_t key;
    key.src_ip = info.src_ip;               /* already network byte order */
    key.dst_ip = info.dst_ip;               /* already network byte order */
    key.src_port = rte_cpu_to_be_16(info.src_port);
    key.dst_port = rte_cpu_to_be_16(info.dst_port);
    key.proto    = info.ip_proto;

    /*
     * CRITICAL: All fields in 'key' must be in NETWORK byte order.
     * If we mixed host and network order, two machines with different
     * endianness would compute different hashes for the SAME flow,
     * breaking flow affinity in a distributed deployment.
     */

    int32_t pos = rte_hash_lookup(ft->hash, &key);
    if (pos >= 0) {
        /* Existing flow: update counters in place. */
        ft->data[pos].pkt_count++;
        ft->data[pos].byte_count += rte_pktmbuf_pkt_len(m);
        ft->data[pos].last_tsc = rte_get_timer_cycles();
    } else {
        /* New flow: insert into hash table. */
        flow_data_t new_data = {
            .pkt_count  = 1,
            .byte_count = rte_pktmbuf_pkt_len(m),
            .last_tsc   = rte_get_timer_cycles(),
        };
        /*
         * We pass NULL as 'data' because we manage the flow_data array
         * ourselves using the returned position index.
         *
         * If we instead passed a pointer here, rte_hash would store it
         * internally and we would need to fetch it back via lookup_data()
         * on every packet.  That adds an extra pointer indirection in
         * the hot path and hurts cache locality.
         */
        int ret = rte_hash_add_key_data(ft->hash, &key, NULL);
        if (ret >= 0) {
            ft->data[ret] = new_data;
        }
        /*
         * ret < 0 means the table is full.  In a production dataplane
         * we would evict the oldest entry (LRU).  Here we silently
         * drop to avoid stalling the RX loop.
         *
         * If we blocked or malloc'd here, we would lose packets during
         * the allocation, destroying line-rate performance.
         */
    }
}

/* Simple helper to find top-N by packet count */
struct top_entry {
    flow_key_t key;
    flow_data_t data;
};

static int
cmp_top(const void *a, const void *b)
{
    const struct top_entry *ea = a;
    const struct top_entry *eb = b;
    if (ea->data.pkt_count < eb->data.pkt_count) return 1;
    if (ea->data.pkt_count > eb->data.pkt_count) return -1;
    return 0;
}

void
flow_table_print_top(struct flow_table *ft, uint32_t n)
{
    if (!ft || !ft->hash)
        return;

    struct top_entry entries[FLOW_TABLE_MAX_ENTRIES];
    uint32_t count = 0;

    const void *key;
    void *data;
    uint32_t iter = 0;
    while (rte_hash_iterate(ft->hash, &key, &data, &iter) >= 0) {
        if (count >= FLOW_TABLE_MAX_ENTRIES)
            break;
        entries[count].key = *(const flow_key_t *)key;
        /*
         * We do a second lookup to get the position index.
         * This is O(1) but wasteful; in production we would store
         * the index directly inside the iterator loop.  We keep it
         * simple here because print_top() is called once per second,
         * not in the hot path.
         */
        int32_t pos = rte_hash_lookup(ft->hash, key);
        if (pos >= 0)
            entries[count].data = ft->data[pos];
        count++;
    }

    if (count == 0)
        return;

    qsort(entries, count, sizeof(entries[0]), cmp_top);

    if (n > count)
        n = count;

    printf("[FLOW] Top %u flows (by packets):\n", n);
    for (uint32_t i = 0; i < n; i++) {
        char sip[16], dip[16];
        struct in_addr sa = { .s_addr = entries[i].key.src_ip };
        struct in_addr da = { .s_addr = entries[i].key.dst_ip };
        snprintf(sip, sizeof(sip), "%s", inet_ntoa(sa));
        snprintf(dip, sizeof(dip), "%s", inet_ntoa(da));
        printf("[FLOW]   %s:%u -> %s:%u proto=%u  pkts=%lu bytes=%lu\n",
               sip,
               rte_be_to_cpu_16(entries[i].key.src_port),
               dip,
               rte_be_to_cpu_16(entries[i].key.dst_port),
               entries[i].key.proto,
               (unsigned long)entries[i].data.pkt_count,
               (unsigned long)entries[i].data.byte_count);
    }
}

void
flow_table_destroy(struct flow_table *ft)
{
    if (!ft)
        return;
    if (ft->hash)
        rte_hash_free(ft->hash);
    /*
     * ft->data is inline (not a pointer), so we do NOT free it
     * separately.  If we accidentally free'd &ft->data here we
     * would corrupt the heap because it points into the middle
     * of the calloc'd block.
     */
    free(ft);
}
