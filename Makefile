CC = gcc

# Version generation
# If VERSION file doesn't exist, create it from git commit hash
# CI/CD will overwrite this with tag version
ifeq ($(wildcard VERSION),)
$(shell git rev-parse --short HEAD 2>/dev/null > VERSION || echo "unknown" > VERSION)
endif
VERSION := $(shell cat VERSION)

# Use generic architecture for CI compatibility
ifdef CI
CFLAGS = -Wall -Wextra -O2 -Isrc -Isrc/include -DHYPRLAX_VERSION=\"$(VERSION)\"
LDFLAGS =
else
CFLAGS = -Wall -Wextra -O3 -march=native -flto -Isrc -Isrc/include -DHYPRLAX_VERSION=\"$(VERSION)\"
LDFLAGS = -flto
endif

# Cross-compilation support
ifdef ARCH
ifeq ($(ARCH),aarch64)
CC = aarch64-linux-gnu-gcc
CFLAGS = -Wall -Wextra -O3 -flto -Isrc
endif
endif

# Add build defines to CFLAGS
CFLAGS += $(BUILD_DEFINES)

# Feature flags (all enabled by default)
ENABLE_WAYLAND ?= 1
ENABLE_HYPRLAND ?= 1
ENABLE_SWAY ?= 1
ENABLE_WAYFIRE ?= 1
ENABLE_NIRI ?= 1
ENABLE_RIVER ?= 1
ENABLE_GENERIC_WAYLAND ?= 1
ENABLE_GLES2 ?= 1

# Build defines based on feature flags
BUILD_DEFINES =
ifeq ($(ENABLE_WAYLAND),1)
BUILD_DEFINES += -DENABLE_WAYLAND
endif
ifeq ($(ENABLE_HYPRLAND),1)
BUILD_DEFINES += -DENABLE_HYPRLAND
endif
ifeq ($(ENABLE_SWAY),1)
BUILD_DEFINES += -DENABLE_SWAY
endif
ifeq ($(ENABLE_WAYFIRE),1)
BUILD_DEFINES += -DENABLE_WAYFIRE
endif
ifeq ($(ENABLE_NIRI),1)
BUILD_DEFINES += -DENABLE_NIRI
endif
ifeq ($(ENABLE_RIVER),1)
BUILD_DEFINES += -DENABLE_RIVER
endif
ifeq ($(ENABLE_GENERIC_WAYLAND),1)
BUILD_DEFINES += -DENABLE_GENERIC_WAYLAND
endif
ifeq ($(ENABLE_GLES2),1)
BUILD_DEFINES += -DENABLE_GLES2
endif

# Package dependencies (conditional)
PKG_DEPS =
ifeq ($(ENABLE_WAYLAND),1)
PKG_DEPS += wayland-client wayland-protocols wayland-egl
endif
ifeq ($(ENABLE_GLES2),1)
PKG_DEPS += egl glesv2
endif

# Get package flags
PKG_CFLAGS = $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LIBS = $(shell pkg-config --libs $(PKG_DEPS))

# Wayland protocols
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

# Protocol files (conditional on Wayland)
ifeq ($(ENABLE_WAYLAND),1)
XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_PROTOCOL = protocols/wlr-layer-shell-unstable-v1.xml
RIVER_STATUS_PROTOCOL = protocols/river-status-unstable-v1.xml
PROTOCOL_SRCS = protocols/xdg-shell-protocol.c protocols/wlr-layer-shell-protocol.c
PROTOCOL_HDRS = protocols/xdg-shell-client-protocol.h protocols/wlr-layer-shell-client-protocol.h
# River status protocol is optional, only include if River is enabled
ifeq ($(ENABLE_RIVER),1)
PROTOCOL_SRCS += protocols/river-status-protocol.c
PROTOCOL_HDRS += protocols/river-status-client-protocol.h
endif
else
PROTOCOL_SRCS =
PROTOCOL_HDRS =
endif

# Core module sources (always included)
CORE_SRCS = src/core/easing.c src/core/animation.c src/core/layer.c src/core/config.c src/core/monitor.c src/core/log.c src/core/cursor.c src/core/render_core.c src/core/event_loop.c \
            src/core/input/input_manager.c src/core/input/providers.c src/core/input/modes/workspace.c src/core/input/modes/cursor.c src/core/input/modes/window.c

# Renderer module sources (conditional)
RENDERER_SRCS = src/renderer/renderer.c src/renderer/shader.c
ifeq ($(ENABLE_GLES2),1)
RENDERER_SRCS += src/renderer/gles2.c
endif

# Platform module sources (conditional)
PLATFORM_SRCS = src/platform/platform.c
ifeq ($(ENABLE_WAYLAND),1)
PLATFORM_SRCS += src/platform/wayland.c
endif

