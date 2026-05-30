#!/usr/bin/env python3
"""StreamSight stress test orchestrator.

Drives streamsight-stress binary across a test matrix,
collects system-level metrics via pidstat, and generates
comparative reports.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import tempfile
import shutil
from datetime import datetime

try:
    import yaml
except ImportError:
    print("Error: pyyaml required. Install with: pip install pyyaml")
    sys.exit(1)


def check_env(config):
    """Verify binary and tools are available."""
    binary = config["binary"]
    if not os.path.isfile(binary):
        sys.exit(f"Binary not found: {binary}")

    if shutil.which("pidstat") is None:
        print("Warning: pidstat not found. CPU/memory stats will be skipped.")
    if shutil.which("ffmpeg") is None:
        sys.exit("ffmpeg not found in PATH. Required for test video generation.")


def generate_test_video(config):
    """Generate a synthetic test video if no file is provided."""
    test_file = config.get("test_file", "")
    if test_file and os.path.isfile(test_file):
        print(f"Using existing test file: {test_file}")
        return test_file

    spec = config["video_spec"]
    duration = config.get("duration_sec", 60) + config.get("warmup_sec", 10) + 30
    tmpfile = tempfile.mktemp(suffix=".mp4", prefix="stress_video_")
    cmd = [
        "ffmpeg", "-f", "lavfi",
        "-i", f"testsrc2=size={spec['width']}x{spec['height']}:rate={spec['fps']}",
        "-t", str(duration),
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-pix_fmt", "yuv420p",
        "-an",
        tmpfile, "-y"
    ]
    print(f"Generating test video ({duration}s)...")
    subprocess.run(cmd, check=True, capture_output=True)
    print(f"Generated: {tmpfile}")
    return tmpfile


def run_stress_test(config, concurrency, mode, ai, test_file, output_dir):
    """Run a single stress test configuration."""
    spec = config["video_spec"]
    base_port = config["base_port"]
    duration = config["duration_sec"]
    warmup = config["warmup_sec"]

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    label = f"n{concurrency}_{mode}_{'ai' if ai else 'noai'}"
    json_out = os.path.join(output_dir, f"stress_{label}_{timestamp}.json")
    pidstat_out = os.path.join(output_dir, f"pidstat_{label}_{timestamp}.log")

    binary = config["binary"]
    cmd = [
        binary,
        "--count", str(concurrency),
        "--mode", mode,
        "--duration", str(duration),
        "--warmup", str(warmup),
        "--base-port", str(base_port + (concurrency * 10)),
        "--input", test_file,
        "--width", str(spec["width"]),
        "--height", str(spec["height"]),
        "--fps", str(spec["fps"]),
        "--bitrate", str(spec["bitrate"]),
        "--json-out", json_out,
    ]
    if ai:
        cmd.append("--enable-ai")

    print(f"\n{'='*60}")
    print(f"Running: {label}")
    print(f"Command: {' '.join(cmd)}")
    print(f"{'='*60}")

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pid = proc.pid

    pidstat_proc = None
    if shutil.which("pidstat"):
        pidstat_cmd = [
            "pidstat", "-p", str(pid), "-u", "-r", "-w", "1",
        ]
        with open(pidstat_out, "w") as f:
            pidstat_proc = subprocess.Popen(pidstat_cmd, stdout=f, stderr=subprocess.DEVNULL)

    stdout, stderr = proc.communicate()

    if pidstat_proc:
        pidstat_proc.terminate()
        pidstat_proc.wait(timeout=5)

    if proc.returncode != 0:
        print(f"ERROR: stress test failed (exit={proc.returncode})")
        print(stderr.decode()[-1000:])
        return None

    try:
        with open(json_out) as f:
            results = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError) as e:
        print(f"ERROR: failed to read results: {e}")
        return None

    cpu_pct = None
    mem_kb = None
    if os.path.isfile(pidstat_out):
        with open(pidstat_out) as f:
            lines = f.readlines()
        cpu_vals = []
        mem_vals = []
        for line in lines:
            if "Command" in line or "Linux" in line or "UID" in line or not line.strip():
                continue
            parts = line.split()
            if len(parts) >= 9:
                try:
                    cpu_vals.append(float(parts[7]))
                    mem_vals.append(float(parts[8]))
                except (ValueError, IndexError):
                    continue
        if cpu_vals:
            cpu_pct = sum(cpu_vals) / len(cpu_vals)
        if mem_vals:
            mem_kb = max(mem_vals)

    results["_meta"] = {
        "label": label,
        "concurrency": concurrency,
        "mode": mode,
        "ai_enabled": ai,
        "cpu_pct_avg": cpu_pct,
        "mem_rss_kb_peak": mem_kb,
        "timestamp": timestamp,
    }
    return results


def generate_report(all_results, output_dir):
    """Generate Markdown comparison report."""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_path = os.path.join(output_dir, f"stress_report_{timestamp}.md")

    lines = []
    lines.append("# StreamSight Stress Test Report")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append("")

    lines.append("## Results Summary")
    lines.append("")
    lines.append("| Test | Streams | Mode | AI | Avg FPS/Stream | Total FPS | Drop% | P50 Lat(ms) | P95 Lat(ms) | CPU% | Mem Peak(MB) |")
    lines.append("|------|---------|------|----|----------------|-----------|-------|-------------|-------------|------|-------------|")

    for r in all_results:
        if r is None:
            continue
        meta = r.get("_meta", {})
        agg = r.get("aggregate", {})
        lat = r.get("latency_tracer", {})
        lat_p50 = lat_p95 = 0
        for k, v in lat.items():
            if "p50_us" in v:
                lat_p50 = v["p50_us"] / 1000.0
                lat_p95 = v.get("p95_us", 0) / 1000.0
                break

        lines.append(
            f"| {meta.get('label','?')} "
            f"| {meta.get('concurrency','?')} "
            f"| {meta.get('mode','?')} "
            f"| {meta.get('ai_enabled',False)} "
            f"| {agg.get('avg_fps_per_stream',0):.1f} "
            f"| {agg.get('total_frames_decoded',0)/(r.get('actual_duration_sec',1)):.0f} "
            f"| {agg.get('drop_rate_pct',0):.2f}% "
            f"| {lat_p50:.2f} "
            f"| {lat_p95:.2f} "
            f"| {meta.get('cpu_pct_avg','N/A')} "
            f"| {meta.get('mem_rss_kb_peak',0)/1024:.0f} "
        )

    lines.append("")

    raw_path = os.path.join(output_dir, f"stress_results_{timestamp}.json")
    with open(raw_path, "w") as f:
        json.dump(all_results, f, indent=2, default=str)

    lines.append(f"Raw data: `{raw_path}`")
    lines.append("")

    with open(report_path, "w") as f:
        f.write("\n".join(lines))

    print(f"\nReport saved to: {report_path}")
    print("\n".join(lines))
    return report_path


def main():
    parser = argparse.ArgumentParser(description="StreamSight Stress Test Orchestrator")
    parser.add_argument("--config", default="scripts/stress_config.yaml",
                        help="YAML config file path")
    parser.add_argument("--test-file", default="",
                        help="Pre-generated test video file")
    args = parser.parse_args()

    with open(args.config) as f:
        config = yaml.safe_load(f)

    if args.test_file:
        config["test_file"] = args.test_file

    check_env(config)

    os.makedirs(config.get("output_dir", "./stress_results"), exist_ok=True)
    output_dir = config["output_dir"]

    test_file = config["test_file"]
    if not test_file:
        test_file = generate_test_video(config)

    all_results = []
    for concurrency in config.get("concurrency", [1]):
        for mode in config.get("pipeline_mode", ["parallel"]):
            for ai in config.get("ai_enabled", [False]):
                result = run_stress_test(
                    config, concurrency, mode, ai, test_file, output_dir
                )
                all_results.append(result)
                time.sleep(2)

    generate_report(all_results, output_dir)

    if not config.get("test_file") and test_file:
        try:
            os.unlink(test_file)
            print(f"Cleaned up: {test_file}")
        except OSError:
            pass

    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
