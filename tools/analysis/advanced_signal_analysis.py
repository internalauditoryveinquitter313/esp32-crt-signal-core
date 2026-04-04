#!/usr/bin/env python3
"""
Advanced Signal Analysis — deeper techniques for CRT composite video diagnostics.

This script focuses on the harder problems:
- Isolating chroma subcarrier patterns visible in webcam captures
- Measuring actual chrominance phase via YIQ decomposition
- Comparing captured bars against ideal SMPTE reference
- Band-pass filtering to isolate specific frequency content
- Generating publication-quality analysis plots (matplotlib)

Usage:
    python advanced_signal_analysis.py input.jpg [--output-dir ./results]
"""

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np

HAS_MATPLOTLIB = False
try:
    import matplotlib
    matplotlib.use('Agg')  # headless
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle
    HAS_MATPLOTLIB = True
except ImportError:
    pass

# ---------------------------------------------------------------------------
# Reference data
# ---------------------------------------------------------------------------

# NTSC 75% color bars — known chrominance phase angles (degrees, relative to burst)
NTSC_75_BARS = [
    {"name": "White",   "phase": None,   "ire": 77.0,  "rgb_75": (191, 191, 191), "i": 0.0,     "q": 0.0},
    {"name": "Yellow",  "phase": 167.1,  "ire": 69.0,  "rgb_75": (191, 191,   0), "i": -0.3218, "q": -0.3118},
    {"name": "Cyan",    "phase": 283.5,  "ire": 56.0,  "rgb_75": (  0, 191, 191), "i": -0.2739, "q":  0.3118},
    {"name": "Green",   "phase": 240.7,  "ire": 48.0,  "rgb_75": (  0, 191,   0), "i": -0.5957, "q":  0.0000},
    {"name": "Magenta", "phase": 60.7,   "ire": 36.0,  "rgb_75": (191,   0, 191), "i":  0.5957, "q":  0.0000},
    {"name": "Red",     "phase": 103.5,  "ire": 28.0,  "rgb_75": (191,   0,   0), "i":  0.2739, "q": -0.3118},
    {"name": "Blue",    "phase": 347.1,  "ire": 15.0,  "rgb_75": (  0,   0, 191), "i":  0.3218, "q":  0.3118},
    {"name": "Black",   "phase": None,   "ire": 7.5,   "rgb_75": (  0,   0,   0), "i": 0.0,     "q": 0.0},
]


def bgr_to_yiq(img):
    """Convert BGR image to YIQ float arrays."""
    f = img.astype(np.float64) / 255.0
    r, g, b = f[:, :, 2], f[:, :, 1], f[:, :, 0]
    y = 0.299 * r + 0.587 * g + 0.114 * b
    i = 0.5957 * r - 0.2745 * g - 0.3213 * b
    q = 0.2115 * r - 0.5226 * g + 0.3111 * b
    return y, i, q


# ---------------------------------------------------------------------------
# 1. CHROMINANCE PHASE POLAR PLOT (requires matplotlib)
# ---------------------------------------------------------------------------