# Compositor module sources (conditional)
COMPOSITOR_SRCS = src/compositor/compositor.c src/compositor/workspace_models.c
ifeq ($(ENABLE_HYPRLAND),1)
COMPOSITOR_SRCS += src/compositor/hyprland.c
endif
ifeq ($(ENABLE_SWAY),1)
COMPOSITOR_SRCS += src/compositor/sway.c
endif
ifeq ($(ENABLE_WAYFIRE),1)
COMPOSITOR_SRCS += src/compositor/wayfire.c
endif
ifeq ($(ENABLE_NIRI),1)
COMPOSITOR_SRCS += src/compositor/niri.c
endif
ifeq ($(ENABLE_RIVER),1)
COMPOSITOR_SRCS += src/compositor/river.c
endif
ifeq ($(ENABLE_GENERIC_WAYLAND),1)
COMPOSITOR_SRCS += src/compositor/generic_wayland.c
endif

# Main module sources
MAIN_SRCS = src/main.c src/hyprlax_main.c src/hyprlax_ctl.c

# Source files (modular only; legacy path removed)
SRCS = $(MAIN_SRCS) src/ipc.c $(CORE_SRCS) $(RENDERER_SRCS) $(PLATFORM_SRCS) \
       $(COMPOSITOR_SRCS) $(PROTOCOL_SRCS)

# Vendor libraries and optional modules
SRCS += src/vendor/toml.c src/core/config_toml.c src/core/config_legacy.c
OBJS = $(SRCS:.c=.o)
TARGET = hyprlax

all: $(TARGET)

# Convenience targets for common configurations
hyprland-minimal:
	$(MAKE) clean
	$(MAKE) ENABLE_SWAY=0 ENABLE_WAYFIRE=0 ENABLE_NIRI=0 ENABLE_RIVER=0 ENABLE_GENERIC_WAYLAND=0

wayland-only:
	$(MAKE) clean
	$(MAKE)

sway-minimal:
	$(MAKE) clean
	$(MAKE) ENABLE_HYPRLAND=0 ENABLE_WAYFIRE=0 ENABLE_NIRI=0 ENABLE_RIVER=0 ENABLE_GENERIC_WAYLAND=0

# Generate protocol files
protocols/xdg-shell-protocol.c: $(XDG_SHELL_PROTOCOL)
	@mkdir -p protocols
	$(WAYLAND_SCANNER) private-code < $< > $@

protocols/xdg-shell-client-protocol.h: $(XDG_SHELL_PROTOCOL)
	@mkdir -p protocols
	$(WAYLAND_SCANNER) client-header < $< > $@

protocols/wlr-layer-shell-protocol.c: $(LAYER_SHELL_PROTOCOL)
	@mkdir -p protocols
	$(WAYLAND_SCANNER) private-code < $< > $@

protocols/wlr-layer-shell-client-protocol.h: $(LAYER_SHELL_PROTOCOL)
	@mkdir -p protocols
	$(WAYLAND_SCANNER) client-header < $< > $@

protocols/river-status-protocol.c: $(RIVER_STATUS_PROTOCOL)
	@mkdir -p protocols
	$(WAYLAND_SCANNER) private-code < $< > $@

protocols/river-status-client-protocol.h: $(RIVER_STATUS_PROTOCOL)
	@mkdir -p protocols
	$(WAYLAND_SCANNER) client-header < $< > $@

