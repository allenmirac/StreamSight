# RTMP Distribution Guide

## Architecture

StreamSight's RtmpOutputAdapter is an RTMP Push Client. It pushes the processed
stream (with AI overlays) to an external RTMP Server for distribution.

```
OBS/FFmpeg → StreamSight (process) → RTMP Push → SRS/nginx-rtmp → players
                           │
                           └── RTSP output (direct play via ffplay/VLC)
```

## Why "Connection refused"?

```
ffplay rtmp://localhost:8888/live/test
rtmp://localhost:8888/live/test: Connection refused
```

This error means no RTMP Server is listening on localhost:8888.
StreamSight provides only the RTMP Push Client, not an RTMP Server.
To watch via RTMP, start an external RTMP Server first.

## Quick Start with SRS

```bash
# Start SRS
docker-compose up -d srs

# Run StreamSight with RTMP push
./build/bin/ffmpeg_streamer --input test.mp4 --rtmp rtmp://localhost:1935/live/stream --port 8554

# Watch via RTMP
ffplay rtmp://localhost:1935/live/stream

# Also available via RTSP (no external server needed)
ffplay rtsp://localhost:8554/live
```

## Manual SRS Setup

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:5
```

## nginx-rtmp Alternative

```bash
docker run --rm -it -p 1935:1935 -p 8080:80 tiangolo/nginx-rtmp
```

## Verify

```bash
# Check SRS is listening
nc -zv localhost 1935

# Push from StreamSight
./build/bin/ffmpeg_streamer --input test.mp4 --rtmp rtmp://localhost:1935/live/test --port 8554
```