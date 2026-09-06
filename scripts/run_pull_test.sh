#!/usr/bin/env bash
# 复现：serial 模式 + AI + N 路 ffplay 拉流延迟测试
# 用法：bash scripts/run_pull_test.sh [拉流路数] [运行秒数]
# 例：  bash scripts/run_pull_test.sh 10 40
set -u

PULLERS="${1:-10}"
DURATION="${2:-40}"
PORT=8554
OUT_DIR="runtime_pull"
mkdir -p "$OUT_DIR"

LATENCY_LOG="$OUT_DIR/latency_events.jsonl"
rm -f "$LATENCY_LOG"

export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG="$LATENCY_LOG"
export STREAMSIGHT_LATENCY_BUFFER_SIZE=10000

echo "[pull-test] 启动 ffmpeg_streamer (serial + AI, analyze-fps=5) ..."
./build/bin/ffmpeg_streamer --input pic/test.h264 --port $PORT \
    --http-port 8080 --analyze-fps 5 --suffix live \
    > "$OUT_DIR/server.log" 2>&1 &
SERVER_PID=$!

# 等待服务就绪
sleep 3

echo "[pull-test] 启动 $PULLERS 路 ffplay 拉流 ..."
for i in $(seq 1 "$PULLERS"); do
    ffplay -loglevel quiet -fflags nobuffer -flags low_delay \
        -framedrop -autoexit -t "$DURATION" \
        "rtsp://127.0.0.1:$PORT/live" \
        > "$OUT_DIR/ffplay_$i.log" 2>&1 &
done

# 等待拉流结束
sleep "$DURATION"
sleep 3

# 停服务
kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

echo "[pull-test] 完成。分析延迟日志："
python3 scripts/analyze_latency.py "$LATENCY_LOG"
echo ""
echo "服务日志尾部："
tail -20 "$OUT_DIR/server.log"
