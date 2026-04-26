# ESP32 CRT Signal Core — convenience targets
# Requires ESP-IDF sourced in shell: . $IDF_PATH/export.sh

PORT ?= /dev/ttyUSB0
BAUD ?= 115200

# ── Build ────────────────────────────────────────────────────────────

.PHONY: build flash monitor clean menuconfig fullclean

build:
	idf.py build

flash:
	idf.py -p $(PORT) flash

monitor:
	idf.py -p $(PORT) -b $(BAUD) monitor

fm: flash monitor  ## Flash + monitor (most common)
	@true

clean:
	idf.py fullclean

menuconfig:
	idf.py menuconfig

# ── Host Tests ───────────────────────────────────────────────────────

TEST_CC     ?= gcc
TEST_CFLAGS ?= -Wall -Wextra -Wno-unused-function -std=c11 -g
TEST_INC    := -I tests/stubs \
               -I components/crt_core/include \
               -I components/crt_timing/include \
               -I components/crt_demo/include \
               -I components/crt_fb/include \
               -I components/crt_compose/include \
               -I components/crt_stimulus/include \
               -I components/crt_tile/include \
               -I components/crt_hal/include
TEST_OUT    := /tmp
LINT_SOURCES := components/crt_core/crt_waveform.c \
                components/crt_core/crt_line_policy.c \
                components/crt_core/crt_composite_palette.c \
                components/crt_hal/crt_hal_clock.c \
                components/crt_timing/crt_timing.c \
                components/crt_demo/crt_demo_pattern.c \
                components/crt_fb/crt_fb.c \
                components/crt_compose/crt_compose.c \
                components/crt_compose/crt_compose_layers.c \
                components/crt_compose/crt_sprite.c \
                components/crt_tile/crt_tile.c \
                components/crt_stimulus/crt_stimulus.c

.PHONY: test test-core test-render test-burst test-policy test-timing test-demo test-hal-clock \
        test-composite-palette test-scanline-abi test-scanline-header test-fb test-compose \
        test-stimulus test-tile

test: test-core test-render  ## Run all host tests
	@echo "\n✓ All tests passed"

test-core: test-burst test-policy test-timing test-demo test-hal-clock test-composite-palette test-scanline-abi test-scanline-header  ## Run core host tests
	@echo "\n✓ Core tests passed"

test-render: test-fb test-compose test-stimulus test-tile  ## Run render adapter host tests
	@echo "\n✓ Render tests passed"

test-burst:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/burst_waveform_test.c components/crt_core/crt_waveform.c \
		-lm -o $(TEST_OUT)/burst_test && $(TEST_OUT)/burst_test

test-policy:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/line_policy_test.c components/crt_core/crt_line_policy.c \
		-o $(TEST_OUT)/policy_test && $(TEST_OUT)/policy_test

test-timing:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_timing_profile_test.c components/crt_timing/crt_timing.c \
		-o $(TEST_OUT)/timing_test && $(TEST_OUT)/timing_test

test-demo:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_demo_pattern_test.c components/crt_demo/crt_demo_pattern.c \
		components/crt_core/crt_waveform.c -lm \
		-o $(TEST_OUT)/demo_test && $(TEST_OUT)/demo_test

test-hal-clock:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_hal_clock_test.c components/crt_hal/crt_hal_clock.c \
		-o $(TEST_OUT)/crt_hal_clock_test && $(TEST_OUT)/crt_hal_clock_test

test-composite-palette:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_composite_palette_test.c components/crt_core/crt_composite_palette.c \
		-o $(TEST_OUT)/crt_composite_palette_test && $(TEST_OUT)/crt_composite_palette_test

test-scanline-abi:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_scanline_abi_test.c \
		-o $(TEST_OUT)/scanline_abi_test && $(TEST_OUT)/scanline_abi_test

test-scanline-header:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_scanline_header_test.c \
		-o $(TEST_OUT)/scanline_header_test && $(TEST_OUT)/scanline_header_test

test-fb:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_fb_test.c components/crt_fb/crt_fb.c components/crt_core/crt_composite_palette.c \
		-o $(TEST_OUT)/crt_fb_test && $(TEST_OUT)/crt_fb_test

test-compose:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_compose_test.c components/crt_compose/crt_compose.c \
		components/crt_compose/crt_compose_layers.c components/crt_compose/crt_sprite.c \
		-o $(TEST_OUT)/crt_compose_test && $(TEST_OUT)/crt_compose_test

test-stimulus:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_stimulus_test.c components/crt_stimulus/crt_stimulus.c \
		-o $(TEST_OUT)/crt_stimulus_test && $(TEST_OUT)/crt_stimulus_test

test-tile:
	@$(TEST_CC) $(TEST_CFLAGS) $(TEST_INC) \
		tests/crt_tile_test.c components/crt_tile/crt_tile.c \
		-o $(TEST_OUT)/crt_tile_test && $(TEST_OUT)/crt_tile_test

# ── Lint ─────────────────────────────────────────────────────────────

.PHONY: format lint

format:  ## Run clang-format on all project sources
	@find components main -name '*.c' -o -name '*.h' | xargs clang-format -i
	@echo "✓ Formatted"

lint:  ## Run clang-tidy on host-portable project sources
	@clang-tidy $(LINT_SOURCES) --quiet -- $(TEST_CFLAGS) $(TEST_INC)
	@echo "✓ Lint done"

# ── Help ─────────────────────────────────────────────────────────────

.PHONY: help
help:  ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?##' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}'