def plot_chrominance_polar(img, output_path, num_bars=8, margin_pct=0.15):
    """Polar plot of measured chrominance phase vs. NTSC reference.

    This is THE key diagnostic for composite video — if your chroma phases
    are wrong, colors will be shifted on the TV.
    """
    if not HAS_MATPLOTLIB:
        print("  [SKIP] matplotlib not available for polar plot")
        return

    h, w = img.shape[:2]
    bar_top = int(h * 0.1)
    bar_bottom = int(h * 0.6)
    bar_width = w // num_bars
    margin = int(bar_width * margin_pct)

    y, i_ch, q_ch = bgr_to_yiq(img)

    fig, (ax_polar, ax_bar) = plt.subplots(1, 2, figsize=(16, 7),
                                            subplot_kw={'projection': None})
    ax_polar = fig.add_subplot(121, projection='polar')
    ax_bar = fig.add_subplot(122)

    measured_phases = []
    measured_amps = []
    ref_phases = []
    names = []

    for idx in range(num_bars):
        bar = NTSC_75_BARS[idx]
        x_start = idx * bar_width + margin
        x_end = (idx + 1) * bar_width - margin

        roi_i = i_ch[bar_top:bar_bottom, x_start:x_end]
        roi_q = q_ch[bar_top:bar_bottom, x_start:x_end]

        i_mean = np.mean(roi_i)
        q_mean = np.mean(roi_q)
        phase = np.degrees(np.arctan2(q_mean, i_mean)) % 360
        amp = np.sqrt(i_mean**2 + q_mean**2)

        measured_phases.append(phase)
        measured_amps.append(amp)
        ref_phases.append(bar["phase"])
        names.append(bar["name"])

    # Polar plot
    bar_colors = ['gray', '#C8C800', '#00C8C8', '#00C800', '#C800C8', '#C80000', '#0000C8', 'black']
    for idx, (name, phase, amp, ref_ph) in enumerate(zip(names, measured_phases, measured_amps, ref_phases)):
        if ref_ph is not None and amp > 0.01:
            # Measured
            ax_polar.plot(np.radians(phase), amp, 'o', markersize=10,
                         color=bar_colors[idx], label=f"{name} ({phase:.1f}deg)")
            # Reference
            ref_amp = np.sqrt(NTSC_75_BARS[idx]["i"]**2 + NTSC_75_BARS[idx]["q"]**2)
            ax_polar.plot(np.radians(ref_ph), ref_amp, 'x', markersize=12,
                         color=bar_colors[idx], markeredgewidth=2)
            # Error arc
            ax_polar.annotate('', xy=(np.radians(phase), amp),
                            xytext=(np.radians(ref_ph), ref_amp),
                            arrowprops=dict(arrowstyle='->', color='red', lw=0.8))

    ax_polar.set_title("Chrominance Phase (o=measured, x=reference)", pad=20)
    ax_polar.legend(loc='upper right', bbox_to_anchor=(1.3, 1.0), fontsize=8)

    # Bar chart of phase errors
    errors = []
    error_names = []
    for idx, (name, phase, ref_ph) in enumerate(zip(names, measured_phases, ref_phases)):
        if ref_ph is not None and measured_amps[idx] > 0.01:
            err = (phase - ref_ph + 180) % 360 - 180
            errors.append(err)
            error_names.append(name)

    if errors:
        colors_err = ['green' if abs(e) < 10 else 'orange' if abs(e) < 20 else 'red' for e in errors]
        ax_bar.barh(error_names, errors, color=colors_err)
        ax_bar.axvline(x=0, color='white', linewidth=0.5)
        ax_bar.axvline(x=10, color='yellow', linewidth=0.5, linestyle='--')
        ax_bar.axvline(x=-10, color='yellow', linewidth=0.5, linestyle='--')
        ax_bar.set_xlabel("Phase Error (degrees)")
        ax_bar.set_title("Chrominance Phase Error vs. NTSC Reference")
        ax_bar.set_facecolor('#1a1a1a')

    fig.patch.set_facecolor('#0d0d0d')
    ax_polar.set_facecolor('#1a1a1a')
    ax_polar.tick_params(colors='white')
    ax_polar.xaxis.label.set_color('white')
    ax_bar.tick_params(colors='white')
    ax_bar.xaxis.label.set_color('white')
    ax_bar.title.set_color('white')
    ax_polar.title.set_color('white')

    plt.tight_layout()
    plt.savefig(str(output_path), dpi=150, bbox_inches='tight',
                facecolor=fig.get_facecolor())
    plt.close()
    print(f"  -> {output_path}")


# ---------------------------------------------------------------------------
# 2. LUMA STAIRCASE ANALYSIS
# ---------------------------------------------------------------------------