# Compile
%.o: %.c $(PROTOCOL_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

# Ensure VERSION file exists before building
VERSION:
	@if [ ! -f VERSION ]; then \
		git rev-parse --short HEAD 2>/dev/null > VERSION || echo "unknown" > VERSION; \
	fi

$(TARGET): VERSION $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(PKG_LIBS) -lm -o $@

clean:
	rm -f $(TARGET) $(OBJS) $(PROTOCOL_SRCS) $(PROTOCOL_HDRS)
	rm -rf protocols/*.o src/*.o

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DESTDIR ?=

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

install-user: $(TARGET)
	install -Dm755 $(TARGET) ~/.local/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall-user:
	rm -f ~/.local/bin/$(TARGET)

# Test suite with Check framework
CHECK_CFLAGS = $(shell pkg-config --cflags check 2>/dev/null)
CHECK_LIBS = $(shell pkg-config --libs check 2>/dev/null)
TEST_CFLAGS = $(CFLAGS) $(CHECK_CFLAGS)
TEST_LIBS = $(CHECK_LIBS) -lm

# Valgrind settings for memory leak detection
VALGRIND = valgrind
VALGRIND_FLAGS = --leak-check=full --show-leak-kinds=definite,indirect --track-origins=yes --error-exitcode=1
# For Arch Linux, enable debuginfod for symbol resolution
export DEBUGINFOD_URLS ?= https://debuginfod.archlinux.org

TEST_TARGETS = tests/test_integration tests/test_ipc tests/test_blur tests/test_config tests/test_animation tests/test_easing tests/test_shader tests/test_platform tests/test_compositor tests/test_modules tests/test_renderer tests/test_workspace_changes tests/test_animation_state tests/test_config_validation tests/test_hyprland_events
ALL_TESTS = $(filter tests/test_%, $(wildcard tests/test_*.c))
ALL_TEST_TARGETS = $(ALL_TESTS:.c=)
SHELL_TESTS = $(wildcard tests/test_*.sh)

# Individual test rules - updated for Check framework
tests/test_integration: tests/test_integration.c src/ipc.c src/core/log.c
	$(CC) $(TEST_CFLAGS) $^ $(TEST_LIBS) -lpthread -o $@

tests/test_ctl: tests/test_ctl.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_renderer: tests/test_renderer.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_ipc: tests/test_ipc.c src/ipc.c src/core/log.c
	$(CC) $(TEST_CFLAGS) $^ $(TEST_LIBS) -o $@

tests/test_blur: tests/test_blur.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_config: tests/test_config.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_animation: tests/test_animation.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_easing: tests/test_easing.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_shader: tests/test_shader.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_platform: tests/test_platform.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_compositor: tests/test_compositor.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_modules: tests/test_modules.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_workspace_changes: tests/test_workspace_changes.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_animation_state: tests/test_animation_state.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_config_validation: tests/test_config_validation.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

# Hyprland event parsing tests (link hyprland adapter and core compositor utils)
tests/test_hyprland_events: tests/test_hyprland_events.c src/compositor/hyprland.c src/compositor/compositor.c src/core/log.c
	$(CC) $(TEST_CFLAGS) -DUNIT_TEST -Isrc -Isrc/include $^ $(TEST_LIBS) -o $@

tests/test_niri_events: tests/test_niri_events.c src/compositor/niri.c src/compositor/compositor.c src/core/log.c
	$(CC) $(TEST_CFLAGS) -DUNIT_TEST -Isrc -Isrc/include $^ $(TEST_LIBS) -o $@

# Ops smoke test: link against compositor adapters to validate get_event_fd presence
tests/test_compositor_ops: tests/test_compositor_ops.c \
    src/compositor/hyprland.c src/compositor/sway.c src/compositor/wayfire.c \
    src/compositor/niri.c src/compositor/river.c src/compositor/generic_wayland.c \
    src/compositor/compositor.c src/compositor/workspace_models.c src/core/log.c \
    src/core/monitor.c \
    protocols/river-status-protocol.c
	$(CC) $(TEST_CFLAGS) -Isrc -Isrc/include $^ $(TEST_LIBS) $(PKG_LIBS) -o $@

# Caps/Model tests
tests/test_compositor_caps: tests/test_compositor_caps.c \
    src/compositor/hyprland.c src/compositor/sway.c src/compositor/wayfire.c \
    src/compositor/niri.c src/compositor/river.c src/compositor/generic_wayland.c \
    src/compositor/compositor.c src/compositor/workspace_models.c src/core/log.c \
    src/core/monitor.c \
    protocols/river-status-protocol.c
	$(CC) $(TEST_CFLAGS) -Isrc -Isrc/include $^ $(TEST_LIBS) $(PKG_LIBS) -o $@

# New parallax-related tests
tests/test_toml_config: tests/test_toml_config.c src/core/config_toml.c src/core/config.c src/core/parallax.c src/core/log.c src/core/easing.c src/vendor/toml.c \
    src/core/input/input_manager.c src/core/input/providers.c src/core/input/modes/workspace.c \
    src/core/input/modes/cursor.c src/core/input/modes/window.c src/core/animation.c
	$(CC) $(TEST_CFLAGS) -Isrc -Isrc/include $^ $(TEST_LIBS) -o $@

tests/test_runtime_properties: tests/test_runtime_properties.c tests/stubs_gfx.c \
    src/hyprlax_main.c src/core/parallax.c src/core/log.c src/core/config.c src/core/layer.c \
    src/core/monitor.c src/core/event_loop.c src/core/input/input_manager.c src/core/input/providers.c \
    src/core/input/modes/workspace.c src/core/input/modes/cursor.c src/core/input/modes/window.c \
    src/core/animation.c src/core/easing.c src/vendor/toml.c src/core/config_toml.c
	$(CC) $(TEST_CFLAGS) -Isrc -Isrc/include $^ $(TEST_LIBS) $(PKG_LIBS) -o $@

# Run all tests
test: $(ALL_TEST_TARGETS)
	@echo "=== Running Full Test Suite ==="
	@failed=0; \
	for test in $(ALL_TEST_TARGETS); do \
		if [ -x $$test ]; then \
			echo "\n--- Running $$test ---"; \
			if ! HYPRLAX_SOCKET_SUFFIX=tests $$test; then \
				echo "✗ $$test FAILED"; \
				failed=$$((failed + 1)); \
			else \
				echo "✓ $$test PASSED"; \
			fi; \
		fi; \
	done; \
	echo "\n=== Test Summary ==="; \
	if [ $$failed -eq 0 ]; then \
		echo "✓ All tests passed!"; \
	else \
		echo "✗ $$failed test(s) failed"; \
		exit 1; \
	fi

# Run shell-based tests (scripts under tests/)
test-scripts: $(TARGET)
	@echo "=== Running Shell Test Scripts ==="
	@failed=0; total=0; \
	for script in $(SHELL_TESTS); do \
		if [ -x $$script ]; then \
			total=$$((total + 1)); \
			echo "\n--- Running $$script ---"; \
			if ! $$script; then \
				echo "✗ $$script FAILED"; \
				failed=$$((failed + 1)); \
			else \
				echo "✓ $$script PASSED"; \
			fi; \
		fi; \
	done; \
	if [ $$total -eq 0 ]; then \
		echo "(no shell tests found)"; \
	fi; \
	echo "\n=== Shell Test Summary ==="; \
	if [ $$failed -eq 0 ]; then \
		echo "✓ All shell tests passed!"; \
	else \
		echo "✗ $$failed shell test(s) failed"; \
		exit 1; \
	fi

# Run tests with valgrind for memory leak detection
memcheck: $(ALL_TEST_TARGETS)
	@if ! command -v valgrind >/dev/null 2>&1; then \
		echo "Error: valgrind is not installed."; \
		echo "Install it with: sudo pacman -S valgrind (Arch) or sudo apt-get install valgrind (Debian/Ubuntu)"; \
		exit 1; \
	fi
	@echo "=== Running Tests with Valgrind Memory Check ==="
	@failed=0; \
	passed=0; \
	total=0; \
	for test in $(ALL_TEST_TARGETS); do \
		if [ -x $$test ]; then \
			total=$$((total + 1)); \
			echo "\n--- Memory check: $$test ---"; \
			if ! HYPRLAX_SOCKET_SUFFIX=tests $(VALGRIND) $(VALGRIND_FLAGS) --log-file=$$test.valgrind.log $$test > /dev/null 2>&1; then \
				echo "✗ $$test MEMORY ISSUES DETECTED"; \
				cat $$test.valgrind.log; \
				failed=$$((failed + 1)); \
			else \
				echo "✓ $$test MEMORY CLEAN"; \
				rm -f $$test.valgrind.log; \
				passed=$$((passed + 1)); \
			fi; \
		fi; \
	done; \
	echo "\n=== Memory Check Summary ==="; \
	echo "Total: $$total tests"; \
	echo "Passed: $$passed tests"; \
	echo "Failed: $$failed tests"; \
	if [ $$failed -eq 0 ]; then \
		echo "✓ All tests memory clean!"; \
	else \
		echo "✗ $$failed test(s) have memory issues"; \
		exit 1; \
	fi

# Linting targets
lint:
	@if [ -x scripts/lint.sh ]; then \
		./scripts/lint.sh; \
	else \
		echo "Lint script not found. Please ensure scripts/lint.sh exists and is executable."; \
		exit 1; \
	fi

lint-fix:
	@if [ -x scripts/lint.sh ]; then \
		./scripts/lint.sh --fix; \
	else \
		echo "Lint script not found. Please ensure scripts/lint.sh exists and is executable."; \
		exit 1; \
	fi

clean-tests:
	rm -f $(ALL_TEST_TARGETS) tests/*.valgrind.log tests/*.valgrind.log.* tests/*.valgrind.log.core.*

.PHONY: all clean install install-user uninstall uninstall-user test test-scripts memcheck clean-tests lint lint-fix bench bench-perf bench-30fps bench-clean
# Benchmark helpers
bench:
	@./scripts/bench/bench-optimizations.sh

bench-perf:
	@./scripts/bench/bench-performance.sh

bench-30fps:
	@./scripts/bench/bench-30fps.sh

bench-clean:
	@rm -f hyprlax-test-*.log || true

# --- Documentation helpers ---
.PHONY: docs docs-linkcheck

# Build MkDocs site (requires mkdocs)
docs:
	@if command -v mkdocs >/dev/null 2>&1; then \
		mkdocs build -f mkdocs.yml; \
	elif [ -x .venv/bin/mkdocs ]; then \
		.venv/bin/mkdocs build -f mkdocs.yml; \
	else \
		echo "Error: mkdocs not found. Install with 'pip install mkdocs mkdocs-material' or use .venv."; \
		exit 1; \
	fi

# Build docs and run internal link checker
docs-linkcheck: docs
	@python3 scripts/docs_link_check.py
