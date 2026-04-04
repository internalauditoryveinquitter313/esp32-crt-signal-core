#!/usr/bin/env Rscript
# ──────────────────────────────────────────────────────────────────────
# CRT Flicker & Signal Stability Analysis
#
# Scientific analysis of frame-by-frame recordings from the CRT monitor.
# Metrics: temporal luminance stability, spatial uniformity, spectral
# content of brightness oscillation, frame-to-frame delta.
#
# Usage:
#   Rscript crt_flicker_analysis.R <recording_dir> [output_dir]
#
# Requires: imager, ggplot2, signal, pracma, viridis, patchwork
# ──────────────────────────────────────────────────────────────────────

.libPaths(c("~/R/library", .libPaths()))
suppressPackageStartupMessages({
  library(imager)
  library(ggplot2)
  library(signal)
  library(pracma)
  library(viridis)
  library(patchwork)
})

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 1) {
  cat("Usage: Rscript crt_flicker_analysis.R <recording_dir> [output_dir]\n")
  quit(status = 1)
}

rec_dir    <- args[1]
out_dir    <- if (length(args) >= 2) args[2] else file.path(rec_dir, "analysis")
dir.create(out_dir, showWarnings = FALSE, recursive = TRUE)

frames <- sort(list.files(rec_dir, pattern = "frame_\\d+\\.jpg$", full.names = TRUE))
N      <- length(frames)
cat(sprintf("Recording: %s (%d frames)\n", rec_dir, N))
if (N < 3) stop("Need at least 3 frames for analysis")

fps <- 30  # C270 capture rate

# ── 1. Extract per-frame luminance statistics ────────────────────────

cat("Extracting luminance statistics...\n")
luma_mean   <- numeric(N)
luma_sd     <- numeric(N)
luma_median <- numeric(N)
luma_p05    <- numeric(N)
luma_p95    <- numeric(N)
delta_mean  <- numeric(N)

prev_luma <- NULL
for (i in seq_len(N)) {
  img <- load.image(frames[i])
  # Convert to grayscale (CIE luminance: 0.2126R + 0.7152G + 0.0722B)
  gray <- grayscale(img)
  vals <- as.numeric(gray)

  luma_mean[i]   <- mean(vals)
  luma_sd[i]     <- sd(vals)
  luma_median[i] <- median(vals)
  luma_p05[i]    <- quantile(vals, 0.05)
  luma_p95[i]    <- quantile(vals, 0.95)

  if (!is.null(prev_luma)) {
    delta_mean[i] <- mean(abs(as.numeric(gray) - prev_luma))
  }
  prev_luma <- as.numeric(gray)

  if (i %% 30 == 0) cat(sprintf("  frame %d/%d\n", i, N))
}
delta_mean[1] <- NA

time_s <- (seq_len(N) - 1) / fps

# ── 2. Temporal luminance stability ──────────────────────────────────

df_temporal <- data.frame(
  time      = time_s,
  frame     = seq_len(N),
  mean      = luma_mean,
  sd        = luma_sd,
  median    = luma_median,
  p05       = luma_p05,
  p95       = luma_p95,
  delta     = delta_mean
)

# Coefficient of variation (flicker metric)
cv <- sd(luma_mean) / mean(luma_mean) * 100
flicker_index <- max(luma_mean) - min(luma_mean)

cat(sprintf("\n── Temporal Stability ──\n"))
cat(sprintf("  Mean luminance:        %.4f ± %.4f\n", mean(luma_mean), sd(luma_mean)))
cat(sprintf("  Coefficient of var:    %.2f%%\n", cv))
cat(sprintf("  Flicker index (Δmax):  %.4f\n", flicker_index))
cat(sprintf("  Frame-to-frame Δ:      %.4f ± %.4f\n",
            mean(delta_mean, na.rm = TRUE), sd(delta_mean, na.rm = TRUE)))

# ── 3. Spectral analysis of luminance oscillation ────────────────────

cat("\nComputing FFT of luminance time series...\n")

# Detrend and window
luma_detrended <- luma_mean - mean(luma_mean)
win <- hamming(N)
luma_windowed  <- luma_detrended * win

# FFT
fft_result <- fft(luma_windowed)
power      <- Mod(fft_result[1:(N %/% 2 + 1)])^2
freqs      <- (0:(N %/% 2)) * fps / N

df_spectral <- data.frame(
  freq  = freqs,
  power = power / max(power)  # normalize to peak
)

# Find dominant frequency (skip DC)
peak_idx  <- which.max(power[-1]) + 1
peak_freq <- freqs[peak_idx]
peak_db   <- 10 * log10(power[peak_idx] / mean(power[-1]))
cat(sprintf("  Dominant frequency:    %.2f Hz (%.1f dB above noise floor)\n",
            peak_freq, peak_db))

# ── 4. Outlier detection (dropped frames / glitches) ─────────────────

cat("\nDetecting outlier frames...\n")
delta_clean <- delta_mean[!is.na(delta_mean)]
iqr_delta   <- IQR(delta_clean)
q3_delta    <- quantile(delta_clean, 0.75)
threshold   <- q3_delta + 3 * iqr_delta  # conservative 3× IQR
outliers    <- which(delta_mean > threshold)

if (length(outliers) > 0) {
  cat(sprintf("  ⚠ %d outlier frames detected (threshold=%.4f):\n", length(outliers), threshold))
  for (o in outliers) {
    cat(sprintf("    frame %04d (t=%.3fs) Δ=%.4f\n", o - 1, time_s[o], delta_mean[o]))
  }
} else {
  cat("  No outlier frames detected — signal stable\n")
}