def plot_luma_staircase(img, output_path, num_bars=8):
    """Plot horizontal luma profile showing the staircase pattern.

    Ideal color bars produce a descending staircase in luma.
    Deviations reveal gain errors, gamma issues, or sync problems.
    """
    if not HAS_MATPLOTLIB:
        print("  [SKIP] matplotlib not available for luma staircase")
        return

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape

    # Average several lines in the bar region
    y_start = int(h * 0.2)
    y_end = int(h * 0.5)
    profile = np.mean(gray[y_start:y_end, :].astype(np.float64), axis=0)

    # Approximate IRE mapping
    ire_profile = (profile / 255.0) * 100.0

    # Reference staircase
    bar_width = w / num_bars
    ref_ires = [bar["ire"] for bar in NTSC_75_BARS]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), gridspec_kw={'height_ratios': [3, 1]})
    fig.patch.set_facecolor('#0d0d0d')

    # Profile
    x = np.arange(len(profile))
    ax1.plot(x, ire_profile, color='#00ff00', linewidth=0.8, label='Measured')

    # Reference
    for idx, ref_ire in enumerate(ref_ires):
        x_start = int(idx * bar_width)
        x_end = int((idx + 1) * bar_width)
        ax1.hlines(ref_ire, x_start, x_end, colors='#ff4444', linewidths=1.5,
                   linestyles='--', label='Reference' if idx == 0 else None)
        ax1.text((x_start + x_end) / 2, ref_ire + 2,
                 NTSC_75_BARS[idx]["name"], ha='center', fontsize=8, color='#ff4444')

    ax1.set_ylabel("Approximate IRE", color='white')
    ax1.set_title("Luma Staircase — Horizontal Profile", color='white')
    ax1.set_facecolor('#1a1a1a')
    ax1.tick_params(colors='white')
    ax1.legend(facecolor='#2a2a2a', edgecolor='white', labelcolor='white')
    ax1.set_ylim(0, 110)
    ax1.axhline(y=7.5, color='#666666', linewidth=0.5, linestyle=':')
    ax1.axhline(y=100, color='#666666', linewidth=0.5, linestyle=':')

    # Derivative (shows transitions)
    gradient = np.gradient(ire_profile)
    ax2.plot(x, gradient, color='#ffaa00', linewidth=0.5)
    ax2.set_ylabel("dIRE/dx", color='white')
    ax2.set_xlabel("Pixel X", color='white')
    ax2.set_title("Transition Gradient (spikes = bar edges)", color='white')
    ax2.set_facecolor('#1a1a1a')
    ax2.tick_params(colors='white')

    plt.tight_layout()
    plt.savefig(str(output_path), dpi=150, bbox_inches='tight',
                facecolor=fig.get_facecolor())
    plt.close()
    print(f"  -> {output_path}")


# ---------------------------------------------------------------------------
# 3. FREQUENCY SPECTRUM (1D horizontal) — look for subcarrier artifacts
# ---------------------------------------------------------------------------

