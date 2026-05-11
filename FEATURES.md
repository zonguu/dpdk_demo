# DPDK 入门小特性路线图

> 基于当前 `dpdk_pcap_demo` 项目，按**由浅入深**排列。
> 每个特性都能独立实践，建议动手实现后再进入下一个。

---

## Level 1：看懂代码就能改

### 1. 命令行参数透传（已完成）
- **做什么**：通过 `--vdev=net_pcap0` 或 `--vdev=net_null0` 选择设备。
- **学什么**：DPDK EAL 参数解析、vdev 设备动态创建。
- **关键 API**：`rte_eal_init()` 的返回值处理。

### 2. 基础包头部解析 ✅ 已实现
- **做什么**：收到包后解析 **EtherType、Src MAC、Dst MAC、IP 协议、源/目的 IP、端口**。
- **学什么**：`rte_ether.h`、`rte_ip.h`、`rte_udp.h` / `rte_tcp.h` 头结构，指针偏移取字段。
- **关键 API**：`rte_pktmbuf_mtod()`、`rte_be_to_cpu_16()`、`rte_ipv4_hdr`。
- **代码位置**：`src/packet_parser.h`、`src/packet_parser.c`
- **预期输出**：
  ```
  TCP 192.168.1.1:12345 -> 192.168.1.2:80 (len=64)
  ```

### 3. 每秒 PPS / BPS 统计 ✅ 已实现
- **做什么**：计算**每秒收发包数**和**每秒比特率**，并统计 IPv4/TCP/UDP/ICMP/Other 协议分布。
- **学什么**：`rte_get_timer_hz()` / `rte_get_timer_cycles()` 高精度计时；DPDK 头结构解析。
- **代码位置**：`src/stats.h`、`src/stats.c`
- **预期输出**：
  ```
  ========== STATS (last 1s) ==========
  Port 0  RX: 20470432 pps  TX: 20470432 pps  IPv4=0 TCP=0 UDP=0 ICMP=0 Other=20470432
  Port 1  RX: 20470432 pps  TX: 20470432 pps  IPv4=0 TCP=0 UDP=0 ICMP=0 Other=20470432
  TOTAL   RX: 40940864 pps  TX: 40940864 pps  RX: 20.962 Gbps
  =====================================
  ```

### 4. 收包落盘（pcap 文件写出）✅ 已实现
- **做什么**：把收到的原始包写入 `.pcap` 文件，类似 tcpdump。
- **学什么**：pcap 文件格式、多线程文件 I/O 同步（rte_spinlock）、mbuf 分段处理。
- **关键 API**：标准 `fopen()`/`fwrite()` + `rte_spinlock_lock()` + mbuf chain 遍历。
- **代码位置**：`src/pcap_dump.h` / `.c`
- **编译开关**：`./build.sh --pcap-dump`
- **输出文件**：`output/captured.pcap`
- **特点**：支持多核并发写入（自旋锁保护），支持 chained mbuf（多段包）。
- **验证**：`tcpdump -r output/captured.pcap` 或 Wireshark 打开检查。

### 5. 内存池动态监控 ✅ 已实现
- **做什么**：主循环中**每秒打印一次所有 mempool 的使用率**。
- **学什么**：`rte_mempool_walk()`、`rte_mempool_avail_count()`、`rte_mempool_in_use_count()`。
- **代码位置**：`src/stats.c` 中的 `stats_print_periodic()`
- **预期输出**：
  ```
  [MEMPOOL] Snapshot:
  [MEMPOOL]   mbuf_pool_0              : avail= 8191 in-use=    0 total= 8191
  ```
- **特点**：自动遍历系统中所有 mempool，无需手动注册。

### 6. 包长直方图 / TTL 分布统计
- **做什么**：按包长区间（64B/128B/256B/512B/1024B/1518B+）和 TTL 值做直方图统计，每秒打印分布。
- **学什么**：直方图数据结构、边界划分、高效累加计数。
- **关键 API**：无 DPDK 特殊依赖，纯 C 数组即可。
- **难度**：⭐ 只需在 `stats_record_rx()` 中多解析两个字段并累加。

### 7. 批量预取优化（`rte_prefetch0`）✅ 已实现
- **做什么**：在 `rx_burst()` 后，对即将处理的 mbuf 数据做 CPU 预取，降低 cache miss。
- **学什么**：CPU cache line 原理、`rte_prefetch0()` 的用法与误区（不要预取太远）。
- **关键 API**：`rte_prefetch0(rte_pktmbuf_mtod(mbuf, void *))`
- **代码位置**：`src/packet_prefetch.h`（header-only inline）
- **集成**：所有 worker 模式（单核/多核/L2FWD/Pipeline）RX 后均调用。

---

## Level 2：需要理解 DPDK 机制

