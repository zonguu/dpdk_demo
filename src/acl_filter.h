#ifndef ACL_FILTER_H
#define ACL_FILTER_H

#include <stdint.h>
#include <rte_mbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Simple hard-coded 5-tuple ACL rule.
 * Action: 0 = drop, 1 = accept.
 */
struct acl_rule {
    uint32_t src_ip;      /* network byte order */
    uint32_t dst_ip;      /* network byte order */
    uint16_t src_port;    /* network byte order */
    uint16_t dst_port;    /* network byte order */
    uint8_t  proto;       /* IPPROTO_TCP/UDP/ICMP */
    uint8_t  action;      /* 0=drop, 1=accept */
};

/*
 * Evaluate mbuf against the built-in rule table.
 * Returns 1 = accept, 0 = drop.
 */
int acl_filter_evaluate(struct rte_mbuf *m);

/*
 * Statistics helpers.
 */
void acl_filter_get_stats(uint64_t *accepted, uint64_t *dropped);
void acl_filter_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* ACL_FILTER_H */