def plot_horizontal_spectrum(img, output_path, line_y=None):
    """1D FFT of a horizontal line — can reveal subcarrier-related patterns.

    In a webcam capture of an NTSC signal, you might see energy at the
    subcarrier beat frequency (3.58 MHz aliased to whatever the pixel
    clock maps to). This manifests as fine color fringing patterns.
    """
    if not HAS_MATPLOTLIB:
        print("  [SKIP] matplotlib not available for spectrum plot")
        return

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float64)
    h, w = gray.shape

    if line_y is None:
        line_y = h // 3

    # Average a few lines for better SNR
    half_band = 5
    y_lo = max(0, line_y - half_band)
    y_hi = min(h, line_y + half_band)
    line = np.mean(gray[y_lo:y_hi, :], axis=0)

    # Windowed FFT
    window = np.hanning(len(line))
    windowed = (line - np.mean(line)) * window

    fft = np.fft.rfft(windowed)
    magnitude = np.abs(fft)
    freqs = np.fft.rfftfreq(len(line))  # in cycles/pixel

    fig, axes = plt.subplots(3, 1, figsize=(14, 10))
    fig.patch.set_facecolor('#0d0d0d')

    # Line profile
    axes[0].plot(line, color='#00ff00', linewidth=0.8)
    axes[0].set_title(f"Horizontal Line Profile (y={line_y})", color='white')
    axes[0].set_ylabel("Pixel Value", color='white')
    axes[0].set_facecolor('#1a1a1a')
    axes[0].tick_params(colors='white')

    # Magnitude spectrum (linear)
    axes[1].plot(freqs[1:], magnitude[1:], color='#00aaff', linewidth=0.8)
    axes[1].set_title("FFT Magnitude Spectrum (linear)", color='white')
    axes[1].set_ylabel("Magnitude", color='white')
    axes[1].set_facecolor('#1a1a1a')
    axes[1].tick_params(colors='white')

    # Magnitude spectrum (log dB)
    db = 20 * np.log10(magnitude[1:] + 1e-10)
    axes[2].plot(freqs[1:], db, color='#ff6600', linewidth=0.8)
    axes[2].set_title("FFT Magnitude Spectrum (dB)", color='white')
    axes[2].set_xlabel("Frequency (cycles/pixel)", color='white')
    axes[2].set_ylabel("dB", color='white')
    axes[2].set_facecolor('#1a1a1a')
    axes[2].tick_params(colors='white')

    # Mark some interesting frequencies
    # Nyquist is at 0.5 cycles/pixel
    axes[2].axvline(x=0.25, color='yellow', linewidth=0.5, linestyle='--', alpha=0.5)
    axes[2].text(0.25, db.max() * 0.9, "1/4 Nyq", fontsize=8, color='yellow')

    plt.tight_layout()
    plt.savefig(str(output_path), dpi=150, bbox_inches='tight',
                facecolor=fig.get_facecolor())
    plt.close()
    print(f"  -> {output_path}")


# ---------------------------------------------------------------------------
# 4. VERTICAL SPECTRUM — scanline frequency detection
# ---------------------------------------------------------------------------

def plot_vertical_spectrum(img, output_path):
    """1D FFT of vertical profile — finds scanline periodicity.

    The dominant vertical frequency corresponds to the scanline spacing
    as captured by the webcam.
    """
    if not HAS_MATPLOTLIB:
        print("  [SKIP] matplotlib not available for vertical spectrum")
        return

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float64)
    h, w = gray.shape

    # Average columns in bar region
    x_start = w // 4
    x_end = 3 * w // 4
    v_profile = np.mean(gray[:, x_start:x_end], axis=1)

    window = np.hanning(len(v_profile))
    windowed = (v_profile - np.mean(v_profile)) * window

    fft = np.fft.rfft(windowed)
    magnitude = np.abs(fft)
    freqs = np.fft.rfftfreq(len(v_profile))

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 7))
    fig.patch.set_facecolor('#0d0d0d')

    ax1.plot(v_profile, color='#00ff00', linewidth=0.8)
    ax1.set_title("Vertical Brightness Profile (averaged across bar region)", color='white')
    ax1.set_ylabel("Pixel Value", color='white')
    ax1.set_xlabel("Row (Y)", color='white')
    ax1.set_facecolor('#1a1a1a')
    ax1.tick_params(colors='white')

    db = 20 * np.log10(magnitude[1:] + 1e-10)
    ax2.plot(freqs[1:], db, color='#ff6600', linewidth=0.8)
    ax2.set_title("Vertical Frequency Spectrum (dB)", color='white')
    ax2.set_xlabel("Frequency (cycles/pixel)", color='white')
    ax2.set_ylabel("dB", color='white')
    ax2.set_facecolor('#1a1a1a')
    ax2.tick_params(colors='white')

    # Find dominant peak (scanline freq)
    peak_idx = np.argmax(magnitude[1:]) + 1
    peak_freq = freqs[peak_idx]
    if peak_freq > 0:
        peak_period = 1.0 / peak_freq
        ax2.axvline(x=peak_freq, color='red', linewidth=1, linestyle='--')
        ax2.text(peak_freq + 0.005, db.max() * 0.9,
                 f"Peak: {peak_freq:.4f} c/px\n= {peak_period:.1f} px/scanline",
                 fontsize=9, color='red')

    plt.tight_layout()
    plt.savefig(str(output_path), dpi=150, bbox_inches='tight',
                facecolor=fig.get_facecolor())
    plt.close()
    print(f"  -> {output_path}")


