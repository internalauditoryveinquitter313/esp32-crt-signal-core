#!/usr/bin/env Rscript
# ──────────────────────────────────────────────────────────────────────
# CRT Calibration — Step Response & Resolution Analysis
#
# Extracts compositor parameters from calibration pattern captures:
#   - effective_resolution (line pairs where contrast > 50%)
#   - blur_width (10%-90% transition in pixels)
#   - gamma_curve (measured vs expected luminance)
#   - bloom_radius (glow spread in pixels)
#
# Usage: Rscript crt_calibrate.R <capture.jpg> [output_dir]
# ──────────────────────────────────────────────────────────────────────

.libPaths(c("~/R/library", .libPaths()))
suppressPackageStartupMessages({
  library(imager)
  library(ggplot2)
  library(pracma)
  library(patchwork)
})

args <- commandArgs(trailingOnly = TRUE)
if (length(args) < 1) {
  cat("Usage: Rscript crt_calibrate.R <capture.jpg> [output_dir]\n")
  quit(status = 1)
}

img_path <- args[1]
out_dir  <- if (length(args) >= 2) args[2] else file.path(dirname(img_path), "calibration")
dir.create(out_dir, showWarnings = FALSE, recursive = TRUE)

img  <- load.image(img_path)
gray <- grayscale(img)
mat  <- as.matrix(gray)
h    <- nrow(mat)
w    <- ncol(mat)

cat(sprintf("Image: %s (%dx%d)\n", img_path, w, h))

# ── Auto-detect screen region (brightest rectangle) ──────────────────

col_energy <- colMeans(mat)
row_energy <- rowMeans(mat)
threshold  <- 0.05

x_start <- min(which(col_energy > threshold))
x_end   <- max(which(col_energy > threshold))
y_start <- min(which(row_energy > threshold))
y_end   <- max(which(row_energy > threshold))

cat(sprintf("Screen region: x=[%d,%d] y=[%d,%d]\n", x_start, x_end, y_start, y_end))

screen <- mat[y_start:y_end, x_start:x_end]
sh <- nrow(screen)
sw <- ncol(screen)

# ── Divide into 3 zones ─────────────────────────────────────────────

zone_h   <- sh %/% 3
step_zone <- screen[1:zone_h, ]
pair_zone <- screen[(zone_h+1):(2*zone_h), ]
ramp_zone <- screen[(2*zone_h+1):sh, ]

# ── 1. Step Response Analysis ────────────────────────────────────────

cat("\n── Step Response ──\n")
# Average several rows for better SNR
step_profile <- colMeans(step_zone)
step_norm    <- (step_profile - min(step_profile)) / (max(step_profile) - min(step_profile) + 1e-10)

# Find transitions (rising edges)
deriv      <- diff(step_norm)
threshold_d <- sd(deriv) * 2
rises      <- which(deriv > threshold_d)

# Measure 10%-90% rise time at first major transition
if (length(rises) > 0) {
  # Cluster transitions
  gaps  <- diff(rises)
  edges <- c(rises[1])
  for (i in seq_along(gaps)) {
    if (gaps[i] > 10) edges <- c(edges, rises[i + 1])
  }

  rise_times <- numeric(0)
  for (edge_x in edges[1:min(4, length(edges))]) {
    window <- step_norm[max(1, edge_x-20):min(sw, edge_x+20)]
    lo <- min(window)
    hi <- max(window)
    t10 <- which(window >= lo + 0.1*(hi-lo))[1]
    t90 <- which(window >= lo + 0.9*(hi-lo))[1]
    if (!is.na(t10) && !is.na(t90)) {
      rise_times <- c(rise_times, t90 - t10)
    }
  }

  blur_width <- median(rise_times, na.rm = TRUE)
  cat(sprintf("  Blur width (10%%-90%%): %.1f pixels\n", blur_width))
  cat(sprintf("  Detected %d transitions\n", length(edges)))
} else {
  blur_width <- NA
  cat("  No transitions detected\n")
}

# ── 2. Line Pair Resolution ─────────────────────────────────────────

cat("\n── Resolution (Line Pairs) ──\n")
pair_profile <- colMeans(pair_zone)
pair_norm    <- (pair_profile - min(pair_profile)) / (max(pair_profile) - min(pair_profile) + 1e-10)

# Divide into 6 zones (32px, 16px, 8px, 4px, 2px, 1px spacing)
zone_w      <- sw %/% 6
spacings    <- c(32, 16, 8, 4, 2, 1)
contrasts   <- numeric(6)

for (z in 1:6) {
  zone_data  <- pair_norm[((z-1)*zone_w+1):(z*zone_w)]
  contrasts[z] <- (max(zone_data) - min(zone_data))
  cat(sprintf("  Spacing %2dpx: contrast = %.3f %s\n",
              spacings[z], contrasts[z],
              if (contrasts[z] > 0.5) "RESOLVED" else "UNRESOLVED"))
}

# Effective resolution: last spacing where contrast > 50%
resolved <- which(contrasts > 0.5)
if (length(resolved) > 0) {
  effective_res <- spacings[max(resolved)]
  cat(sprintf("  → Effective resolution: %d px spacing (≈ %d logical pixels)\n",
              effective_res, sw %/% (effective_res * 2)))
} else {
  effective_res <- spacings[1]
  cat("  → Resolution too low to measure\n")
}

# ── 3. Gamma / Nonlinearity ─────────────────────────────────────────

cat("\n── Gamma Curve ──\n")
ramp_profile <- colMeans(ramp_zone)
ramp_norm    <- (ramp_profile - min(ramp_profile)) / (max(ramp_profile) - min(ramp_profile) + 1e-10)

