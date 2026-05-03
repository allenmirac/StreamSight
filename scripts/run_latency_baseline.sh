#!/usr/bin/env bash
#
# StreamSight Latency Baseline Test
#
# Sets up environment, starts the analysis server with latency tracing enabled,
# and prints commands for client testing and log analysis.
#
# Usage:
#   bash scripts/run_latency_baseline.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"

echo "=== StreamSight Latency Baseline Test ==="
echo ""

# ── Check binaries ──────────────────────────────────────────────────────────────
SERVER_BIN=""
if [ -x "bin/rtsp_analysis_server" ]; then
    SERVER_BIN="bin/rtsp_analysis_server"
elif [ -x "build/bin/rtsp_analysis_server" ]; then
    SERVER_BIN="build/bin/rtsp_analysis_server"
fi

if [ -z "$SERVER_BIN" ]; then
    echo "[ERROR] rtsp_analysis_server not found."
    echo "  Build first: make -j\$(nproc)"
    echo "  Or with CMake: mkdir -p build && cd build && cmake .. && make -j\$(nproc)"
    exit 1
fi

# ── Check test media ────────────────────────────────────────────────────────────
TEST_FILE=""
if [ -f "pic/test.h264" ]; then
    TEST_FILE="pic/test.h264"
elif [ -f "pic/test.mp4" ]; then
    TEST_FILE="pic/test.mp4"
elif [ -f "test.h264" ]; then
    TEST_FILE="test.h264"
fi

if [ -z "$TEST_FILE" ]; then
    echo "[WARN] No test media found (pic/test.h264, pic/test.mp4)."
    echo "  Using --source camera as fallback."
    SOURCE_TYPE="camera"
    INPUT_ARG=""
else
    SOURCE_TYPE="file"
    INPUT_ARG="--input $TEST_FILE"
    echo "[INFO] Test media: $TEST_FILE"
fi

# ── Create runtime directory ────────────────────────────────────────────────────
mkdir -p runtime

# ── Environment variables ───────────────────────────────────────────────────────
export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG=runtime/latency_events.jsonl
export STREAMSIGHT_LATENCY_BUFFER_SIZE=10000

echo "[INFO] Environment:"
echo "  STREAMSIGHT_LATENCY_ENABLE=$STREAMSIGHT_LATENCY_ENABLE"
echo "  STREAMSIGHT_LATENCY_LOG=$STREAMSIGHT_LATENCY_LOG"
echo "  STREAMSIGHT_LATENCY_BUFFER_SIZE=$STREAMSIGHT_LATENCY_BUFFER_SIZE"
echo ""

# ── Start server ────────────────────────────────────────────────────────────────
echo "[INFO] Starting rtsp_analysis_server (press Ctrl+C to stop)..."
echo ""

if [ "$SOURCE_TYPE" = "file" ]; then
    echo "Command: $SERVER_BIN --source file $INPUT_ARG --port 8554 --http-port 8080 --no-ai --suffix live"
    echo ""
    $SERVER_BIN --source file $INPUT_ARG --port 8554 --http-port 8080 --no-ai --suffix live &
else
    echo "Command: $SERVER_BIN --source camera --device 0 --port 8554 --http-port 8080 --no-ai --suffix live"
    echo ""
    $SERVER_BIN --source camera --device 0 --port 8554 --http-port 8080 --no-ai --suffix live &
fi

SERVER_PID=$!
echo "[INFO] Server PID: $SERVER_PID"

# Wait for server to start
sleep 3

# ── Test instructions ──────────────────────────────────────────────────────────
echo ""
echo "=== Server is running ==="
echo ""
echo "Test commands (run in another terminal):"
echo ""
echo "  # RTSP playback (low latency):"
echo "  ffplay -fflags nobuffer -flags low_delay -framedrop rtsp://127.0.0.1:8554/live"
echo ""
echo "  # HTTP API queries:"
echo "  curl -s http://localhost:8080/api/status"
echo "  curl -s http://localhost:8080/api/latency/stats"
echo "  curl -s http://localhost:8080/api/latency/recent?limit=10"
echo ""
echo "  # After collecting data, stop the server with:"
echo "  kill $SERVER_PID"
echo ""
echo "  # Analyze latency logs:"
echo "  python3 scripts/analyze_latency.py runtime/latency_events.jsonl"
echo ""

# Wait for server to be killed
wait $SERVER_PID 2>/dev/null || true

echo ""
echo "=== Server stopped ==="

# ── Analyze logs ───────────────────────────────────────────────────────────────
if [ -f "runtime/latency_events.jsonl" ]; then
    EVENT_COUNT=$(wc -l < "runtime/latency_events.jsonl")
    echo ""
    echo "=== Latency log generated: runtime/latency_events.jsonl ($EVENT_COUNT events) ==="
    echo ""

    if [ -x "$(which python3 2>/dev/null)" ] || [ -f "$PROJECT_DIR/scripts/analyze_latency.py" ]; then
        python3 "$PROJECT_DIR/scripts/analyze_latency.py" runtime/latency_events.jsonl
    else
        echo "[WARN] python3 not found, skipping analysis."
        echo "  Install python3 and run: python3 scripts/analyze_latency.py runtime/latency_events.jsonl"
    fi
else
    echo ""
    echo "[WARN] No latency log generated. Check that STREAMSIGHT_LATENCY_ENABLE=1 was set."
fi
