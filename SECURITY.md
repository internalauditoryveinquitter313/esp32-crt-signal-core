# Security Policy

## Supported Versions

This project currently supports the active `main` branch.

| Version | Supported |
|:--------|:---------:|
| `main`  | Yes       |

## Scope

ESP32 CRT Signal Core is embedded firmware for analog composite video generation. The most relevant security-sensitive areas are:

- Buffer bounds in framebuffer, compositor, tile, and scanline paths
- DMA descriptor ownership and internal SRAM assumptions
- ISR safety and watchdog-sensitive code paths
- UART framebuffer upload when `CRT_ENABLE_UART_UPLOAD` is enabled
- Tooling that opens local network, camera, or capture interfaces

Analog signal quality bugs, PAL/NTSC timing issues, visual artifacts, and ordinary build failures should normally be reported as public issues unless they expose memory corruption, unsafe hardware access, or a denial-of-service path.

## Reporting a Vulnerability

Please do not open a public issue for suspected vulnerabilities.

Report privately by emailing:

```text
gabrielmaialva33@gmail.com
```

Include:

- Affected component or tool
- Impact and threat model
- Reproduction steps or proof of concept
- ESP-IDF version, target chip, and relevant Kconfig options
- Whether physical access, serial access, or network access is required

You should receive an initial response within 72 hours. If the issue is accepted, maintainers will coordinate a fix and public disclosure timeline before publishing details.

## Safe Harbor

Good-faith security research is welcome when it avoids privacy violations, destructive hardware behavior, data loss, and public disclosure before maintainers have had a reasonable chance to respond.
