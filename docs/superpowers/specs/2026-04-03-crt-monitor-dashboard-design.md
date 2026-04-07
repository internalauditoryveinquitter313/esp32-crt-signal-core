# CRT Monitor Dashboard — MVP Design

**Date:** 2026-04-03
**Status:** Draft
**Vibe:** Retro Terminal / CRT Hacker (phosphor green, scanlines, monospace)
**Backend:** Mongoose (C, single file — cesanta/mongoose)
**Layout:** Tabbed SPA (mobile-first, accessed via Tailscale on Android)

---

## 1. Overview

A self-contained C tool that provides a browser-based dashboard for monitoring, capturing, and browsing CRT signal output from the ESP32. Runs on the host PC, serves a retro-styled web UI, and exposes REST/WebSocket APIs for live feed and capture control.

**Location:** `tools/crt_monitor/`

**What it replaces:** The ad-hoc `python3 -m http.server` + separate `live.html` / `index.html` files in `/tmp/crt_analysis/`.

## 2. Architecture

```
Browser (Android/Desktop)
    │
    ├── GET /              → static/index.html (SPA)
    ├── GET /static/*      → static files (CSS, JS, images)
    ├── WS  /ws/live       → WebSocket live feed (binary JPG frames)
    ├── POST /api/capture  → snapshot + save to captures/
    ├── GET  /api/gallery  → JSON list of saved captures
    ├── GET  /captures/*   → serve saved capture images
    └── GET  /api/status   → signal status JSON
    │
Mongoose HTTP Server (C)
    │
    ├── v4l2 capture (direct ioctl or ffmpeg subprocess)
    ├── captures/ directory management
    └── signal status (read from ESP32 serial or static config)
```

## 3. Directory Structure

```
tools/crt_monitor/
├── main.c                  # Entry point, Mongoose event loop, route dispatch
├── capture.c / capture.h   # v4l2 capture logic (open device, grab frame, JPG encode)
├── routes.c / routes.h     # HTTP/WS route handlers
├── status.c / status.h     # Signal status (NTSC/PAL detection, config readback)
├── mongoose.c              # Mongoose library (vendored, single file)
├── mongoose.h              # Mongoose header (vendored)
├── Makefile                # Build: gcc -o crt_monitor main.c capture.c routes.c status.c mongoose.c -lpthread
├── static/
│   ├── index.html          # SPA shell — tab navigation, retro theme
│   ├── style.css           # Retro terminal CSS (phosphor green, scanlines, CRT glow)
│   └── app.js              # Tab logic, WebSocket client, capture trigger, gallery loader
└── captures/               # Saved snapshots (created at runtime)
```

## 4. Tabs (MVP)

### Tab 1: LIVE
- WebSocket connection to `/ws/live`
- Mongoose reads frames from v4l2 device, encodes as JPG, sends as binary WebSocket messages
- Frame rate target: 2-5 fps (C270 USB bandwidth limited anyway)
- Status bar at bottom: video standard, frame counter, connection status
- Big "CAPTURE" button — sends POST `/api/capture`, flashes green on success
- Fallback: if WebSocket fails, falls back to `<img>` polling `/api/capture?live=1`

### Tab 2: GALLERY
- GET `/api/gallery` returns JSON: `[{"name":"capture_001.jpg","timestamp":1712345678,"size":47832}, ...]`
- Grid of thumbnails, click for fullscreen lightbox
- Swipe navigation in lightbox (touch events)
- Delete button (DELETE `/api/gallery/{name}`)
- Sorted by timestamp descending (newest first)

### Tab 3: STATUS
- GET `/api/status` returns:
```json
{
  "video_standard": "NTSC",
  "color_enabled": true,
  "active_lines": 240,
  "sample_rate_hz": 14318180,
  "capture_device": "/dev/video0",
  "capture_resolution": "1280x720",
  "uptime_s": 3600
}
```
- Rendered as retro terminal readout with blinking cursor
- Auto-refresh every 5 seconds
- Shows camera device info and server uptime

## 5. Visual Style — Retro Terminal

### Colors
- Background: `#0a0a0a` (near black)
- Primary text: `#00ff41` (phosphor green)
- Dim text: `#0a6e2a` (dark green)
- Accent: `#00ffcc` (cyan, for highlights/links)
- Error: `#ff3333` (red, for errors/warnings)
- Border: `#0f3` at low opacity

