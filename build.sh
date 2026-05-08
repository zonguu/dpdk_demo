#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Parse arguments
ENABLE_ASAN=OFF
ENABLE_PIPELINE=OFF
ENABLE_MULTICORE=OFF
ENABLE_L2FWD=OFF
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
    esac
done

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure and build
echo "[INFO] Configuring with CMake... (ASan=${ENABLE_ASAN}, Pipeline=${ENABLE_PIPELINE}, Multicore=${ENABLE_MULTICORE}, L2FWD=${ENABLE_L2FWD})"
cmake .. -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DENABLE_PIPELINE=${ENABLE_PIPELINE} \
    -DENABLE_MULTICORE=${ENABLE_MULTICORE} \
    -DENABLE_L2FWD=${ENABLE_L2FWD}

echo "[INFO] Building..."
make -j$(nproc)

echo "[INFO] Build complete: ${BUILD_DIR}/dpdk_pcap_demo"