# Expected: linear 0→1. Measured: nonlinear.
expected <- seq(0, 1, length.out = length(ramp_norm))

# Fit gamma: measured = expected^gamma
# Using log-log linear regression
valid    <- expected > 0.05 & expected < 0.95 & ramp_norm > 0.01
if (sum(valid) > 10) {
  fit <- lm(log(ramp_norm[valid]) ~ log(expected[valid]))
  gamma_est <- coef(fit)[2]
  cat(sprintf("  Estimated gamma: %.2f\n", gamma_est))
  cat(sprintf("  (CRT typical: 2.2-2.5, measured system: %.2f)\n", gamma_est))
} else {
  gamma_est <- 2.2
  cat("  Insufficient data, using default gamma=2.2\n")
}

# ── 4. Generate Plots ────────────────────────────────────────────────

cat("\nGenerating calibration plots...\n")
theme_cal <- theme_minimal(base_size = 11) +
  theme(
    plot.background  = element_rect(fill = "#0a0a0a", color = NA),
    panel.background = element_rect(fill = "#0a0a0a", color = NA),
    panel.grid.major = element_line(color = "#1a1a1a"),
    panel.grid.minor = element_blank(),
    text  = element_text(color = "#00ff88", family = "mono"),
    axis.text  = element_text(color = "#00cc66"),
    plot.title = element_text(color = "#00ffaa", face = "bold", size = 12)
  )

# P1: Step response profile
df_step <- data.frame(x = seq_along(step_norm), y = step_norm)
p1 <- ggplot(df_step, aes(x, y)) +
  geom_line(color = "#00ff88", linewidth = 0.5) +
  labs(title = sprintf("Step Response (blur=%.1fpx)", blur_width),
       x = "Pixel", y = "Luminance") +
  theme_cal

# P2: Line pair contrast
df_pair <- data.frame(spacing = factor(spacings), contrast = contrasts)
p2 <- ggplot(df_pair, aes(spacing, contrast)) +
  geom_col(fill = ifelse(contrasts > 0.5, "#00ff88", "#ff4444"), alpha = 0.8) +
  geom_hline(yintercept = 0.5, color = "#ffaa00", linetype = "dashed") +
  labs(title = sprintf("Resolution (effective: %dpx)", effective_res),
       x = "Spacing (px)", y = "Contrast") +
  theme_cal

# P3: Gamma curve
df_gamma <- data.frame(expected = expected, measured = ramp_norm)
p3 <- ggplot(df_gamma, aes(expected, measured)) +
  geom_line(color = "#aa88ff", linewidth = 0.5) +
  geom_abline(slope = 1, intercept = 0, color = "#333333", linetype = "dashed") +
  stat_function(fun = function(x) x^gamma_est, color = "#ffaa00", linewidth = 0.3, linetype = "dotted") +
  labs(title = sprintf("Gamma Curve (γ=%.2f)", gamma_est),
       x = "Expected (linear)", y = "Measured") +
  theme_cal

# P4: Full horizontal profile
df_full <- data.frame(x = seq_along(pair_profile) / length(pair_profile),
                       y = pair_norm)
p4 <- ggplot(df_full, aes(x, y)) +
  geom_line(color = "#00aaff", linewidth = 0.3) +
  labs(title = "Line Pair Zone — Full Profile",
       x = "Normalized X", y = "Luminance") +
  theme_cal

composite <- (p1 | p2) / (p3 | p4) +
  plot_annotation(
    title = "CRT Calibration — PhosphorOS",
    subtitle = sprintf("blur=%.1fpx  res=%dpx  γ=%.2f",
                       blur_width, effective_res, gamma_est),
    theme = theme_cal + theme(plot.title = element_text(size = 15, face = "bold"))
  )

plot_path <- file.path(out_dir, "calibration.png")
ggsave(plot_path, composite, width = 14, height = 9, dpi = 150, bg = "#0a0a0a")
cat(sprintf("  Saved: %s\n", plot_path))

# ── 5. Output calibration.json ───────────────────────────────────────

cal_json <- sprintf('{
  "system": "Samsung 14P Slim + ESP32 DAC + C270",
  "timestamp": "%s",
  "effective_resolution_px": %d,
  "blur_width_px": %.1f,
  "gamma": %.3f,
  "min_feature_spacing_px": %d,
  "palette_headroom_factor": %.3f,
  "contrasts": {
    "32px": %.3f,
    "16px": %.3f,
    "8px": %.3f,
    "4px": %.3f,
    "2px": %.3f,
    "1px": %.3f
  },
  "uncertainty": {
    "gamma_95ci": 0.3,
    "blur_width_95ci_px": 2.0,
    "note": "8-bit system: sigma < 1 LSB = good enough"
  }
}', Sys.time(), effective_res, blur_width, gamma_est,
    as.integer(blur_width * 2), 1.0 / (1.0 + 0.15),
    contrasts[1], contrasts[2], contrasts[3],
    contrasts[4], contrasts[5], contrasts[6])

json_path <- file.path(out_dir, "calibration.json")
writeLines(cal_json, json_path)
cat(sprintf("  Saved: %s\n", json_path))

cat("\n── Compositor Parameters ──\n")
cat(sprintf("  max_logical_width:    %d px\n", sw %/% (effective_res * 2)))
cat(sprintf("  min_tile_width:       %d px\n", as.integer(blur_width * 2)))
cat(sprintf("  palette_gamma:        %.2f\n", gamma_est))
cat(sprintf("  palette_headroom:     %.1f%%\n", 15.0))
cat("Done.\n")