# ---------------------------------------------------------------------------
# 5. BAND-PASS FILTER — isolate specific frequency content
# ---------------------------------------------------------------------------

def bandpass_filter_image(img, low_freq=0.05, high_freq=0.2):
    """Apply a 2D band-pass filter to isolate medium-frequency content.

    This can reveal:
    - Subcarrier beat patterns (with appropriate freq range)
    - Phosphor grid structure
    - Moire interference patterns

    Frequencies in cycles/pixel: 0.0=DC, 0.5=Nyquist
    """
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float64)
    rows, cols = gray.shape

    dft = np.fft.fft2(gray)
    dft_shift = np.fft.fftshift(dft)

    # Create bandpass mask
    cy, cx = rows // 2, cols // 2
    y_coords, x_coords = np.ogrid[:rows, :cols]
    dist = np.sqrt((y_coords - cy)**2 + (x_coords - cx)**2)

    # Convert freq to pixel radius
    max_dim = max(rows, cols)
    r_low = low_freq * max_dim
    r_high = high_freq * max_dim

    # Smooth Butterworth-style bandpass
    order = 4
    lp = 1.0 / (1.0 + (dist / r_high)**(2 * order))  # low-pass at high_freq
    hp = 1.0 - 1.0 / (1.0 + (dist / r_low)**(2 * order))  # high-pass at low_freq
    mask = lp * hp

    # Apply
    filtered = dft_shift * mask
    result = np.abs(np.fft.ifft2(np.fft.ifftshift(filtered)))

    # Normalize for visibility
    p1, p99 = np.percentile(result, [1, 99])
    result = np.clip((result - p1) / (p99 - p1 + 1e-6) * 255, 0, 255).astype(np.uint8)

    return result


# ---------------------------------------------------------------------------
# 6. REFERENCE COMPARISON — side-by-side with ideal bars
# ---------------------------------------------------------------------------

def generate_reference_bars(width, height):
    """Generate a perfect SMPTE 75% color bar reference image."""
    ref = np.zeros((height, width, 3), dtype=np.uint8)
    bar_width = width // 8

    for idx, bar in enumerate(NTSC_75_BARS):
        x_start = idx * bar_width
        x_end = (idx + 1) * bar_width if idx < 7 else width
        r, g, b = bar["rgb_75"]
        ref[0:int(height*0.67), x_start:x_end] = [b, g, r]  # BGR

    return ref


def comparison_diff(img, output_path):
    """Generate a difference image between capture and ideal reference.

    Bright areas in the diff = large color errors.
    Uniform diff = systematic offset (gamma, white balance).
    """
    h, w = img.shape[:2]
    ref = generate_reference_bars(w, h)

    # Resize ref to match
    ref_resized = cv2.resize(ref, (w, h))

    # Absolute difference
    diff = cv2.absdiff(img, ref_resized)

    # Amplify for visibility
    diff_amplified = cv2.convertScaleAbs(diff, alpha=3.0)

    # Side by side
    result = np.hstack([img, ref_resized, diff_amplified])

    cv2.imwrite(str(output_path), result)
    print(f"  -> {output_path}")


