#!/usr/bin/env python3
"""从 pidstat 日志（-u -r 混合模式）中正确重解析 CPU% 与 RSS。

pidstat -u -r 输出 CPU 行与内存行交替，字段位置不同：
  CPU 行:  time UID PID %usr %system %guest %wait %CPU CPU Command   (10 列)
  内存行:  time UID PID minflt/s majflt/s VSZ RSS %MEM Command        (9 列)
本脚本按 Command 列 = "streamsight-str" 且列数 >= 9 识别，
区分两类行分别取 %CPU（CPU 行）与 RSS KB（内存行）。
"""
import sys
import glob
import os


def parse_file(path):
    cpu_vals, rss_vals = [], []
    with open(path) as f:
        for line in f:
            if "Command" in line or "Linux" in line or "UID" in line or not line.strip():
                continue
            parts = line.split()
            if "streamsight" not in parts[-1]:
                continue
            try:
                if len(parts) == 10:
                    # CPU 行: time UID PID %usr %sys %guest %wait %CPU CPU Command
                    cpu_vals.append(float(parts[7]))
                elif len(parts) == 9:
                    # 内存行: time UID PID minflt/s majflt/s VSZ RSS %MEM Command
                    rss_vals.append(float(parts[6]))
            except ValueError:
                continue
    avg_cpu = sum(cpu_vals) / len(cpu_vals) if cpu_vals else None
    peak_rss = max(rss_vals) if rss_vals else None
    return avg_cpu, peak_rss


def main():
    if len(sys.argv) > 1:
        paths = sys.argv[1:]
    else:
        paths = sorted(glob.glob("runtime_perf/pidstat_*.log"))
    print(f"{'label':<28} {'CPU%':>8} {'RSS_KB':>10} {'RSS_MB':>8}")
    print("-" * 60)
    for p in paths:
        base = os.path.basename(p).replace("pidstat_", "").replace(".log", "")
        avg_cpu, peak_rss = parse_file(p)
        rss_mb = (peak_rss / 1024.0) if peak_rss else 0
        print(f"{base:<28} {avg_cpu if avg_cpu else 0:>8.1f} "
              f"{peak_rss if peak_rss else 0:>10.0f} {rss_mb:>8.1f}")


if __name__ == "__main__":
    main()
