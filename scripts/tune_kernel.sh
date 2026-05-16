#!/bin/bash
# tune_kernel.sh — SRS-inspired Linux kernel tuning for low-latency streaming
# Run as root: sudo ./scripts/tune_kernel.sh
#
# These settings reduce UDP/TCP receive buffer pressure, increase connection
# backlog, and enable TCP optimizations for low-latency RTP media transport.

set -e

echo "[tune_kernel] Applying low-latency streaming kernel parameters..."

# ── Socket buffer sizes ────────────────────────────────────────────────
# Increase max receive/send buffer to 16 MB (default ~208 KB).
# Larger buffers absorb bursts and reduce kernel drops.
sysctl -w net.core.rmem_max=16777216
sysctl -w net.core.wmem_max=16777216
sysctl -w net.core.rmem_default=262144
sysctl -w net.core.wmem_default=262144

# ── Connection backlog ──────────────────────────────────────────────────
# Allow more pending connections before the kernel starts dropping SYNs.
sysctl -w net.core.somaxconn=8192

# ── TCP Fast Open ───────────────────────────────────────────────────────
# 3 = enable TFO for both client and server. Reduces 1 RTT on reconnect.
sysctl -w net.ipv4.tcp_fastopen=3

# ── TIME_WAIT reuse ─────────────────────────────────────────────────────
# Allow reusing sockets in TIME_WAIT for new connections (safe on Linux).
sysctl -w net.ipv4.tcp_tw_reuse=1

# ── TCP keepalive ───────────────────────────────────────────────────────
# Detect dead connections faster (idle 60s → probe every 10s, 3 probes).
sysctl -w net.ipv4.tcp_keepalive_time=60
sysctl -w net.ipv4.tcp_keepalive_intvl=10
sysctl -w net.ipv4.tcp_keepalive_probes=3

# ── Reduce TCP orphan retries ───────────────────────────────────────────
# Limit retransmission of TCP orphans to free resources faster.
sysctl -w net.ipv4.tcp_orphan_retries=1

echo "[tune_kernel] Done. Verify with:"
echo "  ss -tlnp                          # listening sockets"
echo "  netstat -su                       # UDP receive errors"
echo "  cat /proc/sys/net/core/rmem_max   # current rmem_max"
echo ""
echo "To make permanent, add these lines to /etc/sysctl.conf"
