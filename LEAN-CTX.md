# lean-ctx: ESP32 CRT Signal Core Context Config

lean-ctx = transparent shell hook (auto-compresses CLI output) + MCP server (advanced tools).

## CRP v2 — Compact Response Protocol

Every token costs money. This applies to input, output, AND thinking tokens.

### Thinking Reduction (saves 30-60% thinking tokens)

1. **Parse task first, then act.** Don't explore when you already know the answer.
2. **One hypothesis, test it.** Don't enumerate 5 approaches — pick the most likely, try it.
3. **Stop thinking when you have the answer.** Don't continue analyzing alternatives after finding the fix.
4. **Use structured context.** lean-ctx headers tell you deps/exports — don't re-read files.
5. **File ref tracking:** F1=crt_core.c means F1 everywhere in this session. Don't re-resolve paths.

### Output Reduction (saves 50-80% output tokens)

1. NO prose. Just code, commands, and results.
2. NO echoing content that was just read or generated.
3. Summarize tool results: 1 line max.
4. Show edits only — not surrounding unchanged code.
5. Batch tool calls. One message, multiple calls.
6. Never ask "shall I proceed?" — just do it.
7. Bullets > paragraphs. Tables > lists.
8. Completion: 1-2 sentences. No recaps.

### Compact Notation

- `F:path` — reading file (not "I'll now read the file at path...")
- `+file` = created, `~file` = modified, `!file` = error
- `->` for results: "build -> OK, 0 warnings"

## MCP Tool Selection

| Built-in     | lean-ctx   | Saving        | ESP32 CRT Use                              |
|--------------|------------|---------------|--------------------------------------------|
| Read         | ctx_read   | -90% re-reads | Component sources, headers, timing tables  |
| Shell/Bash   | ctx_shell  | -60-90%       | idf.py build output, gcc test output       |
| ls/find/Glob | ctx_tree   | -40%          | Component directory mapping                |
| Grep         | ctx_search | -50%          | Hook references, stage pipeline, constants |

Shell hook compresses ALL terminal output automatically.
MCP adds caching, modes, and analysis on top.

## ctx_read Mode Guide

| Mode         | When                                     | Tokens                 |
|--------------|------------------------------------------|------------------------|
| `map`        | Component overview, deps + API surface   | ~5-15%                 |
| `signatures` | Headers, timing profiles, constants only | ~10-20%                |
| `full`       | Files you will edit (core, fb, app_main) | 100% first, ~0% cached |
| `diff`       | Re-reading after edits                   | only changed lines     |
| `aggressive` | Large build logs, idf.py output          | ~30-50%                |
| `entropy`    | Repetitive DMA/ISR debug logs            | ~20-40%                |
| `task`       | Task-relevant lines from large files     | variable               |
| `lines:N-M`  | Specific function or section             | exact range            |

## Component Mode Strategy

| Component      | Path                     | Mode         | Reason                          |
|----------------|--------------------------|--------------|---------------------------------|
| crt_core       | `components/crt_core/`   | `full`       | Hot path, edited frequently     |
| crt_hal        | `components/crt_hal/`    | `map`        | Hardware layer, rarely edited   |
| crt_timing     | `components/crt_timing/` | `signatures` | Constants and lookup tables     |
| crt_demo       | `components/crt_demo/`   | `map`        | Color palettes, patterns        |
| crt_diag       | `components/crt_diag/`   | `signatures` | Simple lock-free API            |
| crt_fb         | `components/crt_fb/`     | `full`       | Framebuffer, actively developed |
| app_main       | `main/app_main.c`        | `full`       | Entry point, always relevant    |
| crt_monitor    | `tools/crt_monitor/`     | `map`        | Web dashboard, separate tool    |
| godzilla_img.h | `main/godzilla_img.h`    | `reference`  | ~60KB pixel data, NEVER full    |
| test files     | `tests/`                 | `signatures` | Just assert patterns            |
| stubs          | `tests/stubs/`           | `signatures` | ESP-IDF type mocks              |

## Project Structure

