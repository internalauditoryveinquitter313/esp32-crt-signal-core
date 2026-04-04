#include <assert.h>
#include <stddef.h>

#include "crt_scanline.h"

static void test_scanline_contract_layout(void)
{
    crt_scanline_t scanline = {
        .physical_line = 12U,
        .logical_line = CRT_SCANLINE_LOGICAL_LINE_NONE,
        .type = CRT_LINE_BLANK,
        .field = 0U,
        .frame_number = 42U,
        .subcarrier_phase = 0x000ABCDEU & CRT_SCANLINE_SUBCARRIER_PHASE_MASK,
        .timing = NULL,
    };

    assert(scanline.physical_line == 12U);
    assert(scanline.logical_line == CRT_SCANLINE_LOGICAL_LINE_NONE);
    assert(scanline.type == CRT_LINE_BLANK);
    assert(scanline.field == 0U);
    assert(scanline.frame_number == 42U);
    assert(scanline.subcarrier_phase == (0x000ABCDEU & CRT_SCANLINE_SUBCARRIER_PHASE_MASK));
    assert(scanline.timing == NULL);
}

static void frame_hook(uint32_t frame_number, void *user_data)
{
    (void)frame_number;
    (void)user_data;
}

static void scanline_hook(const crt_scanline_t *scanline,
                          uint16_t *active_buf,
                          uint16_t active_width,
                          void *user_data)
{
    (void)scanline;
    (void)active_buf;
    (void)active_width;
    (void)user_data;
}

static void mod_hook(const crt_scanline_t *scanline,
                     uint16_t *line_buf,
                     uint16_t line_width,
                     void *user_data)
{
    (void)scanline;
    (void)line_buf;
    (void)line_width;
    (void)user_data;
}

static void test_hook_signatures_compile(void)
{
    crt_frame_hook_fn frame = frame_hook;
    crt_scanline_hook_fn render = scanline_hook;
    crt_mod_hook_fn mod = mod_hook;

    assert(frame != NULL);
    assert(render != NULL);
    assert(mod != NULL);
}

int main(void)
{
    test_scanline_contract_layout();
    test_hook_signatures_compile();
    return 0;
}
