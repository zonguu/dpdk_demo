#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Parse arguments
ENABLE_ASAN=OFF
ENABLE_PIPELINE=OFF
ENABLE_MULTICORE=OFF
ENABLE_L2FWD=OFF
ENABLE_PCAP_DUMP=OFF
ENABLE_ACL_RATELIMIT=ON
ENABLE_PKT_MODIFY=ON
for arg in "$@"; do
    case $arg in
        --asan)
            ENABLE_ASAN=ON
            shift
            ;;
        --pipeline)
            ENABLE_PIPELINE=ON
            shift
            ;;
        --multicore)
            ENABLE_MULTICORE=ON
            shift
            ;;
        --l2fwd)
            ENABLE_L2FWD=ON
            shift
            ;;
        --pcap-dump)
            ENABLE_PCAP_DUMP=ON
            shift
            ;;
        --no-acl-ratelimit)
            ENABLE_ACL_RATELIMIT=OFF
            shift
            ;;
        --no-pkt-modify)
            ENABLE_PKT_MODIFY=OFF
            shift
            ;;
    esac
done

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure and build
echo "[INFO] Configuring with CMake... (ASan=${ENABLE_ASAN}, Pipeline=${ENABLE_PIPELINE}, Multicore=${ENABLE_MULTICORE}, L2FWD=${ENABLE_L2FWD}, PCAP=${ENABLE_PCAP_DUMP}, ACL_Ratelimit=${ENABLE_ACL_RATELIMIT}, PktModify=${ENABLE_PKT_MODIFY})"
cmake .. -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DENABLE_PIPELINE=${ENABLE_PIPELINE} \
    -DENABLE_MULTICORE=${ENABLE_MULTICORE} \
    -DENABLE_L2FWD=${ENABLE_L2FWD} \
    -DENABLE_PCAP_DUMP=${ENABLE_PCAP_DUMP} \
    -DENABLE_ACL_RATELIMIT=${ENABLE_ACL_RATELIMIT} \
    -DENABLE_PKT_MODIFY=${ENABLE_PKT_MODIFY}

echo "[INFO] Building..."
make -j$(nproc)

echo "[INFO] Build complete: ${BUILD_DIR}/dpdk_pcap_demo"