```
esp32-crt-signal-core/
├── CLAUDE.md              # Agent quick-start
├── AGENTS.md              # Full architecture knowledge base
├── LEAN-CTX.md            # This file — context engineering config
├── main/
│   ├── app_main.c         # Entry point, hook registration, UART upload
│   ├── godzilla_img.h     # Generated pixel data (~60KB) — never read full
│   └── CMakeLists.txt
├── components/
│   ├── crt_core/          # Engine: lifecycle, stage pipeline, hooks
│   │   ├── crt_core.c
│   │   ├── crt_waveform.c
│   │   ├── crt_line_policy.c
│   │   └── include/       # crt_scanline.h (ABI), crt_stage.h
│   ├── crt_hal/           # Hardware: I2S0, APLL, DAC, DMA, ISR
│   ├── crt_timing/        # NTSC/PAL profiles + line classification
│   ├── crt_demo/          # Demo patterns: color bars + ramp
│   ├── crt_diag/          # Telemetry: underruns, queue depth
│   └── crt_fb/            # Indexed-8 framebuffer + palette LUT
├── tests/                 # Host-compiled C tests (gcc, not ESP32)
│   ├── stubs/             # ESP-IDF type stubs for host tests
│   └── *_test.c
├── tools/
│   ├── crt_monitor/       # Web dashboard (V4L2 + Mongoose HTTP)
│   │   ├── main.c, capture.c, routes.c
│   │   └── static/        # PWA frontend (HTML/JS/CSS)
│   └── img2fb.py          # Image → C header converter
└── docs/superpowers/      # Design specs and plans
```

## Frequent Search Patterns (ctx_search)

| Domain           | Pattern                                                                |
|------------------|------------------------------------------------------------------------|
| Hook system      | `crt_register_.*hook\|crt_scanline_hook\|crt_frame_hook\|crt_mod_hook` |
| Stage pipeline   | `blanking_stage\|sync_stage\|burst_stage\|active_stage`                |
| DMA/ISR          | `lldesc_t\|refill_queue\|crt_hal_isr\|MALLOC_CAP_DMA`                  |
| Timing constants | `sample_rate_hz\|samples_per_line\|active_width\|total_lines`          |
| Framebuffer      | `crt_fb_surface\|crt_fb_put\|crt_fb_clear\|palette`                    |
| Fast mono        | `s_fast_active_line\|s_fast_blank_line\|s_fast_vsync_line`             |
| Signal levels    | `SYNC_LEVEL\|BLANK_LEVEL\|LUMA_BLACK\|LUMA_WHITE`                      |
| Build errors     | `error:\|undefined reference\|multiple definition`                     |

## ctx_tree — Recommended Depths

| Path                 | Depth | Why                         |
|----------------------|-------|-----------------------------|
| `.` (project root)   | 2     | Components + tools overview |
| `components/<name>/` | 1     | Source + include listing    |
| `tests/`             | 1     | All test files              |
| `tools/crt_monitor/` | 2     | Server + static frontend    |
| `docs/`              | 2     | Specs and plans             |

## ctx_overview — Task Keywords

| Keyword                             | Focus                   |
|-------------------------------------|-------------------------|
| `"composite video signal pipeline"` | Full engine overview    |
| `"framebuffer rendering"`           | crt_fb component        |
| `"DMA I2S hardware"`                | crt_hal component       |
| `"NTSC PAL timing"`                 | crt_timing profiles     |
| `"scanline hook system"`            | Hook ABI + registration |
| `"web dashboard monitor"`           | crt_monitor tool        |
| `"host test compilation"`           | Test infrastructure     |

## Session Management

- `ctx_overview("ESP32 CRT signal task")` at session start
- `ctx_compress` after >10 turns or large build output
- `ctx_session task "feature/fix description"` for tracking
- `ctx_session finding "component — issue"` for bugs found
- `ctx_session decision "design choice rationale"` for arch decisions
- `ctx_metrics` to check token savings

## Key Files by Task

| Task                | Read These                                | Mode            |
|---------------------|-------------------------------------------|-----------------|
| New feature         | CLAUDE.md, AGENTS.md, relevant component  | map, map, full  |
| Bug in core         | crt_core.c, crt_scanline.h                | full            |
| Timing issue        | crt_timing.c, crt_hal.c                   | full, map       |
| Framebuffer work    | crt_fb.c, crt_fb.h, app_main.c            | full            |
| Hook integration    | crt_scanline.h, crt_core.c                | full            |
| Monitor dashboard   | tools/crt_monitor/*.c, static/            | full            |
| Image conversion    | tools/img2fb.py                           | full            |
| Running tests       | tests/*_test.c, CLAUDE.md (test commands) | signatures, map |
| Build debugging     | build log output                          | aggressive      |
| Architecture review | AGENTS.md                                 | map or task     |

## ESP32-Specific Compression Tips

- `idf.py build` output → shell hook auto-compresses; use `aggressive` if reading logs
- Test gcc output → short, shell hook handles it
- Serial monitor logs → `entropy` mode collapses repetitive ISR/DMA messages
- `menuconfig` dumps → `signatures` for current config values
- Generated headers (img2fb output) → `reference` mode only, never `full`
- AGENTS.md (8KB+) → `map` for overview, `task` with description for specific section