### 8. 多队列 + 多 lcore 收包 ✅ 已实现
- **做什么**：每个 lcore 负责一个端口（或一个队列），实现**真并行收包**。
- **学什么**：`rte_eal_remote_launch()`、lcore ID 与端口映射、避免竞争。
- **关键 API**：`rte_lcore_id()`、`rte_eal_remote_launch()`、`RTE_LCORE_FOREACH_WORKER()`。
- **代码位置**：`src/multi_queue_worker.h` / `.c`
- **编译开关**：`./build.sh --multicore`
- **运行要求**：至少 2 个 lcore（`-l 0-1`）
- **实测性能**（双核 `net_null`）：
  - 单核模式：约 **4100 万 pps**
  - 双核模式：约 **7500 万 pps**（接近线性扩展）
- **验证**：启动时加 `-l 0-3`，在 4 个核上同时跑，CPU 使用率拉满。

### 9. L2 转发（l2fwd）✅ 已实现
- **做什么**：双端口场景下，端口 0 收到的包从端口 1 发出，反之亦然；**交换源/目的 MAC 地址**。
- **学什么**：端口配对（port pair）、MAC 地址交换、转发延迟概念、mbuf 所有权转移。
- **关键 API**：`rte_eth_tx_burst(dst_port, ...)`、`rte_ether_addr_copy()`。
- **代码位置**：`src/l2fwd_worker.h` / `.c`
- **编译开关**：`./build.sh --l2fwd`
- **运行要求**：至少 1 个端口（2 个端口时 0<->1 转发，1 个端口时自环）
- **实测性能**（双核 `net_null`）：
  - 约 **3700 万 pps**（含 MAC 交换开销）
- **练习**：先用 `net_null0/net_null1` 跑通逻辑，再换 `net_pcap` 验证真实包内容。

### 10. Ring 无锁队列（lcore 间通信）✅ 已实现
- **做什么**：一个 lcore 负责 `rx_burst()`，把 mbuf 指针 `rte_ring_enqueue()` 到 Ring；另一个 lcore `rte_ring_dequeue()` 后做业务处理再发送。
- **学什么**：单生产者/单消费者（SP/SC）模式、mbuf 引用计数、多 lcore 协作。
- **关键 API**：`rte_ring_create()`、`rte_ring_enqueue_burst()`、`rte_ring_dequeue_burst()`、`rte_eal_remote_launch()`。
- **代码位置**：`src/pipeline.h`、`src/pipeline.c`
- **编译开关**：`./build.sh --pipeline`
- **运行要求**：至少 2 个 lcore（`-l 0-1`）
- **预期输出**：
  ```
  [MAIN] Running in pipeline mode (multi-lcore)
  [PIPELINE] Worker started on lcore 1
  [PIPELINE] Master on lcore 0, worker on lcore 1, 2 port(s)
  ========== STATS (last 1s) ==========
  Port 0  RX: 12629056 pps  TX: 12629056 pps  ...
  Port 1  RX: 11365536 pps  TX: 11365536 pps  ...
  TOTAL   RX: 23994592 pps  TX: 23994592 pps  RX: 12.285 Gbps
  ```
- **意义**：这是 DPDK **pipeline 模型**的基础（收包 → 处理 → 发包 解耦）。

### 11. 定时器（`rte_timer`）
- **做什么**：每隔 1 秒自动打印统计，而不是在主循环里用 `timer_cycles` 判断。
- **学什么**：DPDK 轮询式定时器、`rte_timer_reset()`、`rte_timer_manage()`。
- **关键 API**：`rte_timer_subsystem_init()`、`rte_timer_init()`、`rte_timer_manage()`。
- **注意**：必须在 EAL 线程（lcore）上调用 `rte_timer_manage()`。

### 12. 简单 ACL / 包过滤 ✅ 已实现
- **做什么**：只转发特定五元组的包，或丢弃特定目标端口的包。
- **学什么**：五元组匹配、硬编码规则表、原子计数统计。
- **关键 API**：无外部 `rte_acl` 依赖，纯 C if 判断保持简单。
- **代码位置**：`src/acl_filter.h` / `.c`
- **规则示例**：默认丢弃 UDP dst_port=9/19（discard/chargen），其余放行。
- **统计**：`acl_filter_get_stats()` 输出 accepted / dropped 计数。
- **测试**：`test/test_acl_filter.cpp`（4 个用例：accept、drop、non-IPv4、reset stats）

### 13. 软件 RSS / 五元组哈希分发 ✅ 已实现
- **做什么**：不依赖网卡硬件 RSS，用 `rte_jhash` 把包按五元组均匀分发到多个 worker lcore。
- **学什么**：一致性哈希、避免同一流被乱序处理、lcore 间负载均衡。
- **关键 API**：`rte_jhash()`、`rss_hash_packet()`、`rss_select_worker()`。
- **代码位置**：`src/rss_hash.h` / `.c`
- **集成**：`--pipeline` 模式下 Master RX 后按 hash 分桶 enqueue 到多个 worker ring。
- **验证**：`-l 0-3` 启动时可见 `3 worker(s)` 各自 dequeue 处理。

