#!/usr/bin/env bash
# =============================================================================
# FFmpeg CRT Capture Analysis Toolkit
# =============================================================================
# Apply broadcast-grade analysis filters to webcam captures of CRT monitors.
# Each command takes a PNG/JPG input and produces diagnostic PNGs.
#
# Usage:
#   ./ffmpeg_analysis.sh input.jpg [output_dir]
#   ./ffmpeg_analysis.sh input.png ./results
#
# Requires: ffmpeg with --enable-libfreetype (most distro packages include it)
# =============================================================================

set -euo pipefail

INPUT="${1:?Usage: $0 input.jpg [output_dir]}"
OUTDIR="${2:-./ffmpeg_analysis}"
STEM=$(basename "${INPUT%.*}")

mkdir -p "$OUTDIR"

echo "=== FFmpeg CRT Analysis Toolkit ==="
echo "Input:  $INPUT"
echo "Output: $OUTDIR/"
echo ""

# ---------------------------------------------------------------------------
# 1. WAVEFORM MONITOR — plots luma per column, broadcast style
# ---------------------------------------------------------------------------
echo "[1/12] Waveform monitor (luma)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,waveform=m=column:g=green:f=lowpass:e=instant:c=bt709" \
  -frames:v 1 "$OUTDIR/${STEM}_waveform_luma.png"

# Waveform showing each color plane stacked
echo "[1b/12] Waveform monitor (color parade)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,waveform=m=column:d=parade:g=green:f=lowpass:e=instant:c=bt709" \
  -frames:v 1 "$OUTDIR/${STEM}_waveform_parade.png"

# ---------------------------------------------------------------------------
# 2. VECTORSCOPE — shows chroma distribution (U vs V)
# ---------------------------------------------------------------------------
echo "[2/12] Vectorscope (color3 mode)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,vectorscope=m=color3:g=green:e=instant" \
  -frames:v 1 "$OUTDIR/${STEM}_vectorscope.png"

# Vectorscope with color targets visible
echo "[2b/12] Vectorscope (color4 + targets)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,vectorscope=m=color4:g=green:e=instant" \
  -frames:v 1 "$OUTDIR/${STEM}_vectorscope_targets.png"

# ---------------------------------------------------------------------------
# 3. HISTOGRAM — RGB distribution
# ---------------------------------------------------------------------------
echo "[3/12] Histogram (RGB levels)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "histogram=d=parade:l=256" \
  -frames:v 1 "$OUTDIR/${STEM}_histogram.png"

# ---------------------------------------------------------------------------
# 4. EXTRACT PLANES — separate Y, U, V channels
# ---------------------------------------------------------------------------
echo "[4/12] Extract Y (luma) plane..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,extractplanes=y" \
  -frames:v 1 "$OUTDIR/${STEM}_plane_Y.png"

echo "[4b/12] Extract U (chroma blue) plane..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,extractplanes=u" \
  -frames:v 1 "$OUTDIR/${STEM}_plane_U.png"

echo "[4c/12] Extract V (chroma red) plane..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,extractplanes=v" \
  -frames:v 1 "$OUTDIR/${STEM}_plane_V.png"

# Extract RGB planes
echo "[4d/12] Extract R, G, B planes..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=rgb24,extractplanes=r" \
  -frames:v 1 "$OUTDIR/${STEM}_plane_R.png"
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=rgb24,extractplanes=g" \
  -frames:v 1 "$OUTDIR/${STEM}_plane_G.png"
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=rgb24,extractplanes=b" \
  -frames:v 1 "$OUTDIR/${STEM}_plane_B.png"

# ---------------------------------------------------------------------------
# 5. FALSE COLOR / PSEUDOCOLOR — heatmap of luma levels
# ---------------------------------------------------------------------------
echo "[5/12] Pseudocolor (jet colormap on luma)..."
# This maps luma values to a rainbow colormap using the pseudocolor filter
# i=0 means base on the Y/luma plane
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,pseudocolor='\
if(between(val,0,32),lerp(32,0,(val)/32),\
if(between(val,32,64),lerp(0,0,(val-32)/32),\
if(between(val,64,96),lerp(0,0,(val-64)/32),\
if(between(val,96,128),lerp(0,32,(val-96)/32),\
if(between(val,128,160),lerp(32,64,(val-128)/32),\
if(between(val,160,192),lerp(64,128,(val-160)/32),\
if(between(val,192,224),lerp(128,192,(val-192)/32),\
lerp(192,255,(val-224)/31)))))))):\
if(between(val,0,32),lerp(0,0,(val)/32),\
if(between(val,32,64),lerp(0,128,(val-32)/32),\
if(between(val,64,96),lerp(128,255,(val-64)/32),\
if(between(val,96,128),lerp(255,255,(val-96)/32),\
if(between(val,128,160),lerp(255,255,(val-128)/32),\
if(between(val,160,192),lerp(255,128,(val-160)/32),\
if(between(val,192,224),lerp(128,0,(val-192)/32),\
lerp(0,0,(val-224)/31)))))))):\
if(between(val,0,32),lerp(128,255,(val)/32),\
if(between(val,32,64),lerp(255,255,(val-32)/32),\
if(between(val,64,96),lerp(255,128,(val-64)/32),\
if(between(val,96,128),lerp(128,0,(val-96)/32),\
if(between(val,128,160),lerp(0,0,(val-128)/32),\
if(between(val,160,192),lerp(0,0,(val-160)/32),\
if(between(val,192,224),lerp(0,0,(val-192)/32),\
lerp(0,128,(val-224)/31))))))))':i=0" \
  -frames:v 1 "$OUTDIR/${STEM}_pseudocolor.png" 2>/dev/null || \
  echo "  (pseudocolor filter failed — trying simpler LUT approach)"

