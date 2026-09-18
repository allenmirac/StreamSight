#!/usr/bin/env python3
"""StreamSight 完整性能矩阵编排脚本。

驱动 streamsight-stress 跑 16 轮压测：
  pipeline_mode ∈ {serial, parallel} × concurrency ∈ {1,4,8,16} × ai ∈ {off,on}

每轮：预热 warmup 秒 → 采集 duration 秒（每秒对全部 session 调 GetStatus），
同时用 pidstat 独立采集被测进程的系统级 CPU% 与 RSS 峰值。
输出：每轮一个 JSON（含 latency_tracer 百分位 + per-session 时间序列 + 聚合指标）
      + 一个 pidstat 日志，最后汇总为 Markdown 报告。

用法：
  python3 scripts/run_perf_matrix.py --binary ./build/bin/streamsight-stress \
      --input pic/perf_1080p30.mp4 --out-dir runtime_perf \
      --duration 60 --warmup 10
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime

BINARY = "./build/bin/streamsight-stress"
INPUT = "pic/perf_1080p30.mp4"
OUT_DIR = "runtime_perf"
DURATION = 60
WARMUP = 10
WIDTH, HEIGHT, FPS, BITRATE = 1920, 1080, 30, 2000000


def run_one(binary, mode, count, ai, input_file, out_dir, duration, warmup):
    label = f"n{count}_{mode}_{'ai' if ai else 'noai'}"
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_out = os.path.join(out_dir, f"perf_{label}_{ts}.json")
    pidstat_out = os.path.join(out_dir, f"pidstat_{label}_{ts}.log")

    cmd = [
        binary,
        "--count", str(count), "--mode", mode,
        "--duration", str(duration), "--warmup", str(warmup),
        "--base-port", str(8554 + (count * 10)),
        "--input", input_file,
        "--width", str(WIDTH), "--height", str(HEIGHT),
        "--fps", str(FPS), "--bitrate", str(BITRATE),
        "--json-out", json_out,
    ]
    if ai:
        cmd.append("--enable-ai")

    print(f"\n{'=' * 60}\nRunning: {label}\n  {' '.join(cmd)}\n{'=' * 60}",
          flush=True)

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pid = proc.pid

    pidstat_proc = None
    if shutil.which("pidstat"):
        # 只采集 CPU%（-u）；内存 RSS 用 stress_tester 内部读的
        # /proc/self/status VmRSS（memory_peak_rss_kb 字段），更权威。
        # 若同时加 -r，输出会 CPU/内存行交替，字段位置不同易误解析。
        pidstat_cmd = ["pidstat", "-p", str(pid), "-u", "1"]
        with open(pidstat_out, "w") as f:
            pidstat_proc = subprocess.Popen(
                pidstat_cmd, stdout=f, stderr=subprocess.DEVNULL)

    try:
        stdout, _ = proc.communicate(timeout=duration + warmup + 60)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, _ = proc.communicate()
        print(f"ERROR: {label} timed out", flush=True)
        if pidstat_proc:
            pidstat_proc.terminate(); pidstat_proc.wait(timeout=5)
        return None

    if pidstat_proc:
        pidstat_proc.terminate(); pidstat_proc.wait(timeout=5)

    if proc.returncode != 0:
        tail = (stdout or b"").decode(errors="replace")[-800:]
        print(f"ERROR: {label} exit={proc.returncode}\n{tail}", flush=True)
        return None

    try:
        with open(json_out) as f:
            results = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError) as e:
        print(f"ERROR: {label} bad json: {e}", flush=True)
        return None

    # 解析 pidstat：平均 CPU%（仅 -u，%CPU 在第 8 列，即 parts[7]）
    cpu_pct = None
    if os.path.isfile(pidstat_out):
        cpus = []
        with open(pidstat_out) as f:
            for line in f:
                if "Command" in line or "Linux" in line or "UID" in line or not line.strip():
                    continue
                parts = line.split()
                # CPU 行：time UID PID %usr %sys %guest %wait %CPU CPU Command
                if len(parts) >= 9:
                    try:
                        cpus.append(float(parts[7]))
                    except ValueError:
                        continue
        if cpus:
            cpu_pct = sum(cpus) / len(cpus)

    # RSS 用 stress_tester 内部读的 VmRSS（权威），pidstat 只作 CPU 参考。
    results["_meta"] = {
        "label": label, "concurrency": count, "mode": mode,
        "ai_enabled": ai, "cpu_pct_avg": cpu_pct, "timestamp": ts,
    }
    print(f"OK: {label} cpu%={cpu_pct}", flush=True)
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=BINARY)
    ap.add_argument("--input", default=INPUT)
    ap.add_argument("--out-dir", default=OUT_DIR)
    ap.add_argument("--duration", type=int, default=DURATION)
    ap.add_argument("--warmup", type=int, default=WARMUP)
    ap.add_argument("--concurrency", default="1,4,8,16")
    ap.add_argument("--modes", default="serial,parallel")
    ap.add_argument("--ai", default="0,1")
    args = ap.parse_args()

    if not os.path.isfile(args.binary):
        sys.exit(f"binary not found: {args.binary}")
    if not os.path.isfile(args.input):
        sys.exit(f"input not found: {args.input}")
    if shutil.which("pidstat") is None:
        print("WARN: pidstat not found, CPU/mem skipped", flush=True)

    os.makedirs(args.out_dir, exist_ok=True)

    all_results = []
    counts = [int(x) for x in args.concurrency.split(",")]
    modes = args.modes.split(",")
    ai_flags = [bool(int(x)) for x in args.ai.split(",")]

    for mode in modes:
        for count in counts:
            for ai in ai_flags:
                r = run_one(args.binary, mode, count, ai, args.input,
                            args.out_dir, args.duration, args.warmup)
                all_results.append(r)
                time.sleep(2)

    # 汇总报告
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    rep = os.path.join(args.out_dir, f"report_{ts}.md")
    lines = ["# StreamSight 性能矩阵测试报告",
             f"生成: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
             f"二进制: {args.binary}  输入: {args.input}",
             f"每轮: warmup={args.warmup}s + duration={args.duration}s",
             "", "| 场景 | 路数 | 模式 | AI | 单路FPS | 总FPS | 丢帧% | "
             "pipeline.full P50(ms) | P99(ms) | CPU% | RSS(MB) |",
             "|------|------|------|----|---------|-------|-------|"
             "----------------------|---------|------|---------|"]
    for r in all_results:
        if not r:
            continue
        m = r.get("_meta", {})
        agg = r.get("aggregate", {})
        lat = r.get("latency_tracer", {})
        p50 = p99 = 0.0
        if "pipeline.full" in lat:
            p50 = lat["pipeline.full"].get("p50_us", 0) / 1000.0
            p99 = lat["pipeline.full"].get("p99_us", 0) / 1000.0
        mem = r.get("memory_peak_rss_kb", 0) / 1024
        lines.append(
            f"| {m.get('label')} | {m.get('concurrency')} | {m.get('mode')} "
            f"| {m.get('ai_enabled')} | {agg.get('avg_fps_per_stream', 0):.1f} "
            f"| {agg.get('total_frames_decoded', 0) / max(1, r.get('actual_duration_sec', 1)):.0f} "
            f"| {agg.get('drop_rate_pct', 0):.2f}% | {p50:.1f} | {p99:.1f} "
            f"| {m.get('cpu_pct_avg')} | {mem:.0f} ")
    raw = os.path.join(args.out_dir, f"all_results_{ts}.json")
    with open(raw, "w") as f:
        json.dump([r for r in all_results if r], f, indent=2, default=str)
    lines += ["", f"原始数据: `{raw}`", ""]
    with open(rep, "w") as f:
        f.write("\n".join(lines))
    print(f"\n报告: {rep}\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
