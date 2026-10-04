#!/usr/bin/env bash
#
# StreamSight net-layer microbenchmark driver.
#
# Runs streamsight-netbench once and renders its JSON into a Markdown report
# under runtime_netbench/.
#
# The benchmark itself only exercises src/net/ (EventLoop + TcpServer +
# RingBuffer) — no FFmpeg/OpenCV — so it is cheap to run and independent of
# the media pipeline.
#
# Build first (Release, for meaningful numbers):
#   cmake -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target streamsight-netbench
#
# Usage:
#   bash scripts/run_net_bench.sh [--binary PATH] [--out-dir DIR]
#                                 [--connects N] [--threads N]
#                                 [--holds "1000,5000,10000"] [--hold-secs N]
#                                 [--bw-conns N] [--bw-payload B]
#                                 [--bw-duration S] [--ring-ops N]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

BINARY=""
[ -x "build-release/bin/streamsight-netbench" ] && BINARY="build-release/bin/streamsight-netbench"
[ -z "$BINARY" ] && [ -x "build/bin/streamsight-netbench" ] && BINARY="build/bin/streamsight-netbench"

OUT_DIR="runtime_netbench"
CONNECTS=20000
THREADS=""
HOLDS="1000,5000,10000"
HOLD_SECS=10
BW_CONNS=64
BW_PAYLOAD=1400
BW_DURATION=10
RING_OPS=5000000

while [ $# -gt 0 ]; do
    case "$1" in
        --binary)      BINARY="$2"; shift 2 ;;
        --out-dir)     OUT_DIR="$2"; shift 2 ;;
        --connects)    CONNECTS="$2"; shift 2 ;;
        --threads)     THREADS="$2"; shift 2 ;;
        --holds)       HOLDS="$2"; shift 2 ;;
        --hold-secs)   HOLD_SECS="$2"; shift 2 ;;
        --bw-conns)    BW_CONNS="$2"; shift 2 ;;
        --bw-payload)  BW_PAYLOAD="$2"; shift 2 ;;
        --bw-duration) BW_DURATION="$2"; shift 2 ;;
        --ring-ops)    RING_OPS="$2"; shift 2 ;;
        *) echo "unknown arg: $1"; exit 1 ;;
    esac
done

if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    echo "[ERROR] streamsight-netbench not found."
    echo "  Build: cmake -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --target streamsight-netbench"
    exit 1
fi

mkdir -p "$OUT_DIR"
TS="$(date +%Y%m%d_%H%M%S)"
JSON="$OUT_DIR/netbench_${TS}.json"
LOG="$OUT_DIR/netbench_${TS}.log"

ARGS=(--bench all --connects "$CONNECTS" --hold "$HOLDS" --hold-secs "$HOLD_SECS"
      --bw-conns "$BW_CONNS" --bw-payload "$BW_PAYLOAD" --bw-duration "$BW_DURATION"
      --ring-ops "$RING_OPS" --json-out "$JSON")
[ -n "$THREADS" ] && ARGS+=(--threads "$THREADS")

echo "[netbench] binary=$BINARY"
echo "[netbench] connects=$CONNECTS threads=${THREADS:-1,2,4,8} holds=$HOLDS hold_secs=$HOLD_SECS"
echo "[netbench] bw: conns=$BW_CONNS payload=$BW_PAYLOAD duration=$BW_DURATION"
echo "[netbench] running..."

if ! "$BINARY" "${ARGS[@]}" > "$LOG" 2>&1; then
    echo "[ERROR] benchmark failed; see $LOG"
    tail -20 "$LOG"
    exit 1
fi

# Echo the one-line summaries so the operator sees progress without the log.
grep -E '^\[(ring|churn|conn|bw|loop)\]' "$LOG" | sed 's/^/    /'

# A Release binary is required for meaningful numbers; the JSON records the
# build type so a stray Debug run can never be mistaken for a Release one.
BUILD_TYPE="$(python3 -c "import json,sys; print(json.load(open('$JSON')).get('build','unknown'))" 2>/dev/null || echo unknown)"
case "$BUILD_TYPE" in
    Release|RelWithDebInfo|MinSizeRel) ;;
    *) echo "[WARN] binary was built as '$BUILD_TYPE'; -O2 (Release) is required for meaningful numbers." ;;
esac

# ── Aggregate to Markdown ─────────────────────────────────────────────────────
REPORT="$OUT_DIR/report_${TS}.md"
python3 - "$REPORT" "$JSON" "$CONNECTS" "$HOLDS" "$HOLD_SECS" <<'PY'
import json, os, platform, sys
from datetime import datetime

report, path, connects, holds, hold_secs = sys.argv[1:6]
with open(path) as f:
    data = json.load(f)

metrics = data.get("metrics", [])
by_name = {m["name"]: m for m in metrics}


def note(note_str, key, cast=str, default=None):
    for kv in (note_str or "").split():
        if kv.startswith(key + "="):
            return cast(kv.split("=", 1)[1])
    return default


def val(name, default=0.0):
    return by_name.get(name, {}).get("value", default)