# Simpler false color using LUT
echo "[5b/12] LUT-based false color (luma inversion + contrast)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,lutyuv=y='if(lt(val,16),255,if(lt(val,32),200,if(lt(val,64),150,if(lt(val,128),100,if(lt(val,200),50,0)))))'" \
  -frames:v 1 "$OUTDIR/${STEM}_lut_zones.png"

# ---------------------------------------------------------------------------
# 6. CONTRAST ENHANCEMENT — CLAHE-like via eq filter
# ---------------------------------------------------------------------------
echo "[6/12] Histogram equalization..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "eq=contrast=1.5:brightness=0.05:saturation=1.5" \
  -frames:v 1 "$OUTDIR/${STEM}_enhanced.png"

# ---------------------------------------------------------------------------
# 7. SATURATION HIGHLIGHT — shows only saturated (chromatic) content
# ---------------------------------------------------------------------------
echo "[7/12] Saturation highlight..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,lutyuv=y=128:u=val:v=val" \
  -frames:v 1 "$OUTDIR/${STEM}_chroma_only.png"

# Luma only (kill chroma)
echo "[7b/12] Luma only (chroma removed)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=yuv444p,lutyuv=y=val:u=128:v=128" \
  -frames:v 1 "$OUTDIR/${STEM}_luma_only.png"

# ---------------------------------------------------------------------------
# 8. EDGE DETECTION — reveals transitions between bars
# ---------------------------------------------------------------------------
echo "[8/12] Edge detection (Sobel-like via convolution)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "format=gray,convolution='0 -1 0 -1 4 -1 0 -1 0:0 -1 0 -1 4 -1 0 -1 0:0 -1 0 -1 4 -1 0 -1 0:0 -1 0 -1 4 -1 0 -1 0'" \
  -frames:v 1 "$OUTDIR/${STEM}_edges.png"

# ---------------------------------------------------------------------------
# 9. DATASCOPE — shows actual pixel values overlaid
# ---------------------------------------------------------------------------
echo "[9/12] Datascope (pixel value overlay)..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "scale=160:120,datascope=s=hd720:mode=color2" \
  -frames:v 1 "$OUTDIR/${STEM}_datascope.png"

# ---------------------------------------------------------------------------
# 10. OSCILLOSCOPE — horizontal line trace
# ---------------------------------------------------------------------------
echo "[10/12] Oscilloscope trace..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "oscilloscope=x=0.5:y=0.33:s=1:c=1:t=1" \
  -frames:v 1 "$OUTDIR/${STEM}_oscilloscope.png"

# ---------------------------------------------------------------------------
# 11. CIE SCOPE — chromaticity diagram
# ---------------------------------------------------------------------------
echo "[11/12] CIE scope..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -vf "ciescope=s=512:system=bt709" \
  -frames:v 1 "$OUTDIR/${STEM}_ciescope.png"

# ---------------------------------------------------------------------------
# 12. COMPOSITE DASHBOARD — all scopes in one image
# ---------------------------------------------------------------------------
echo "[12/12] Composite dashboard..."
ffmpeg -y -loglevel warning -i "$INPUT" \
  -filter_complex "\
    [0:v]split=5[orig][wf_in][vs_in][hist_in][osc_in];\
    [orig]scale=480:360[orig_s];\
    [wf_in]format=yuv444p,waveform=m=column:g=green:f=lowpass:e=instant:c=bt709,scale=480:360[wf];\
    [vs_in]format=yuv444p,vectorscope=m=color4:g=green:e=instant,scale=480:360[vs];\
    [hist_in]histogram=d=parade:l=256,scale=480:360[hist];\
    [osc_in]oscilloscope=x=0.5:y=0.33:s=1:c=1:t=1,scale=480:360[osc];\
    color=black:480x360[black];\
    [orig_s][wf][vs]hstack=3[top];\
    [hist][osc][black]hstack=3[bot];\
    [top][bot]vstack[out]" \
  -map '[out]' -frames:v 1 "$OUTDIR/${STEM}_ffmpeg_dashboard.png"

echo ""
echo "Done! All outputs in: $OUTDIR/"
echo ""
echo "=== Quick reference for individual filters ==="
echo ""
echo "# Waveform monitor on any image:"
echo "ffmpeg -i img.png -vf 'format=yuv444p,waveform=m=column:g=green' -frames:v 1 wf.png"
echo ""
echo "# Vectorscope with target boxes:"
echo "ffmpeg -i img.png -vf 'format=yuv444p,vectorscope=m=color4:g=green' -frames:v 1 vs.png"
echo ""
echo "# Luma-only (strip all color):"
echo "ffmpeg -i img.png -vf 'format=yuv444p,lutyuv=y=val:u=128:v=128' -frames:v 1 luma.png"
echo ""
echo "# Chroma-only (flatten luma to mid-gray):"
echo "ffmpeg -i img.png -vf 'format=yuv444p,lutyuv=y=128:u=val:v=val' -frames:v 1 chroma.png"
echo ""
echo "# Side-by-side original + vectorscope:"
echo "ffmpeg -i img.png -filter_complex '[0]split[a][b];[b]format=yuv444p,vectorscope=m=color4:g=green[vs];[a][vs]hstack' -frames:v 1 sbs.png"
