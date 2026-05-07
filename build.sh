#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Parse arguments
ENABLE_ASAN=OFF
ENABLE_PIPELINE=OFF
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
    esac
done

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure and build
echo "[INFO] Configuring with CMake... (ASan=${ENABLE_ASAN}, Pipeline=${ENABLE_PIPELINE})"
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=${ENABLE_ASAN} -DENABLE_PIPELINE=${ENABLE_PIPELINE}

echo "[INFO] Building..."
make -j$(nproc)

echo "[INFO] Build complete: ${BUILD_DIR}/dpdk_pcap_demo"
