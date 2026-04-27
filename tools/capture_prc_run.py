#!/usr/bin/env python3
"""Capture a full PRC boot run from the ESP32 and save CSVs.

Run after `idf.py flash` reboots the chip. This script:
  1. Pulses DTR/RTS to reset the ESP32 cleanly (skips download mode).
  2. Streams UART for `--duration` seconds.
  3. Parses the ESP-IDF logs (IR cal, area_x, area_y, decay, prbs, decay_csv,
     prbs_csv).
  4. Writes timestamped CSVs to <project>/tmp/prc_runs/<ts>/{cal,area,decay,prbs}.csv
  5. Prints a one-screen analysis: ring center offset, decay tau estimate,
     PRBS rise/fall asymmetry.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import serial


PROJECT_ROOT = Path(__file__).resolve().parent.parent
TMP_DIR = PROJECT_ROOT / "tmp" / "prc_runs"


def reset_and_capture(port: str, baud: int, duration: float) -> str:
    s = serial.Serial(port, baud, timeout=0)
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    data = b""
    end = time.time() + duration
    while time.time() < end:
        chunk = s.read(8192)
        if chunk:
            data += chunk
        time.sleep(0.05)
    s.close()
    return data.decode("utf-8", errors="replace")


def parse_run(text: str) -> dict:
    out: dict = {
        "cal": {},
        "area_x": [],
        "area_y": [],
        "area_peak": {},
        "decay_trials": [],
        "decay_period_us": 100,
        "decay_switch_idx": 64,
        "decay_n": 256,
        "prbs_bits": None,
        "prbs_adc": [],
    }

    for line in text.splitlines():
        # Calibration
        m = re.search(r"IR cal: CLARO\s+min=\s*(\d+) max=\s*(\d+) mean=\s*(\d+)", line)
        if m:
            out["cal"]["claro"] = tuple(int(x) for x in m.groups())
            continue
        m = re.search(r"IR cal: ESCURO min=\s*(\d+) max=\s*(\d+) mean=\s*(\d+)", line)
        if m:
            out["cal"]["escuro"] = tuple(int(x) for x in m.groups())
            continue
        m = re.search(r"IR cal: swing=([+-]?\d+) baseline=(\d+) threshold=(\d+) hyst=(\d+)", line)
        if m:
            out["cal"]["swing"] = int(m.group(1))
            out["cal"]["baseline"] = int(m.group(2))
            out["cal"]["threshold"] = int(m.group(3))
            out["cal"]["hyst"] = int(m.group(4))
            continue

        # Area sweep
        m = re.search(r"area_x.*ADC@x:\s*(.+)$", line)
        if m:
            out["area_x"] = [int(v) for v in m.group(1).split()]
            continue
        m = re.search(r"area_y.*ADC@y:\s*(.+)$", line)
        if m:
            out["area_y"] = [int(v) for v in m.group(1).split()]
            continue
        m = re.search(
            r"area: peak X=(\d+) \(px.(\d+) / (\d+)\) \| Y=(\d+) \(px.(\d+) / (\d+)\)", line
        )
        if m:
            out["area_peak"] = {
                "x_idx": int(m.group(1)),
                "x_px": int(m.group(2)),
                "fb_w": int(m.group(3)),
                "y_idx": int(m.group(4)),
                "y_px": int(m.group(5)),
                "fb_h": int(m.group(6)),
            }
            continue

        # Decay header
        m = re.search(
            r"decay: trial=(\d+) period_us=(\d+) switch_idx=(\d+) N=(\d+)", line
        )
        if m:
            out["decay_period_us"] = int(m.group(2))
            out["decay_switch_idx"] = int(m.group(3))
            out["decay_n"] = int(m.group(4))
            out["decay_trials"].append({"trial": int(m.group(1)), "samples": []})
            continue
        m = re.search(r"decay_csv: i=\s*(\d+)\s+(.+)$", line)
        if m and out["decay_trials"]:
            out["decay_trials"][-1]["samples"].extend(int(v) for v in m.group(2).split())
            continue

        # PRBS
        m = re.search(r"prbs: bits=0x([0-9a-fA-F]+)", line)
        if m:
            out["prbs_bits"] = int(m.group(1), 16)
            continue
        m = re.search(
            r"prbs: bits_lo=0x([0-9a-fA-F]+)\s+bits_hi=0x([0-9a-fA-F]+)", line
        )
        if m:
            lo = int(m.group(1), 16)
            hi = int(m.group(2), 16)
            out["prbs_bits"] = lo | (hi << 64)
            continue
        m = re.search(r"prbs_csv: i=\s*(\d+)\s+(.+)$", line)
        if m:
            out["prbs_adc"].extend(int(v) for v in m.group(2).split())
            continue

    return out


def write_csvs(parsed: dict, outdir: Path) -> None:
    outdir.mkdir(parents=True, exist_ok=True)

    # cal.csv
    cal = parsed["cal"]
    if cal:
        with (outdir / "cal.csv").open("w") as f:
            f.write("phase,min,max,mean\n")
            if "claro" in cal:
                f.write(f"CLARO,{cal['claro'][0]},{cal['claro'][1]},{cal['claro'][2]}\n")
            if "escuro" in cal:
                f.write(f"ESCURO,{cal['escuro'][0]},{cal['escuro'][1]},{cal['escuro'][2]}\n")
            f.write("\n# derived\n")
            for k in ("swing", "baseline", "threshold", "hyst"):
                if k in cal:
                    f.write(f"{k},{cal[k]}\n")

    # area.csv (X and Y curves side-by-side)
    if parsed["area_x"] or parsed["area_y"]:
        with (outdir / "area.csv").open("w") as f:
            f.write("idx,adc_x,bar_x_px,adc_y,bar_y_px\n")
            xs = parsed["area_x"]
            ys = parsed["area_y"]
            ap = parsed["area_peak"]
            fb_w = ap.get("fb_w", 256)
            fb_h = ap.get("fb_h", 240)
            n = max(len(xs), len(ys))
            steps = max(n - 1, 1)
            for i in range(n):
                xv = xs[i] if i < len(xs) else ""
                yv = ys[i] if i < len(ys) else ""
                bx = int((i * (fb_w - 8)) / steps)
                by = int((i * (fb_h - 8)) / steps)
                f.write(f"{i},{xv},{bx},{yv},{by}\n")

    # decay.csv (each trial as a column)
    trials = parsed["decay_trials"]
    if trials:
        period_us = parsed["decay_period_us"]
        switch_idx = parsed["decay_switch_idx"]
        n = parsed["decay_n"]
        with (outdir / "decay.csv").open("w") as f:
            f.write(f"# period_us={period_us} switch_idx={switch_idx} N={n}\n")
            cols = ["sample_idx", "t_us", "phase"] + [f"trial{t['trial']}_adc" for t in trials]
            f.write(",".join(cols) + "\n")
            for i in range(n):
                t_us = i * period_us
                phase = "WHITE" if i < switch_idx else "DECAY"
                row = [str(i), str(t_us), phase]
                for t in trials:
                    row.append(str(t["samples"][i]) if i < len(t["samples"]) else "")
                f.write(",".join(row) + "\n")

    # prbs.csv
    if parsed["prbs_bits"] is not None and parsed["prbs_adc"]:
        bits = parsed["prbs_bits"]
        n = len(parsed["prbs_adc"])
        with (outdir / "prbs.csv").open("w") as f:
            f.write(f"# bits=0x{bits:016x} (LSB first)\n")
            f.write("bit_idx,bit,adc\n")
            for i in range(n):
                bit = (bits >> i) & 1
                f.write(f"{i},{bit},{parsed['prbs_adc'][i]}\n")


def analyze(parsed: dict) -> str:
    lines = []
    lines.append("=" * 64)
    lines.append("PRC RUN ANALYSIS")
    lines.append("=" * 64)

    cal = parsed["cal"]
    if cal:
        lines.append(
            f"Calibration: swing={cal.get('swing')} LSB "
            f"(CLARO mean={cal.get('claro', (0, 0, 0))[2]}, "
            f"ESCURO mean={cal.get('escuro', (0, 0, 0))[2]})"
        )
        lines.append(
            f"  threshold={cal.get('threshold')} hyst={cal.get('hyst')} "
            f"baseline={cal.get('baseline')}"
        )

    ap = parsed.get("area_peak", {})
    if ap:
        fb_w = ap.get("fb_w", 256)
        fb_h = ap.get("fb_h", 240)
        x_off = ap["x_px"] - fb_w // 2
        y_off = ap["y_px"] - fb_h // 2
        lines.append("")
        lines.append(
            f"Area peak: X={ap['x_px']}/{fb_w} (offset {x_off:+d}), "
            f"Y={ap['y_px']}/{fb_h} (offset {y_off:+d})"
        )

        # FWHM-ish: count positions above 50% of peak
        def fwhm(curve):
            if not curve:
                return 0
            peak = max(curve)
            half = peak * 0.5
            above = [i for i, v in enumerate(curve) if v >= half]
            return (above[-1] - above[0] + 1) if above else 0

        x_fwhm = fwhm(parsed["area_x"]) * (fb_w - 8) // 15
        y_fwhm = fwhm(parsed["area_y"]) * (fb_h - 8) // 15
        lines.append(f"  approx FWHM: X≈{x_fwhm} px, Y≈{y_fwhm} lines")

    trials = parsed.get("decay_trials", [])
    if trials:
        period_us = parsed["decay_period_us"]
        switch_idx = parsed["decay_switch_idx"]
        # Average across trials
        n = parsed["decay_n"]
        avg = []
        for i in range(n):
            vals = [t["samples"][i] for t in trials if i < len(t["samples"])]
            avg.append(sum(vals) / len(vals) if vals else 0)
        pre_mean = sum(avg[:switch_idx]) / switch_idx
        post_mean = sum(avg[switch_idx:]) / (n - switch_idx)
        decay_pct = (1 - post_mean / pre_mean) * 100 if pre_mean else 0
        capture_ms = (n - switch_idx) * period_us / 1000
        lines.append("")
        lines.append(
            f"Decay (avg of {len(trials)} trials, {capture_ms:.1f} ms post-switch):"
        )
        lines.append(f"  pre_mean={pre_mean:.0f}  post_mean={post_mean:.0f}  "
                     f"decay={decay_pct:.1f}%")
        if abs(decay_pct) < 5:
            lines.append(
                "  → phosphor persistence dominates 25 ms window; need longer capture"
            )

    if parsed.get("prbs_adc") and parsed.get("prbs_bits") is not None:
        bits = parsed["prbs_bits"]
        adc = parsed["prbs_adc"]
        # rise = transitions where bit goes 0→1, measure ADC immediately
        # fall = transitions 1→0, count how many bits until ADC < threshold
        thr = cal.get("threshold", 500) if cal else 500
        baseline = cal.get("baseline", 1500) if cal else 1500
        rise_lat = []
        fall_lat = []
        for i in range(1, len(adc)):
            prev = (bits >> (i - 1)) & 1
            cur = (bits >> i) & 1
            if prev == 0 and cur == 1:
                # rise: how many bits ahead until ADC > baseline?
                for k in range(i, min(i + 8, len(adc))):
                    if adc[k] > baseline + thr:
                        rise_lat.append(k - i)
                        break
            elif prev == 1 and cur == 0:
                # fall: how many bits until ADC < baseline?
                for k in range(i, min(i + 16, len(adc))):
                    if adc[k] < baseline - thr:
                        fall_lat.append(k - i)
                        break
        lines.append("")
        lines.append("PRBS asymmetry:")
        if rise_lat:
            r_mean = sum(rise_lat) / len(rise_lat)
            lines.append(
                f"  rise events: {len(rise_lat)}, latency (bits) "
                f"min={min(rise_lat)} mean={r_mean:.1f}"
            )
        else:
            lines.append("  rise events: 0")
        if fall_lat:
            f_mean = sum(fall_lat) / len(fall_lat)
            lines.append(
                f"  fall events: {len(fall_lat)}, latency (bits) "
                f"min={min(fall_lat)} mean={f_mean:.1f}"
            )
        else:
            lines.append("  fall events: 0 (phosphor persistence too long)")

    lines.append("=" * 64)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM1")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=22.0)
    ap.add_argument("--no-reset", action="store_true",
                    help="skip DTR/RTS pulse (already-running session)")
    args = ap.parse_args()

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    outdir = TMP_DIR / ts
    print(f"# capturing {args.duration}s from {args.port} → {outdir}")
    text = reset_and_capture(args.port, args.baud, args.duration)

    raw = outdir / "raw.log"
    outdir.mkdir(parents=True, exist_ok=True)
    raw.write_text(text)

    parsed = parse_run(text)
    write_csvs(parsed, outdir)
    print(analyze(parsed))
    print(f"\n# files written to {outdir}/")
    for p in sorted(outdir.glob("*.csv")):
        print(f"  {p.relative_to(PROJECT_ROOT)}")
    latest = TMP_DIR / "latest"
    if latest.exists() or latest.is_symlink():
        latest.unlink()
    latest.symlink_to(outdir.name)
    print(f"  tmp/prc_runs/latest -> {outdir.name}")


if __name__ == "__main__":
    main()