L = []
L.append("# StreamSight net 层基准报告")
L.append("")
L.append(f"生成: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
L.append(f"构建类型: **{data.get('build', 'unknown')}**    "
         f"主机: {platform.platform()}   CPU 核数: {os.cpu_count()}")
L.append("")
L.append("说明: 仅测 `src/net/`（EventLoop + TcpServer + TcpConnection + RingBuffer），不含媒体管线。")
L.append("")

# ── 1. RingBuffer ────────────────────────────────────────────────────────────
L.append("## 一、RingBuffer 微基准")
L.append("")
L.append("| 指标 | 数值 | 单位 | 容量 |")
L.append("|------|------|------|------|")
for m in metrics:
    if m["name"].startswith("ringbuf_"):
        L.append(f"| {m['name']} | {m['value']:.2f} | {m['unit']} "
                 f"| {note(m.get('note'), 'cap')} |")
L.append("")

# ── 2. Connect churn (basic) ─────────────────────────────────────────────────
L.append("## 二、连接建立速率（基本测试）")
L.append("")
L.append("口径提醒：速率分母是**最慢的那个客户端线程**的耗时（所有线程 join 后才停表），")
L.append("因此对拖尾敏感。每线程耗时分布单独列出，用于分辨「整体变慢」与「个别线程被卡住」。")
L.append("")
L.append("| 客户端线程 | 每线程连接数 | conn/s | 客户端成功 | 服务端接受 | 丢失 | 每线程耗时 p50 / p99 / max (ms) |")
L.append("|-----------|-------------|--------|-----------|-----------|------|-------------------------------|")
for m in metrics:
    if not m["name"].startswith("tcp_accept_") or "_thread_" in m["name"]:
        continue
    tag = m["name"]
    n = m.get("note", "")
    L.append(f"| {note(n, 'threads')} | {note(n, 'per_thread')} "
             f"| {m['value']:,.0f} | {note(n, 'clients_ok')} | {note(n, 'accepted')} "
             f"| {note(n, 'lost')} "
             f"| {val(tag + '_thread_p50_ms'):.1f} / {val(tag + '_thread_p99_ms'):.1f} "
             f"/ {val(tag + '_thread_max_ms'):.1f} |")
L.append("")

# ── 3. Concurrent connections ────────────────────────────────────────────────
L.append("## 三、并发用户数量")
L.append("")
L.append(f"每条连接保持 **{hold_secs} 秒**后探测存活性（MSG_PEEK）。")
L.append("")
L.append("| 请求连接数 | 实际打开 | 服务端接受 | 保持后存活 | fd 数 | RSS(kB) | 每连接(kB) | 备注 |")
L.append("|-----------|---------|-----------|-----------|-------|---------|-----------|------|")
for m in metrics:
    if not m["name"].endswith("_opened") or not m["name"].startswith("conn_"):
        continue
    tag = m["name"][: -len("_opened")]
    opened = val(tag + "_opened")
    req = note(by_name.get(tag + "_opened", {}).get("note", ""), "requested")
    aborted = note(by_name.get(tag + "_opened", {}).get("note", ""), "aborted") == "1"
    L.append(f"| {req} | {opened:,.0f} | {val(tag + '_accepted'):,.0f} "
             f"| {val(tag + '_alive_end'):,.0f} | {val(tag + '_fd_count'):,.0f} "
             f"| {val(tag + '_rss_kb'):,.0f} | {val(tag + '_rss_per_conn_kb'):.2f} "
             f"| {'**连接失败，已中止**' if aborted else ''} |")
L.append("")

# ── 4. Throughput ────────────────────────────────────────────────────────────
L.append("## 四、吞吐量 / 带宽（服务端发送）")
L.append("")
L.append("口径提醒：主指标是**客户端实收**（客户端 drain 后核对），不是交给 `Send()` 的字节。")
L.append("`Send()` 忽略 `BufferWriter::Append()` 的失败返回值，写队列满（500 包）时静默丢数据，")
L.append("因此「已提交」只是上界，「未送达」列才是背压信号。")
L.append("")
L.append("| 连接数 | payload(B) | tick(ms) | 聚合 rx(MB/s) | 每连接(MB/s) | tx(MB/s) | 未送达(B) | RSS(kB) |")
L.append("|-------|-----------|----------|--------------|-------------|----------|-----------|---------|")
bw = by_name.get("bw_rx_mbps")
if bw:
    n = bw.get("note", "")
    L.append(f"| {note(n, 'conns')} | {note(n, 'payload')} | {note(n, 'tick_ms')} "
             f"| {bw['value']:.2f} | {val('bw_rx_bytes_per_conn'):.2f} "
             f"| {val('bw_tx_mbps'):.2f} | {val('bw_undelivered_bytes'):,.0f} "
             f"| {val('bw_rss_kb'):,.0f} |")
else:
    L.append("| — | — | — | — | — | — | — | — |")
L.append("")

# ── 5. EventLoop ─────────────────────────────────────────────────────────────
L.append("## 五、EventLoop")
L.append("")
L.append("| 指标 | 数值 | 单位 | 备注 |")
L.append("|------|------|------|------|")
for m in metrics:
    if m["name"].startswith("eventloop_"):
        L.append(f"| {m['name']} | {m['value']:.1f} | {m['unit']} | {m.get('note','')} |")
L.append("")
L.append(f"进程峰值 RSS: {data.get('peak_rss_kb', 0):,} kB")
L.append("")
L.append("## 原始数据")
L.append("")
L.append(f"- `{path}`")
L.append("")

text = "\n".join(L)
with open(report, "w") as f:
    f.write(text)
print(text)
print(f"\n报告: {report}")
PY
