#!/usr/bin/env bash
# Build and run the Tutti server with WebTransport enabled.
# Uses sudo to get RT thread priority (SCHED_FIFO) for mixer threads.
set -euo pipefail

cd "$(dirname "$0")/server"

BUILD=true
RUN=true
SERVER_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --run-only)  BUILD=false ;;
        --build-only) RUN=false ;;
        --help|-h)
            echo "Usage: dev.sh [--run-only | --build-only] [-- server args...]"
            echo "  --run-only   Skip build, just run the server"
            echo "  --build-only Build only, don't run"
            echo ""
            echo "Server args (passed after --):"
            echo "  --bind <addr>  --http-port <port>  --ws-port <port>"
            echo "  --wt-port <port>  --max-participants <n>"
            exit 0
            ;;
        --)
            shift
            SERVER_ARGS=("$@")
            break
            ;;
        *)
            echo "Unknown option: $arg (use -- to pass args to server)" >&2
            exit 1
            ;;
    esac
    shift
done

if $BUILD; then
    echo "==> Configuring (WebTransport ON)..."
    cmake -B build -DTUTTI_ENABLE_WEBTRANSPORT=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -Wno-dev 2>&1 \
        | tail -1
    echo "==> Building..."
    cmake --build build -j"$(nproc)"
    echo "==> Build complete."
fi

if $RUN; then
    echo "==> Starting server (sudo for RT priority)..."
    exec sudo ./build/tutti-server "${SERVER_ARGS[@]}"
fi
