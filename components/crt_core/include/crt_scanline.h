#ifndef CRT_SCANLINE_H
#define CRT_SCANLINE_H

#include "crt_timing_types.h"

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sentinel for scanlines that do not map to a visible logical line.
 *
 * The logical line field is stored as uint16_t for compactness in the hot path.
 * Non-visible lines use the wrapped representation of -1.
 */
#define CRT_SCANLINE_LOGICAL_LINE_NONE ((uint16_t)UINT16_MAX)

/**
 * @brief Mask for Q20 subcarrier phase accumulation.
 *
 * Phase updates are expected to follow:
 * `phase = (phase + phase_step) & CRT_SCANLINE_SUBCARRIER_PHASE_MASK`.
 */
#define CRT_SCANLINE_SUBCARRIER_PHASE_MASK ((uint32_t)0x000FFFFFU)

/**
 * @brief Temporal class of a physical scanline.
 *
 * This classifies the physical line independently from renderer-facing content.
 */
typedef enum {
    CRT_LINE_ACTIVE = 0,
    CRT_LINE_BLANK = 1,
    CRT_LINE_VSYNC = 2,
    CRT_LINE_EQ_PULSE = 3,
} crt_line_type_t;

/**
 * @brief Immutable descriptor for one physical scanline.
 *
 * The core owns this metadata and presents it to hooks as read-only state.
 * Hooks must not allocate, block, log, or use floating point in the hot path.
 */
typedef struct {
    /** Physical line index within the active video standard. */
    uint16_t physical_line;

    /**
     * Logical visible line index.
     *
     * Visible lines use the range `0..active_lines - 1`. Non-visible lines use
     * `CRT_SCANLINE_LOGICAL_LINE_NONE`.
     */
    uint16_t logical_line;

    /** Temporal class for this physical line. */
    crt_line_type_t type;

    /**
     * Interlace field index.
     *
     * Currently `0` for progressive-style scheduling, reserved for future
     * interlaced field handling.
     */
    uint8_t field;

    /** Monotonic frame counter associated with this scanline. */
    uint32_t frame_number;

    /**
     * Accumulated subcarrier phase in Q20 fixed-point.
     *
     * The hot path should wrap with `CRT_SCANLINE_SUBCARRIER_PHASE_MASK`.
     */
    uint32_t subcarrier_phase;

    /** Pointer to the active timing profile for this line. */
    const crt_timing_profile_t *timing;
} crt_scanline_t;

/**
 * @brief Frame hook called once before the first line of each frame.
 */
typedef void (*crt_frame_hook_fn)(uint32_t frame_number, void *user_data);

/**
 * @brief Scanline render hook called once for each active line.
 *
 * The core passes only the active region buffer, isolating sync, porch, and
 * burst handling from content rendering.
 */
typedef void (*crt_scanline_hook_fn)(const crt_scanline_t *scanline, uint16_t *active_buf,
                                     uint16_t active_width, void *user_data);

/**
 * @brief Post-render modulation hook for full-line perturbations.
 *
 * This hook sees the full line buffer after core composition and active region
 * rendering, enabling deterministic effects such as phase sweep or wobble.
 */
typedef void (*crt_mod_hook_fn)(const crt_scanline_t *scanline, uint16_t *line_buf,
                                uint16_t line_width, void *user_data);

/**
 * @brief Register a frame hook.
 *
 * The hook pointer may be NULL to clear the current registration.
 */
esp_err_t crt_register_frame_hook(crt_frame_hook_fn hook, void *user_data);

/**
 * @brief Register an active scanline render hook.
 *
 * The hook pointer may be NULL to clear the current registration.
 */
esp_err_t crt_register_scanline_hook(crt_scanline_hook_fn hook, void *user_data);

/**
 * @brief Register a post-render modulation hook.
 *
 * The hook pointer may be NULL to clear the current registration.
 */
esp_err_t crt_register_mod_hook(crt_mod_hook_fn hook, void *user_data);

/* ── Phase utilities ───────────────────────────────────────────────── */

/** Q20 fixed-point: one full subcarrier cycle */
#define CRT_PHASE_Q20_FULL_CYCLE ((uint32_t)0x100000U)

/** Advance phase by step with Q20 wrap */
#define CRT_PHASE_Q20_ADVANCE(phase, step) (((phase) + (step)) & CRT_SCANLINE_SUBCARRIER_PHASE_MASK)

/** Check if scanline is an active (visible) line */
#define CRT_SCANLINE_IS_ACTIVE(s) ((s)->type == CRT_LINE_ACTIVE)

/** Check if scanline has a valid logical line index */
#define CRT_SCANLINE_HAS_LOGICAL(s) ((s)->logical_line != CRT_SCANLINE_LOGICAL_LINE_NONE)

#ifdef __cplusplus
}
#endif

#endif /* CRT_SCANLINE_H */
