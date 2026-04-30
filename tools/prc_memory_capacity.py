#!/usr/bin/env python3
"""Memory Capacity benchmark of the CRT-IR-ring reservoir.

Standard PRC benchmark (Jaeger 2002): for each delay k, train a linear
readout to predict u[t-k] from the reservoir state x[t]. The R² of the
prediction is MC[k]; the total Memory Capacity is MC = Σ_k MC[k].

State construction from the available data:
  * The PRBS log gives one ADC value per bit.
  * To build a richer reservoir state, we use a sliding window of the
    last W ADC samples — the reservoir's "fading memory" is then probed
    by training W weights to reconstruct u[t-k].

A high MC means the system stores past inputs in a linearly recoverable
form. For an exponential reservoir with τ ≫ bit-width, MC saturates to
W (the state dimensionality) when k is small.

Outputs:
  mc.csv     — k, MC(k)
  mc_fit.txt — total MC + per-delay table

Usage:
  python tools/prc_memory_capacity.py            # latest run
  python tools/prc_memory_capacity.py --window 8 # readout window size
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import numpy as np


PROJECT_ROOT = Path(__file__).resolve().parent.parent
TMP_DIR = PROJECT_ROOT / "tmp" / "prc_runs"


def load_prbs(path: Path):
    bits, adc = [], []
    with path.open() as f:
        for line in f:
            if line.startswith("#") or line.startswith("bit_idx"):
                continue
            parts = line.strip().split(",")
            if len(parts) >= 3:
                bits.append(int(parts[1]))
                adc.append(int(parts[2]))
    return np.array(bits), np.array(adc, dtype=float)


def load_mprbs(path: Path):
    """Load 4-quadrant multi-PRBS. Returns (bits matrix Nx4, adc array N)."""
    rows = []
    with path.open() as f:
        for line in f:
            if line.startswith("#") or line.startswith("tick"):
                continue
            parts = line.strip().split(",")
            if len(parts) >= 7:
                rows.append([int(parts[1]), int(parts[2]), int(parts[3]),
                             int(parts[4]), int(parts[6])])
    arr = np.array(rows)
    return arr[:, :4], arr[:, 4].astype(float)


def build_state_matrix(adc: np.ndarray, window: int) -> np.ndarray:
    """X[t] = [adc[t], adc[t-1], ..., adc[t-window+1]] for t >= window-1."""
    n = len(adc) - (window - 1)
    X = np.zeros((n, window))
    for k in range(window):
        X[:, k] = adc[(window - 1) - k : (window - 1) - k + n]
    return X


def mc_at_delay(bits: np.ndarray, X: np.ndarray, k: int, ridge: float) -> tuple[float, float]:
    """Train ridge regression to predict u[t-k] from x[t]; return (R^2, var(u))."""
    # Align: X has n rows starting at t = window-1. We need t-k valid.
    n = X.shape[0]
    window = X.shape[1]
    t_offsets = np.arange(window - 1, window - 1 + n)
    valid = t_offsets - k >= 0
    if valid.sum() < 10:
        return 0.0, 0.0
    Xv = X[valid]
    target_idx = t_offsets[valid] - k
    yv = bits[target_idx].astype(float)
    yv_centered = yv - yv.mean()
    if yv_centered.var() == 0:
        return 0.0, 0.0
    # Train/test split — first 70% train, last 30% test
    n_tr = int(len(Xv) * 0.7)
    Xt, yt = Xv[:n_tr], yv[:n_tr]
    Xv2, yv2 = Xv[n_tr:], yv[n_tr:]
    # Center features
    mu_X = Xt.mean(axis=0)
    Xt_c = Xt - mu_X
    Xv_c = Xv2 - mu_X
    mu_y = yt.mean()
    yt_c = yt - mu_y
    # Ridge regression closed form
    A = Xt_c.T @ Xt_c + ridge * np.eye(window)
    w = np.linalg.solve(A, Xt_c.T @ yt_c)
    y_pred = Xv_c @ w + mu_y
    ss_res = np.sum((yv2 - y_pred) ** 2)
    ss_tot = np.sum((yv2 - yv2.mean()) ** 2)
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0
    return max(r2, 0.0), float(yv.var())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", default=None)
    ap.add_argument("--window", type=int, default=8,
                    help="readout window size (number of past ADC samples)")
    ap.add_argument("--max-delay", type=int, default=15)
    ap.add_argument("--ridge", type=float, default=1e-3)
    args = ap.parse_args()

    if args.run:
        run_dir = TMP_DIR / args.run
    else:
        latest = TMP_DIR / "latest"
        run_dir = latest.resolve() if latest.is_symlink() else None
        if run_dir is None:
            runs = sorted([p for p in TMP_DIR.iterdir() if p.is_dir()])
            run_dir = runs[-1] if runs else None
    if run_dir is None or not run_dir.exists():
        print("no run found", file=sys.stderr)
        sys.exit(1)

    print(f"# analyzing {run_dir}")

    # Prefer gprbs.csv if present
    gprbs_path = run_dir / "gprbs.csv"
    if gprbs_path.exists():
        lvls = []
        adcs = []
        with gprbs_path.open() as f:
            for line in f:
                if line.startswith("#") or line.startswith("tick"):
                    continue
                parts = line.strip().split(",")
                if len(parts) >= 4:
                    lvls.append(int(parts[1]))
                    adcs.append(int(parts[3]))
        lvls = np.array(lvls, dtype=float)
        adc = np.array(adcs, dtype=float)
        print(f"# gray-level PRBS: {len(lvls)} ticks, ADC range {adc.min():.0f}..{adc.max():.0f}")
        X = build_state_matrix(adc, args.window)
        mc_per_k = []
        for k in range(args.max_delay + 1):
            r2, _ = mc_at_delay(lvls, X, k, args.ridge)
            mc_per_k.append(r2)
        total_mc = sum(mc_per_k)
        print("=" * 64)
        print(f"GRAY-LEVEL PRBS — predict u[t-k] (level idx 0..3) from x[t]")
        print(f"  window W = {args.window}, ridge λ = {args.ridge}")
        print("=" * 64)
        for k, mc in enumerate(mc_per_k):
            bar = "#" * int(mc * 40)
            print(f"  k={k:2d} | {mc:.4f}  {bar}")
        print("=" * 64)
        print(f"  total MC = {total_mc:.4f}")
        print(f"  upper bound (W) = {args.window}, ratio = "
              f"{100*total_mc/args.window:.1f}% of W")
        print("=" * 64)

        out_csv = run_dir / "mc_gray.csv"
        with out_csv.open("w") as f:
            f.write("delay_k,mc\n")
            for k, mc in enumerate(mc_per_k):
                f.write(f"{k},{mc:.6f}\n")
            f.write(f"# total_mc={total_mc:.6f} window={args.window} ridge={args.ridge}\n")
        print(f"# saved {out_csv.relative_to(PROJECT_ROOT)}")
        return

    mprbs_path = run_dir / "mprbs.csv"
    if mprbs_path.exists():
        bits_matrix, adc = load_mprbs(mprbs_path)
        print(f"# multi-input mode: {bits_matrix.shape[0]} ticks × {bits_matrix.shape[1]} quadrants")
        X = build_state_matrix(adc, args.window)
        total_mc_global = 0.0
        per_quadrant = []
        for q in range(bits_matrix.shape[1]):
            bits_q = bits_matrix[:, q]
            mc_per_k = []
            for k in range(args.max_delay + 1):
                r2, _ = mc_at_delay(bits_q, X, k, args.ridge)
                mc_per_k.append(r2)
            tot = sum(mc_per_k)
            per_quadrant.append(mc_per_k)
            total_mc_global += tot
            print(f"\n--- quadrant {q} (TL=0,TR=1,BL=2,BR=3) — MC = {tot:.4f} ---")
            for k, mc in enumerate(mc_per_k):
                bar = "#" * int(mc * 40)
                print(f"  k={k:2d} | {mc:.4f}  {bar}")
        print("\n" + "=" * 64)
        print(f"  total MC across 4 quadrants = {total_mc_global:.4f}")
        print(f"  theoretical upper bound     = {4 * args.window:.1f} (4 × W)")
        print(f"  ratio                       = "
              f"{100*total_mc_global/(4*args.window):.1f}% of 4·W")
        print("=" * 64)

        out_csv = run_dir / "mc_multi.csv"
        with out_csv.open("w") as f:
            f.write("delay_k," + ",".join(f"q{q}" for q in range(len(per_quadrant))) + "\n")
            for k in range(args.max_delay + 1):
                f.write(f"{k}," + ",".join(f"{per_quadrant[q][k]:.6f}"
                                           for q in range(len(per_quadrant))) + "\n")
            f.write(f"# total_mc={total_mc_global:.6f} window={args.window} ridge={args.ridge}\n")
        print(f"# saved {out_csv.relative_to(PROJECT_ROOT)}")
        return

    bits, adc = load_prbs(run_dir / "prbs.csv")
    if len(bits) < args.window + args.max_delay + 10:
        print(f"# only {len(bits)} bits — need at least {args.window + args.max_delay + 10}",
              file=sys.stderr)
        sys.exit(2)

    X = build_state_matrix(adc, args.window)
    mc_per_k = []
    for k in range(args.max_delay + 1):
        r2, _ = mc_at_delay(bits, X, k, args.ridge)
        mc_per_k.append(r2)
    total_mc = sum(mc_per_k)

    # Output table
    print("=" * 64)
    print(f"MEMORY CAPACITY of CRT-IR reservoir  (n_bits={len(bits)})")
    print(f"  window W = {args.window}  ridge λ = {args.ridge}")
    print("=" * 64)
    print(f"  delay k | MC(k) = R²(û[t-k] | x[t])")
    print(f"  --------+--------------------------")
    for k, mc in enumerate(mc_per_k):
        bar = "#" * int(mc * 40)
        print(f"  {k:7d} | {mc:.4f}  {bar}")
    print(f"  --------+--------------------------")
    print(f"   total  | MC = Σ_k MC(k) = {total_mc:.4f}")
    print(f"   max     | W = {args.window} (theoretical upper bound)")
    print(f"   ratio  | {total_mc/args.window*100:.1f}% of W")
    print("=" * 64)

    # Save CSV
    out_csv = run_dir / "mc.csv"
    with out_csv.open("w") as f:
        f.write("delay_k,mc\n")
        for k, mc in enumerate(mc_per_k):
            f.write(f"{k},{mc:.6f}\n")
        f.write(f"# total_mc={total_mc:.6f} window={args.window} ridge={args.ridge}\n")
    print(f"# saved {out_csv.relative_to(PROJECT_ROOT)}")


if __name__ == "__main__":
    main()