# ── 5. Generate publication plots ────────────────────────────────────

cat("\nGenerating plots...\n")
theme_crt <- theme_minimal(base_size = 11) +
  theme(
    plot.background  = element_rect(fill = "#0a0a0a", color = NA),
    panel.background = element_rect(fill = "#0a0a0a", color = NA),
    panel.grid.major = element_line(color = "#1a1a1a"),
    panel.grid.minor = element_blank(),
    text  = element_text(color = "#00ff88", family = "mono"),
    axis.text  = element_text(color = "#00cc66"),
    axis.title = element_text(color = "#00ff88"),
    plot.title = element_text(color = "#00ffaa", face = "bold", size = 13),
    plot.subtitle = element_text(color = "#009966", size = 9)
  )

# P1: Temporal luminance
p1 <- ggplot(df_temporal, aes(x = time)) +
  geom_ribbon(aes(ymin = p05, ymax = p95), fill = "#003322", alpha = 0.5) +
  geom_line(aes(y = mean), color = "#00ff88", linewidth = 0.5) +
  geom_line(aes(y = median), color = "#00aaff", linewidth = 0.3, linetype = "dashed") +
  labs(
    title = "Temporal Luminance Stability",
    subtitle = sprintf("CV=%.2f%%  Flicker Index=%.4f  N=%d frames @ %d fps", cv, flicker_index, N, fps),
    x = "Time (s)", y = "Luminance (0–1)"
  ) +
  theme_crt

# P2: Frame-to-frame delta
p2 <- ggplot(df_temporal[-1, ], aes(x = time, y = delta)) +
  geom_line(color = "#ff6644", linewidth = 0.4) +
  geom_hline(yintercept = threshold, color = "#ffaa00", linetype = "dashed", linewidth = 0.3) +
  { if (length(outliers) > 0)
      geom_point(data = df_temporal[outliers, ], aes(x = time, y = delta),
                 color = "#ff0000", size = 2)
    else NULL } +
  labs(
    title = "Frame-to-Frame Delta",
    subtitle = sprintf("Mean Δ=%.4f  Outlier threshold=%.4f  Outliers=%d",
                       mean(delta_clean), threshold, length(outliers)),
    x = "Time (s)", y = "|Δ luminance|"
  ) +
  theme_crt

# P3: Power spectrum
p3 <- ggplot(df_spectral[-1, ], aes(x = freq, y = 10 * log10(power + 1e-10))) +
  geom_line(color = "#aa88ff", linewidth = 0.5) +
  geom_vline(xintercept = peak_freq, color = "#ffaa00", linetype = "dashed", linewidth = 0.3) +
  annotate("text", x = peak_freq + 0.3, y = max(10 * log10(df_spectral$power[-1] + 1e-10)) - 3,
           label = sprintf("%.1f Hz", peak_freq), color = "#ffaa00", size = 3, hjust = 0) +
  labs(
    title = "Luminance Power Spectrum",
    subtitle = sprintf("Dominant: %.2f Hz (%.1f dB)  Nyquist: %.1f Hz", peak_freq, peak_db, fps / 2),
    x = "Frequency (Hz)", y = "Power (dB)"
  ) +
  theme_crt

# P4: Luminance histogram across all frames
p4 <- ggplot(df_temporal, aes(x = mean)) +
  geom_histogram(bins = 30, fill = "#00aa66", color = "#003322", alpha = 0.8) +
  geom_vline(xintercept = mean(luma_mean), color = "#00ffaa", linetype = "dashed") +
  labs(
    title = "Luminance Distribution",
    subtitle = sprintf("μ=%.4f  σ=%.4f  range=[%.4f, %.4f]",
                       mean(luma_mean), sd(luma_mean), min(luma_mean), max(luma_mean)),
    x = "Mean frame luminance", y = "Count"
  ) +
  theme_crt

# Compose
composite <- (p1 | p3) / (p2 | p4) +
  plot_annotation(
    title    = "CRT Signal Flicker Analysis",
    subtitle = sprintf("%s — %d frames @ %d fps", basename(rec_dir), N, fps),
    theme = theme_crt + theme(
      plot.title = element_text(size = 16, face = "bold"),
      plot.subtitle = element_text(size = 10)
    )
  )

out_path <- file.path(out_dir, "flicker_analysis.png")
ggsave(out_path, composite, width = 16, height = 10, dpi = 150, bg = "#0a0a0a")
cat(sprintf("  Saved: %s\n", out_path))

# ── 6. Save data ─────────────────────────────────────────────────────

csv_path <- file.path(out_dir, "frame_stats.csv")
write.csv(df_temporal, csv_path, row.names = FALSE)
cat(sprintf("  Saved: %s\n", csv_path))

spectral_path <- file.path(out_dir, "spectral.csv")
write.csv(df_spectral, spectral_path, row.names = FALSE)
cat(sprintf("  Saved: %s\n", spectral_path))

cat("\n── Summary ──\n")
cat(sprintf("  Signal quality:  %s\n",
    if (cv < 1) "EXCELLENT" else if (cv < 3) "GOOD" else if (cv < 5) "FAIR" else "POOR"))
cat(sprintf("  Flicker:         %s\n",
    if (flicker_index < 0.02) "NONE" else if (flicker_index < 0.05) "MINIMAL" else "DETECTED"))
cat(sprintf("  Dropped frames:  %d\n", length(outliers)))
cat("Done.\n")
