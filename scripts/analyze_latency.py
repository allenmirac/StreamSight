#!/usr/bin/env python3
"""
StreamSight Latency Log Analyzer

Parses a JSONL latency events file and prints per-module.stage statistics:
count, avg, p50, p90, p95, p99, max, jitter, and total_time_share.

Usage:
  python3 scripts/analyze_latency.py runtime/latency_events.jsonl
  python3 scripts/analyze_latency.py runtime/latency_events.jsonl --stage ai.face_detection
  python3 scripts/analyze_latency.py runtime/latency_events.jsonl --top 10 --json
  python3 scripts/analyze_latency.py runtime/latency_events.jsonl --csv
"""

import argparse
import json
import sys
from collections import defaultdict
from statistics import mean, stdev


def percentile(sorted_vals, p):
    """Return p-th percentile (0..1) of sorted list."""
    if not sorted_vals:
        return 0.0
    idx = int(len(sorted_vals) * p)
    if idx >= len(sorted_vals):
        idx = len(sorted_vals) - 1
    return sorted_vals[idx]


def jitter(sorted_vals):
    """Frame-to-frame difference standard deviation."""
    if len(sorted_vals) < 2:
        return 0.0
    diffs = [abs(sorted_vals[i] - sorted_vals[i - 1]) for i in range(1, len(sorted_vals))]
    return stdev(diffs) if diffs else 0.0


def main():
    parser = argparse.ArgumentParser(description="StreamSight latency log analyzer")
    parser.add_argument("logfile", help="Path to JSONL latency events file")
    parser.add_argument("--stage", help="Filter by module.stage (e.g. 'ai.face_detection')")
    parser.add_argument("--top", type=int, default=0, help="Show only top N stages by total time")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--csv", action="store_true", help="Output as CSV")
    args = parser.parse_args()

    # Read and parse JSONL
    events = []
    try:
        with open(args.logfile, "r") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    ev = json.loads(line)
                except json.JSONDecodeError:
                    continue
                events.append(ev)
    except FileNotFoundError:
        print(f"Error: file not found: {args.logfile}", file=sys.stderr)
        sys.exit(1)

    if not events:
        print("No events found in log file.")
        return

    # Group by module.stage
    groups = defaultdict(list)  # key -> list of duration_us
    for ev in events:
        mod = ev.get("module", "unknown")
        stg = ev.get("stage", "unknown")
        dur = ev.get("duration_us", 0)
        key = f"{mod}.{stg}"
        groups[key].append(dur)

    # Filter
    if args.stage:
        groups = {k: v for k, v in groups.items() if k == args.stage}
        if not groups:
            print(f"No events found for stage: {args.stage}")
            return

    # Compute stats
    results = []
    total_all_us = sum(sum(v) for v in groups.values())

    for key, durations in groups.items():
        d_sorted = sorted(durations)
        total_us = sum(d_sorted)
        cnt = len(d_sorted)
        avg_ms = (total_us / cnt / 1000.0) if cnt > 0 else 0.0

        result = {
            "stage": key,
            "count": cnt,
            "avg_ms": round(avg_ms, 3),
            "p50_ms": round(percentile(d_sorted, 0.50) / 1000.0, 3),
            "p90_ms": round(percentile(d_sorted, 0.90) / 1000.0, 3),
            "p95_ms": round(percentile(d_sorted, 0.95) / 1000.0, 3),
            "p99_ms": round(percentile(d_sorted, 0.99) / 1000.0, 3),
            "max_ms": round(max(d_sorted) / 1000.0, 3),
            "jitter_ms": round(jitter(d_sorted) / 1000.0, 3),
            "total_ms": round(total_us / 1000.0, 3),
            "total_share_pct": round(total_us / total_all_us * 100.0, 1) if total_all_us > 0 else 0.0,
        }
        results.append(result)

    # Sort by total time descending
    results.sort(key=lambda r: r["total_ms"], reverse=True)

    # Top N
    if args.top > 0:
        results = results[:args.top]

    # Output
    if args.json:
        print(json.dumps(results, indent=2, ensure_ascii=False))
    elif args.csv:
        headers = ["stage", "count", "avg_ms", "p50_ms", "p90_ms", "p95_ms",
                   "p99_ms", "max_ms", "jitter_ms", "total_ms", "total_share_pct"]
        print(",".join(headers))
        for r in results:
            print(",".join(str(r[h]) for h in headers))
    else:
        # Pretty table
        print(f"{'Stage':<35} {'Count':>7} {'Avg(ms)':>9} {'P50':>9} {'P90':>9} "
              f"{'P95':>9} {'P99':>9} {'Max':>9} {'Jitter':>9} {'Share%':>7}")
        print("-" * 120)
        for r in results:
            print(f"{r['stage']:<35} {r['count']:>7} {r['avg_ms']:>9.3f} {r['p50_ms']:>9.3f} "
                  f"{r['p90_ms']:>9.3f} {r['p95_ms']:>9.3f} {r['p99_ms']:>9.3f} "
                  f"{r['max_ms']:>9.3f} {r['jitter_ms']:>9.3f} {r['total_share_pct']:>6.1f}%")

        print()
        print(f"Total events: {len(events)}")
        print(f"Total stages: {len(results)}")


if __name__ == "__main__":
    main()
