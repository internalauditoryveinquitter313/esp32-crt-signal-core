/**
 * @file crt_scanline_abi_test.c
 * @brief Validates crt_scanline_t layout, constants, and type correctness.
 *
 * Host-compiled: gcc -I components/crt_core/include -I components/crt_timing/include \
 *                    -I tests/stubs tests/crt_scanline_abi_test.c -o /tmp/scanline_abi_test
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "crt_scanline.h"

/* ── Enum values ───────────────────────────────────────────────────── */

static void test_line_type_enum(void)
{
    assert(CRT_LINE_ACTIVE   == 0);
    assert(CRT_LINE_BLANK    == 1);
    assert(CRT_LINE_VSYNC    == 2);
    assert(CRT_LINE_EQ_PULSE == 3);
    printf("  line_type enum: OK\n");
}

/* ── Sentinel and masks ────────────────────────────────────────────── */

static void test_constants(void)
{
    assert(CRT_SCANLINE_LOGICAL_LINE_NONE == 0xFFFF);
    assert(CRT_SCANLINE_SUBCARRIER_PHASE_MASK == 0x000FFFFF);
    assert(CRT_PHASE_Q20_FULL_CYCLE == 0x100000);
    printf("  constants: OK\n");
}

/* ── Phase arithmetic ──────────────────────────────────────────────── */

static void test_phase_advance(void)
{
    uint32_t phase = 0;
    uint32_t step = 0x40000; /* 1/4 cycle */

    phase = CRT_PHASE_Q20_ADVANCE(phase, step);
    assert(phase == 0x40000);

    phase = CRT_PHASE_Q20_ADVANCE(phase, step);
    assert(phase == 0x80000);

    phase = CRT_PHASE_Q20_ADVANCE(phase, step);
    assert(phase == 0xC0000);

    /* Wrap around */
    phase = CRT_PHASE_Q20_ADVANCE(phase, step);
    assert(phase == 0x00000); /* 4/4 = full cycle wraps to 0 */

    printf("  phase advance + wrap: OK\n");
}

/* ── Scanline descriptor construction ──────────────────────────────── */

static void test_scanline_struct(void)
{
    crt_timing_profile_t ntsc_timing;
    memset(&ntsc_timing, 0, sizeof(ntsc_timing));
    ntsc_timing.total_lines = 262;
    ntsc_timing.active_lines = 240;

    /* Active line */
    crt_scanline_t active = {
        .physical_line = 100,
        .logical_line = 100,
        .type = CRT_LINE_ACTIVE,
        .field = 0,
        .frame_number = 42,
        .subcarrier_phase = 0x55555,
        .timing = &ntsc_timing,
    };

    assert(active.physical_line == 100);
    assert(active.logical_line == 100);
    assert(active.type == CRT_LINE_ACTIVE);
    assert(active.field == 0);
    assert(active.frame_number == 42);
    assert(active.subcarrier_phase == 0x55555);
    assert(active.timing == &ntsc_timing);
    assert(CRT_SCANLINE_IS_ACTIVE(&active));
    assert(CRT_SCANLINE_HAS_LOGICAL(&active));

    /* Blank line */
    crt_scanline_t blank = {
        .physical_line = 245,
        .logical_line = CRT_SCANLINE_LOGICAL_LINE_NONE,
        .type = CRT_LINE_BLANK,
        .field = 0,
        .frame_number = 42,
        .subcarrier_phase = 0,
        .timing = &ntsc_timing,
    };

    assert(!CRT_SCANLINE_IS_ACTIVE(&blank));
    assert(!CRT_SCANLINE_HAS_LOGICAL(&blank));
    assert(blank.logical_line == 0xFFFF);

    printf("  scanline struct + macros: OK\n");
}

/* ── Hook type signature check (compile-time) ──────────────────────── */

static void dummy_frame_hook(uint32_t frame_number, void *user_data)
{
    (void)frame_number;
    (void)user_data;
}

static void dummy_scanline_hook(const crt_scanline_t *scanline,
                                uint16_t *active_buf,
                                uint16_t active_width,
                                void *user_data)
{
    (void)scanline;
    (void)active_buf;
    (void)active_width;
    (void)user_data;
}

static void dummy_mod_hook(const crt_scanline_t *scanline,
                           uint16_t *line_buf,
                           uint16_t line_width,
                           void *user_data)
{
    (void)scanline;
    (void)line_buf;
    (void)line_width;
    (void)user_data;
}

static void test_hook_signatures(void)
{
    crt_frame_hook_fn fh = dummy_frame_hook;
    crt_scanline_hook_fn sh = dummy_scanline_hook;
    crt_mod_hook_fn mh = dummy_mod_hook;

    assert(fh != NULL);
    assert(sh != NULL);
    assert(mh != NULL);

    printf("  hook signatures: OK\n");
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void)
{
    printf("crt_scanline ABI test\n");
    test_line_type_enum();
    test_constants();
    test_phase_advance();
    test_scanline_struct();
    test_hook_signatures();
    printf("ALL PASSED\n");
    return 0;
}