### 14. 简单流表（`rte_hash`）✅ 已实现
- **做什么**：用 `rte_hash` 维护一个五元组 → 流统计的哈希表，统计每条流的包数、字节数、最近活跃时间。
- **学什么**：DPDK 哈希库、key 构造、并发访问（per-lcore hash 避免锁）。
- **关键 API**：`rte_hash_create()`、`rte_hash_lookup()`、`rte_hash_add_key_data()`。
- **代码位置**：`src/flow_table.h` / `.c`
- **集成**：单核模式每秒打印 top-5 活跃流。
- **输出示例**：
  ```
  [FLOW] Top 5 flows (by packets):
  [FLOW]   192.168.1.1:12345 -> 192.168.1.2:80 proto=6  pkts=1024 bytes=65536
  ```

### 15. 端口镜像（Port Mirroring）✅ 已实现
- **做什么**：把端口 A 收到的流量**同时复制一份**到端口 B（或原端口转发 + 镜像端口发送）。
- **学什么**：`rte_pktmbuf_clone()`、mbuf 引用计数、零拷贝复制。
- **关键 API**：`rte_pktmbuf_clone()`、`rte_eth_tx_burst()`。
- **代码位置**：`src/port_mirror.h` / `.c`
- **集成**：单核/多核模式 RX 后自动 mirror 到配对端口 `port ^ 1`。
- **注意**：clone 后原 mbuf 和 clone 共享 data buffer，若需修改必须 `rte_pktmbuf_copy()`。

---

## Level 3：接近生产环境的小模块

### 16. KNI（Kernel Network Interface）
- **做什么**：把 DPDK 收到的包通过 KNI 送给 Linux 内核协议栈处理；内核发出的包回到 DPDK。
- **学什么**：DPDK 与内核协议栈互通、TUN/TAP 替代方案。
- **关键 API**：`rte_kni_init()`、`rte_kni_alloc()`、`rte_kni_tx_burst()` / `rx_burst()`。
- **前提**：需要编译 DPDK 时开启 KNI 模块（`CONFIG_RTE_LIBRTE_KNI=y`）。

### 17. 包修改 + 校验和重算 ✅ 已实现
- **做什么**：修改包的源/目的 IP 和 L4 端口，然后重新计算 IPv4 和 TCP/UDP checksum，再发送。
- **学什么**：`rte_ipv4_cksum()`、`rte_ipv4_udptcp_cksum()`、endian 转换。
- **关键 API**：`rte_ipv4_hdr` 字段修改、软 checksum 重算。
- **代码位置**：`src/packet_modify.h` / `.c`
- **集成**：`--l2fwd` 模式下自动执行 MAC + IP + Port 交换并重新计算 checksum（对称转发演示）。
- **测试**：`test/test_packet_modify.cpp`（4 个用例：swap IP、swap port、recalc checksum、swap all）

### 18. 简单 Token Bucket 限速 ✅ 已实现
- **做什么**：对 TX 方向做软件限速，默认限制每核 100 Mbps。
- **学什么**：令牌桶算法、`rte_get_timer_cycles()` 高精度时间戳、按 lcore 独立 bucket。
- **代码位置**：`src/token_bucket.h` / `.c`
- **关键参数**：
  - `rate_bps = 100_000_000`（100 Mbps）
  - `burst_bits = 1514 * 8`（1 MTU）
- **集成**：单核/多核/Pipeline 模式均内置 ACL + Token Bucket 处理链。
- **测试**：`test/test_token_bucket.cpp`（4 个用例：init、first packet、burst limit、replenish）

### 19. 静态 ARP 应答
- **做什么**：收到 ARP Request 后，构造 ARP Reply 包并发送回去。
- **学什么**：从零构造 mbuf（`rte_pktmbuf_alloc()` + `rte_pktmbuf_append()`）、ARP 包格式、`rte_eth_tx_burst()` 发送自构造包。
- **关键 API**：`rte_pktmbuf_alloc()`、`rte_pktmbuf_append()`、`rte_eth_tx_burst()`。
- **验证**：用 `net_pcap` + `iface=lo`，从本机 ping 一个静态 IP，观察 ARP 回复。
- **难度**：⭐⭐ 需要手动填 Ethernet + ARP 头部，是**从零发包**的最佳入门练习。

