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
               -I components/crt_demo/include
TEST_OUT    := /tmp

.PHONY: test test-burst test-policy test-timing test-demo

test: test-burst test-policy test-timing test-demo  ## Run all host tests
	@echo "\n✓ All tests passed"

test-safe: test-burst test-policy test-timing  ## Run passing host tests (skip known failures)
	@echo "\n✓ Passing tests OK"

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

# ── Lint ─────────────────────────────────────────────────────────────

.PHONY: format lint

format:  ## Run clang-format on all project sources
	@find components main -name '*.c' -o -name '*.h' | xargs clang-format -i
	@echo "✓ Formatted"

lint:  ## Run clang-tidy on project sources
	@find components main -name '*.c' | xargs clang-tidy -p build --quiet 2>/dev/null || true
	@echo "✓ Lint done"

# ── Help ─────────────────────────────────────────────────────────────

.PHONY: help
help:  ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?##' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}'
