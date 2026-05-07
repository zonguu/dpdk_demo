#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${PROJECT_DIR}/build"
APP="${BUILD_DIR}/dpdk_pcap_demo"

if [ ! -f "${APP}" ]; then
    echo "[ERROR] Application not found: ${APP}"
    echo "[INFO] Please run ./build.sh first."
    exit 1
fi

# Helper to check root
need_root() {
    if [ "$EUID" -ne 0 ]; then
        echo "[WARN] DPDK often needs root for hugepages and pcap. Trying with sudo..."
        SUDO="sudo"
    else
        SUDO=""
    fi
}

# ------------------------------------------------------------------
# Test 1: net_pcap on loopback interface (lo)
# ------------------------------------------------------------------
run_lo() {
    need_root
    echo "========================================"
    echo "[TEST] net_pcap on loopback (lo)"
    echo "========================================"
    echo "[HINT] In another terminal, run:  ping 127.0.0.1"
    echo "[HINT] You should see packet stats in this terminal."
    echo ""
    ${SUDO} "${APP}" -l 0 \
        --no-huge -m 256 \
        --vdev=net_pcap0,iface=lo \
        --log-level=6
}

# ------------------------------------------------------------------
# Test 2: net_pcap with pcap files (no root needed if files exist)
# ------------------------------------------------------------------
run_pcap_file() {
    local RX_PCAP="${SCRIPT_DIR}/sample_in.pcap"
    local TX_PCAP="${SCRIPT_DIR}/sample_out.pcap"

    # Create a minimal pcap if not exists using tcpdump or scapy
    if [ ! -f "${RX_PCAP}" ]; then
        echo "[INFO] Creating sample pcap: ${RX_PCAP}"
        # Try to create via ping + tcpdump, or use a simple Python script
        if command -v python3 &>/dev/null; then
            python3 -c "
import struct, time, os
# Minimal pcap header + 1 ICMP-like packet
pcap_hdr = struct.pack('<IHHIIII',
    0xa1b2c3d4,  # magic
    2, 4,        # major, minor
    0,           # thiszone
    0,           # sigfigs
    65535,       # snaplen
    1)           # network (Ethernet)
# Fake ethernet + IP + ICMP packet (64 bytes)
pkt = b'\\xff' * 6 + b'\\x00' * 6 + b'\\x08\\x00'  # eth dst/src/type
pkt += b'\\x45' + b'\\x00' * 19                   # IP header stub
pkt += b'\\x08\\x00' + b'\\x00\\x00' * 5           # ICMP stub
pkt += b'\\x00' * (64 - len(pkt))
ts_sec = int(time.time())
pkt_hdr = struct.pack('<IIII', ts_sec, 0, len(pkt), len(pkt))
with open('${RX_PCAP}', 'wb') as f:
    f.write(pcap_hdr)
    f.write(pkt_hdr)
    f.write(pkt)
print('Created sample pcap with 1 packet')
"
        else
            echo "[WARN] No python3 found. Please create ${RX_PCAP} manually."
            exit 1
        fi
    fi

    echo "========================================"
    echo "[TEST] net_pcap reading from pcap file"
    echo "========================================"
    echo "[INFO] Input : ${RX_PCAP}"
    echo "[INFO] Output: ${TX_PCAP}"
    echo ""
    "${APP}" -l 0 \
        --no-huge -m 256 \
        --vdev="net_pcap0,rx_pcap=${RX_PCAP},tx_pcap=${TX_PCAP}" \
        --log-level=6

    echo ""
    echo "[INFO] Check output pcap: ${TX_PCAP}"
    ls -lh "${TX_PCAP}" 2>/dev/null || true
}

# ------------------------------------------------------------------
# Test 3: net_null (pure software, no I/O)
# ------------------------------------------------------------------
run_null() {
    echo "========================================"
    echo "[TEST] net_null (pure software loopback)"
    echo "========================================"
    echo "[INFO] This test uses two null devices."
    echo "       Packets sent are dropped; no real traffic."
    echo "       Useful for testing init path and hot-path CPU cycles."
    echo ""
    "${APP}" -l 0 \
        --no-huge -m 256 \
        --vdev=net_null0 \
        --vdev=net_null1 \
        --log-level=6
}

# ------------------------------------------------------------------
# Help
# ------------------------------------------------------------------
usage() {
    echo "Usage: $0 {lo|pcap|null|help}"
    echo ""
    echo "  lo    - net_pcap attached to Linux loopback (needs root, ping 127.0.0.1 to generate traffic)"
    echo "  pcap  - net_pcap reading/writing pcap files (no root, no real traffic)"
    echo "  null  - net_null devices (fastest, pure software, no traffic)"
    echo "  help  - show this message"
    echo ""
    echo "Examples:"
    echo "  sudo $0 lo"
    echo "  $0 pcap"
    echo "  $0 null"
}

# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------
case "${1:-help}" in
    lo)
        run_lo
        ;;
    pcap)
        run_pcap_file
        ;;
    null)
        run_null
        ;;
    help|--help|-h|*)
        usage
        ;;
esac