### 20. ICMP Echo Reply（Ping 应答）✅ 已实现
- **做什么**：收到 ICMP Echo Request（Ping）后，交换 MAC/IP、改 ICMP type=0（Reply）、重算 checksum，再发送。
- **学什么**：ICMP 头部结构、完整修改→重算→发包闭环。
- **关键 API**：`rte_ipv4_cksum()`、`rte_raw_cksum()`、`rte_pktmbuf_mtod()`。
- **代码位置**：`src/icmp_reply.h` / `.c`
- **集成**：单核模式下 RX 后自动识别 ICMP Echo Request 并回复，回复包不进入正常 TX 路径。
- **验证**：用 `net_pcap,iface=lo`，从本机 ping DPDK 监听的 IP，观察回复。

### 21. TX 拥塞重试与丢包统计 ✅ 已实现
- **做什么**：`rte_eth_tx_burst()` 返回小于 `nb_tx` 时不直接 free，而是做有限次（3 次）重试，超时后才丢弃。
- **学什么**：DPDK 发送队列拥塞模型、忙等 vs 退让策略。
- **关键 API**：`rte_eth_tx_burst()`、`rte_get_timer_cycles()` 做微秒级忙等。
- **代码位置**：`src/tx_retry.h` / `.c`
- **集成**：替换所有 worker 中的 `rte_eth_tx_burst` + 手动 free 逻辑。
- **参数**：`TX_RETRY_MAX = 3`，每次重试间隔约 1 µs。

### 22. mbuf Clone / 引用计数演示 ✅ 已实现
- **做什么**：程序启动时运行一次独立 demo，展示 alloc → clone → modify → copy → free 的完整生命周期。
- **学什么**：mbuf 结构体、`refcnt` 引用计数、data buffer 共享 vs 独享。
- **关键 API**：`rte_pktmbuf_clone()`、`rte_pktmbuf_copy()`、`rte_mbuf_refcnt_read()`。
- **代码位置**：`src/mbuf_clone_demo.h` / `.c`
- **输出示例**：
  ```
  [CLONE-DEMO] 2. Cloned mbuf
  [CLONE-DEMO]    clone data[0] = 0xAB  refcnt(data) = 1
  [CLONE-DEMO] 3. Modified orig buffer to 0xCD
  [CLONE-DEMO]    clone data[0] = 0xCD  <-- same buffer!
  [CLONE-DEMO] 5. Modified copy buffer to 0xEF
  [CLONE-DEMO]    orig data[0]  = 0xCD  <-- unchanged!
  ```

### 23. 热升级 / 优雅退出 + 资源统计
- **做什么**：收到 `SIGUSR1` 时 dump 当前所有统计到文件，收到 `SIGINT` 时确保所有 mbuf 已释放再退出。
- **学什么**：信号处理、资源泄漏检查、`rte_mempool_in_use_count()` 断言为 0。
- **验证**：valgrind 跑 `--no-huge` 模式，确认无内存泄漏（DPDK 大页 valgrind 会有误报，普通内存模式更干净）。

### 24. 多进程模式（Primary / Secondary）
- **做什么**：进程 A（Primary）初始化 EAL 和端口；进程 B（Secondary）attach 同一个 mempool 和端口做收发。
- **学什么**：DPDK 多进程共享内存、`--proc-type=primary/secondary`。
- **关键 API**：`rte_eal_init()` 参数区分主从进程、`rte_mp_channel`。

---

## 推荐学习顺序

```
2 → 3 → 5 → 6 → 8 → 7 → 9 → 10 → 12 → 13 → 19 → 20
```

- **先做单核单队列**：把包解析、统计、过滤做扎实。
- **再做多核**：理解 lcore、queue、ring 的协作关系。
- **最后做包修改和限速**：接近真实转发设备逻辑。
- **收尾练习 ARP + Ping 应答**：体验从零构造/修改数据包的完整闭环。

---

## 调试技巧速查

| 问题 | 方法 |
|------|------|
| 看不到包 | 确认 `--vdev` 参数正确；`iface=lo` 时要发 ping；`net_null` 会有自环假包 |
| 内存池不足 | 调大 `NUM_MBUFS`，或检查 `rte_mempool_in_use_count()` |
| Segfault | 检查 `rte_pktmbuf_mtod()` 前确认 mbuf 非空；注意 `tx_burst()` 后不要复用已发送的 mbuf |
| 性能低 | 加大 `BURST_SIZE` 到 32/64/128；确认在 Release 模式编译 (`-DCMAKE_BUILD_TYPE=Release`) |
| 没有 hugepage | 加 `--no-huge -m 256`，适合学习和 CI |

---

## 参考文档

- [DPDK API Documentation](https://doc.dpdk.org/api/)
- `examples/l2fwd` — 官方二层转发示例
- `examples/helloworld` — 最简 EAL 初始化示例
- `examples/rxtx_callbacks` — 收发回调示例
- `examples/qos_meter` — 限速/token bucket 官方实现