### Effects
- Scanline overlay: CSS `repeating-linear-gradient` with 2px transparent/1px rgba green bands
- Subtle text-shadow glow: `0 0 8px rgba(0,255,65,0.4)`
- Monospace font: system `monospace` or `"Fira Code", "JetBrains Mono", monospace`
- Tab bar styled as terminal menu: `[ LIVE ]  GALLERY   STATUS`
- Blinking cursor on status readouts: CSS animation `blink 1s step-end infinite`

### Mobile
- Tabs at bottom (thumb-reachable)
- Touch targets minimum 44px
- Full-width layout, no side margins wasted
- Live feed fills available viewport height

## 6. Capture Pipeline

### Live Feed (WebSocket)
1. On WS connect: open `/dev/video0` via v4l2 (MJPEG format if available, else YUYV + sw encode)
2. Timer at ~3fps: grab frame → send as binary WS message
3. On WS disconnect: release device (or keep open if other connections exist)
4. Refcount on device handle — multiple viewers share one capture stream

### Snapshot (POST /api/capture)
1. Grab single frame from v4l2 (or from current live stream if active)
2. Save as `captures/capture_YYYYMMDD_HHMMSS.jpg`
3. Return JSON: `{"ok":true, "name":"capture_20260403_165530.jpg"}`

### v4l2 Strategy
- Open device with V4L2_CAP_VIDEO_CAPTURE
- Prefer MJPEG native format (C270 supports it — zero CPU encode)
- If MJPEG unavailable: YUYV → libjpeg-turbo encode (optional dep)
- Camera settings: set via v4l2 ioctls at startup (exposure=166, gain=0, etc.) based on hardcoded defaults or CLI args

## 7. Build & Run

```bash
cd tools/crt_monitor
make                    # builds ./crt_monitor
./crt_monitor           # starts on port 8080, serves static/ and captures/

# Options:
./crt_monitor -p 9090                    # custom port
./crt_monitor -d /dev/video2             # custom capture device
./crt_monitor -c /path/to/captures       # custom captures directory
./crt_monitor --exposure 120 --gain 0    # camera defaults
```

### Dependencies
- **Build**: gcc, pthreads (standard)
- **Runtime**: v4l2 kernel headers (`linux/videodev2.h`), libjpeg-turbo (if YUYV→JPG needed)
- **Vendored**: mongoose.c/h (committed to repo)

### Makefile
```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
LDFLAGS = -lpthread
SRCS = main.c capture.c routes.c status.c mongoose.c
TARGET = crt_monitor

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
```

## 8. API Summary

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Serve `static/index.html` |
| GET | `/static/*` | Serve static assets |
| WS | `/ws/live` | Live video feed (binary JPG frames) |
| POST | `/api/capture` | Take snapshot, save to captures/ |
| GET | `/api/gallery` | List saved captures (JSON array) |
| GET | `/captures/*` | Serve individual capture images |
| DELETE | `/api/gallery/{name}` | Delete a capture |
| GET | `/api/status` | Server/signal status JSON |

## 9. Future (post-MVP)

These are scoped OUT of MVP but designed to plug in easily:

- **v1.1**: Camera controls (`POST /api/camera` → v4l2 ioctls), Signal scopes (vectorscope/waveform images generated server-side via ffmpeg subprocess)
- **v1.2**: Color bar phase meter (integrate `analyze_crt_capture.py` or port key functions to C), ESP32 config panel (serial communication via `/dev/ttyACM0`)

## 10. Constraints & Decisions

- **No malloc after init**: Mongoose uses its own allocator, but our code (capture, routes, status) should preallocate buffers at startup where possible. Follows the project's memory discipline.
- **Single-threaded event loop**: Mongoose is event-driven. Capture runs in the same loop via timers. No threads needed for MVP (v4l2 MJPEG read is fast enough).
- **MJPEG native preferred**: C270 supports MJPEG natively at 1280x720. This avoids needing libjpeg-turbo entirely for MVP — raw MJPEG frames go straight to WebSocket/disk.
- **Bind 0.0.0.0**: Default bind to all interfaces so Tailscale access works without config.
- **Captures on filesystem**: Simple directory listing, no database. Captures are JPG files with timestamp names.