# ---------------------------------------------------------------------------
# 7. I/Q CHANNEL HEATMAPS
# ---------------------------------------------------------------------------

def iq_heatmaps(img, output_dir):
    """Generate color-mapped heatmaps of I and Q channels.

    I channel: orange-cyan contrast axis
    Q channel: purple-green contrast axis

    For color bars, each bar should show a uniform I and Q value within
    its region. Non-uniformity indicates chroma noise or cross-talk.
    """
    _, i_ch, q_ch = bgr_to_yiq(img)

    # Map to 0-255 for visualization
    i_vis = np.clip((i_ch + 0.6) / 1.2 * 255, 0, 255).astype(np.uint8)
    q_vis = np.clip((q_ch + 0.53) / 1.06 * 255, 0, 255).astype(np.uint8)

    i_heatmap = cv2.applyColorMap(i_vis, cv2.COLORMAP_TWILIGHT_SHIFTED)
    q_heatmap = cv2.applyColorMap(q_vis, cv2.COLORMAP_TWILIGHT_SHIFTED)

    cv2.imwrite(str(output_dir / "I_channel_heatmap.png"), i_heatmap)
    print(f"  -> {output_dir / 'I_channel_heatmap.png'}")

    cv2.imwrite(str(output_dir / "Q_channel_heatmap.png"), q_heatmap)
    print(f"  -> {output_dir / 'Q_channel_heatmap.png'}")

    # Combined I-Q as false color (I=red, Q=blue, amplitude=green)
    amp = np.sqrt(i_ch**2 + q_ch**2)
    amp_vis = np.clip(amp / 0.6 * 255, 0, 255).astype(np.uint8)

    iq_combined = np.stack([q_vis, amp_vis, i_vis], axis=-1)
    cv2.imwrite(str(output_dir / "IQ_combined.png"), iq_combined)
    print(f"  -> {output_dir / 'IQ_combined.png'}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Advanced CRT Signal Analysis — chrominance phase, frequency, and reference comparison"
    )
    parser.add_argument("input", help="Input image (PNG/JPG from webcam capture)")
    parser.add_argument("--output-dir", "-o", default="./crt_analysis_advanced",
                        help="Output directory")

    args = parser.parse_args()

    img = cv2.imread(args.input)
    if img is None:
        print(f"ERROR: cannot read {args.input}")
        sys.exit(1)

    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    stem = Path(args.input).stem

    print(f"=== Advanced Signal Analysis ===")
    print(f"Input:  {args.input} ({img.shape[1]}x{img.shape[0]})")
    print(f"Output: {out}/\n")

    print("[1/7] Chrominance phase polar plot...")
    plot_chrominance_polar(img, out / f"{stem}_chroma_polar.png")

    print("[2/7] Luma staircase analysis...")
    plot_luma_staircase(img, out / f"{stem}_luma_staircase.png")

    print("[3/7] Horizontal frequency spectrum...")
    plot_horizontal_spectrum(img, out / f"{stem}_h_spectrum.png")

    print("[4/7] Vertical frequency spectrum (scanlines)...")
    plot_vertical_spectrum(img, out / f"{stem}_v_spectrum.png")

    print("[5/7] Band-pass filtered views...")
    for name, lo, hi in [("low_detail", 0.01, 0.05),
                          ("mid_detail", 0.05, 0.2),
                          ("high_detail", 0.2, 0.45)]:
        filtered = bandpass_filter_image(img, lo, hi)
        path = out / f"{stem}_bandpass_{name}.png"
        cv2.imwrite(str(path), filtered)
        print(f"  -> {path}")

    print("[6/7] Reference comparison...")
    comparison_diff(img, out / f"{stem}_vs_reference.png")

    print("[7/7] I/Q channel heatmaps...")
    iq_heatmaps(img, out)

    print(f"\nDone! All outputs in: {out}/")


if __name__ == "__main__":
    main()
