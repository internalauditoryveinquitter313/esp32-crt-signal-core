# Contributing

Thanks for helping improve ESP32 CRT Signal Core. This project is open source and welcomes focused contributions that preserve the core contract: deterministic composite video generation on ESP32.

## Before You Start

- Read `AGENTS.md` for the repository map, architecture invariants, and verification matrix.
- Check existing issues and pull requests before starting larger work.
- For design changes, open an issue first so timing, hardware, and API trade-offs can be discussed before implementation.
- Keep changes focused. Small pull requests are easier to review and safer for realtime firmware.

## Development Setup

Required tools:

- ESP-IDF 5.4.x
- Xtensa ESP32 toolchain from ESP-IDF
- Host `gcc` for unit tests
- `clang-format` for C formatting

Build firmware:

```bash
bash -c '. ~/esp/esp-idf/export.sh && idf.py build'
```

Run the host test matrix:

```bash
make test
```

Run focused test groups:

```bash
make test-core
make test-render
```

Flash and monitor hardware:

```bash
bash -c '. ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash monitor'
```

## Coding Guidelines

- Use C11 and the existing ESP-IDF component layout.
- Follow `.clang-format` for C and header files.
- Preserve deterministic signal behavior over image richness.
- Avoid allocations after `crt_core_start()`.
- Do not add blocking work, logging, or peripheral access to hot-path stages, hooks, or ISR paths.
- Keep ISR work minimal and IRAM-safe.
- Treat framebuffer and tile rendering as adapters, not as the architectural center.
- Keep public APIs small and document timing-sensitive contracts.

## Tests and Verification

Run the relevant checks for the files you touched:

- `components/crt_timing/`: timing profile tests
- `components/crt_core/crt_waveform.c`: burst waveform and demo pattern tests
- `components/crt_core/crt_line_policy.c`: line policy tests
- `components/crt_core/include/crt_scanline.h` or hook ABI: scanline ABI/header tests
- `components/crt_fb/`: framebuffer tests
- `components/crt_compose/`: compositor tests
- `components/crt_tile/`: tile renderer tests
- `main/`, `components/crt_hal/`, `components/crt_core/crt_core.c`, Kconfig, or build wiring: ESP-IDF build
- `tools/crt_monitor/`: build the monitor tool from `tools/crt_monitor`

For documentation-only changes, verify that commands, component names, and counts match the current tree.

## Pull Requests

Before opening a PR:

- Explain the problem and the solution.
- Link related issues or design notes when available.
- List the exact verification commands you ran.
- Include hardware notes for changes that affect signal output, timing, GPIO, I2S, DAC, DMA, PAL/NTSC behavior, or monitor tooling.
- Call out anything intentionally left unverified.

Recent commit history uses concise subjects such as:

```text
📝 docs: update contributor guidance
🔨 build(make): reorganize host test targets
⚡ refactor(hal): tighten DMA descriptor lookup
```

Use a similarly clear, imperative summary when possible.

## Security Issues

Do not report vulnerabilities in public issues. Follow `SECURITY.md`.

## License

By contributing, you agree that your contributions are licensed under the MIT License unless explicitly stated otherwise.
