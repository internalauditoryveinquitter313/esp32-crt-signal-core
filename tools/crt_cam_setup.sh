#!/usr/bin/env bash
# crt_cam_setup.sh — Configure Logitech C270 for CRT capture without flicker/noise
#
# Usage:
#   sudo ./crt_cam_setup.sh [profile] [device]
#
# Profiles:
#   ntsc     — NTSC 59.94Hz CRT (default)
#   pal      — PAL 50Hz CRT
#   still    — Long exposure for still image capture (low noise)
#   reset    — Reset to factory defaults
#
# The key insight: exposure_time_absolute is in units of 0.1ms (100us).
# NTSC field = 16.68ms (value 167), frame = 33.37ms (value 333).
# C270 has a rolling shutter — each sensor row exposes at a different time.
# To eliminate banding, exposure must span at least 1 full CRT frame (2 fields).
#
# References:
#   - UVC spec 1.5: exposure_time_absolute unit = 100us
#   - int10h.org/blog/2018/06/taking-decent-photos-of-your-crt-tv-screen/
#   - kurokesu.com/main/2020/05/22/uvc-camera-exposure-timing-in-opencv/

set -euo pipefail

PROFILE="${1:-ntsc}"
DEV="${2:-/dev/video0}"
V4L="v4l2-ctl -d $DEV"

# ── Color codes ─────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[1;33m'; CYN='\033[0;36m'; RST='\033[0m'

info()  { echo -e "${CYN}[cam]${RST} $*"; }
ok()    { echo -e "${GRN}[ok]${RST}  $*"; }
warn()  { echo -e "${YEL}[!]${RST}  $*"; }
fail()  { echo -e "${RED}[err]${RST} $*" >&2; exit 1; }

# ── Verify device ───────────────────────────────────────────────────
[[ -c "$DEV" ]] || fail "Device $DEV not found"
info "Configuring $DEV — profile: $PROFILE"

# ── Helper: set control with retry (C270 sometimes needs a second try) ──
set_ctrl() {
    local ctrl="$1" val="$2"
    for attempt in 1 2 3; do
        if $V4L --set-ctrl="$ctrl=$val" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    warn "Failed to set $ctrl=$val after 3 attempts"
    return 1
}

# ── Step 1: Force manual exposure FIRST (must be before exposure_time) ──
info "Switching to manual exposure..."
set_ctrl auto_exposure 1   # 1 = Manual Mode, 3 = Aperture Priority

# ── Step 2: Profile-specific exposure ───────────────────────────────
case "$PROFILE" in
    ntsc)
        # NTSC: 59.94 Hz field rate, 29.97 Hz frame rate
        # 2 frames = 66.73ms (value 667) — absorbs C270 timing drift
        # C270 adds ~5 internally, so request 662 to land on 667
        # Camera drops to ~15fps effective (exposure > frame interval)
        EXPOSURE=667
        info "NTSC: exposure=667 (66.7ms = 2 frames @ 29.97Hz, ~15fps)"
        ;;
    pal)
        # PAL: 50 Hz field rate, 25 Hz frame rate
        # 2 frames = 80.0ms (value 800)
        EXPOSURE=800
        info "PAL: exposure=800 (80ms = 2 frames @ 25Hz)"
        ;;
    still)
        # Long exposure for stills — 3 frames worth, minimum noise
        # NTSC: 3 * 333 ≈ 1000 (100ms)
        EXPOSURE=1000
        info "Still: exposure=1000 (100ms = ~3 NTSC frames)"
        ;;
    reset)
        info "Resetting to factory defaults..."
        set_ctrl auto_exposure 3          # Aperture Priority (auto)
        set_ctrl exposure_time_absolute 166
        set_ctrl exposure_dynamic_framerate 0
        set_ctrl brightness 128
        set_ctrl contrast 32
        set_ctrl saturation 32
        set_ctrl gain 64
        set_ctrl sharpness 24
        set_ctrl white_balance_automatic 1
        set_ctrl power_line_frequency 2   # 60 Hz
        set_ctrl backlight_compensation 0
        ok "Reset to defaults"
        $V4L --list-ctrls
        exit 0
        ;;
    *)
        fail "Unknown profile: $PROFILE (use: ntsc, pal, still, reset)"
        ;;
esac

# C270 firmware adds ~5 to requested value. Compensate.
set_ctrl exposure_time_absolute "$((EXPOSURE - 5))"

# ── Step 3: Disable dynamic framerate (lock at 30fps) ──────────────
set_ctrl exposure_dynamic_framerate 0

# ── Step 4: Disable backlight compensation (adds noise on CRT) ─────
# This tries to brighten dark areas = amplifies sensor noise on CRT's
# naturally dark borders and blanking regions.
set_ctrl backlight_compensation 0

# ── Step 5: Manual white balance (CRT phosphor ~5500K-6500K) ───────
# Auto WB fluctuates frame-to-frame on CRT — kills consistency.
# P22 phosphor (Samsung CRT) has warm white around 5500K-6500K.
set_ctrl white_balance_automatic 0
sleep 0.2  # C270 needs a moment before accepting temperature
set_ctrl white_balance_temperature 5500

# ── Step 6: Gain — keep LOW to minimize sensor noise ───────────────
# With exposure=333 (33ms), plenty of light from CRT phosphor.
# High gain = amplified noise. Start at 32, increase only if too dark.
set_ctrl gain 32

# ── Step 7: Power line frequency — DISABLE for CRT ─────────────────
# This filter is for fluorescent/LED light 50/60Hz flicker.
# We're filming a CRT, not a room — the filter can interfere.
set_ctrl power_line_frequency 0

# ── Step 8: Image processing — gentle for CRT ──────────────────────
# Sharpness: LOW — reduces moiré between phosphor dots and sensor pixels
# Contrast: slightly above default — CRT already has soft contrast
# Brightness: default is fine, CRT provides its own
# Saturation: slightly up for color CRT, default for mono
set_ctrl sharpness 16       # Low — moiré killer
set_ctrl brightness 128     # Neutral
set_ctrl contrast 40        # Slightly punchy
set_ctrl saturation 48      # Slightly vivid for color bars

# ── Verify ──────────────────────────────────────────────────────────
echo ""
ok "Configuration applied!"
echo ""
info "Current settings:"
$V4L --list-ctrls | while IFS= read -r line; do
    echo "  $line"
done

echo ""
info "Quick test: ffplay -f v4l2 -input_format mjpeg -video_size 1280x720 -i $DEV"
info "Or: ./crt_monitor -p 8080 -d $DEV"
echo ""

# ── Exposure math cheat sheet ───────────────────────────────────────
cat <<'CHEAT'
┌─────────────────────────────────────────────────────┐
│  exposure_time_absolute units = 0.1ms (100µs)       │
│                                                     │
│  NTSC (59.94 Hz):                                   │
│    1 field  = 16.68ms  → value 167                  │
│    1 frame  = 33.37ms  → value 333  ← recommended   │
│    2 frames = 66.73ms  → value 667                  │
│                                                     │
│  PAL (50 Hz):                                       │
│    1 field  = 20.00ms  → value 200                  │
│    1 frame  = 40.00ms  → value 400  ← recommended   │
│    2 frames = 80.00ms  → value 800                  │
│                                                     │
│  Rolling shutter: use ≥1 frame for zero banding     │
│  More frames = less noise, but more motion blur     │
└─────────────────────────────────────────────────────┘
CHEAT
