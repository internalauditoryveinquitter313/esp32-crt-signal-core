# Pull Request

## Summary

<!-- What changed and why? Keep this focused. -->

## Type

- [ ] Bug fix
- [ ] Feature
- [ ] Refactor
- [ ] Build/tooling
- [ ] Documentation
- [ ] Tests

## Affected Areas

- [ ] `crt_core`
- [ ] `crt_hal`
- [ ] `crt_timing`
- [ ] `crt_demo`
- [ ] `crt_diag`
- [ ] `crt_fb`
- [ ] `crt_compose`
- [ ] `crt_tile`
- [ ] `main`
- [ ] `tools`
- [ ] Documentation only

## Realtime / Hardware Impact

- [ ] No realtime or hardware impact
- [ ] Affects scanline timing
- [ ] Affects ISR or DMA behavior
- [ ] Affects I2S0, DAC, GPIO25, or APLL configuration
- [ ] Affects NTSC/PAL timing or colorburst behavior
- [ ] Requires hardware verification

Notes:

<!-- Include captures, serial logs, monitor output, or hardware setup notes when relevant. -->

## Verification

Commands run:

```text

```

Results:

<!-- Mention anything intentionally not run and why. -->

## Checklist

- [ ] I kept the change focused and avoided unrelated refactors.
- [ ] I preserved deterministic sync/blanking behavior.
- [ ] I avoided allocations after `crt_core_start()` in runtime paths.
- [ ] I avoided blocking work, logging, and peripheral access in hot paths and hooks.
- [ ] I kept ISR work minimal.
- [ ] I updated docs when behavior, commands, or workflow changed.
- [ ] I followed the security policy for any vulnerability-related change.
