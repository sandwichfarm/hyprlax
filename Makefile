CC = gcc
# Use generic architecture for CI compatibility
ifdef CI
CFLAGS = -Wall -Wextra -O2 -Isrc -Isrc/include
LDFLAGS =
else
CFLAGS = -Wall -Wextra -O3 -march=native -flto -Isrc -Isrc/include
LDFLAGS = -flto
endif

# Cross-compilation support
ifdef ARCH
ifeq ($(ARCH),aarch64)
CC = aarch64-linux-gnu-gcc
CFLAGS = -Wall -Wextra -O3 -flto -Isrc
endif
endif

# Package dependencies
PKG_DEPS = wayland-client wayland-protocols wayland-egl egl glesv2 x11 xext

# Get package flags
PKG_CFLAGS = $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LIBS = $(shell pkg-config --libs $(PKG_DEPS))

# Wayland protocols
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

# Protocol files
XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_PROTOCOL = protocols/wlr-layer-shell-unstable-v1.xml

# Generated protocol files  
PROTOCOL_SRCS = protocols/xdg-shell-protocol.c protocols/wlr-layer-shell-protocol.c
PROTOCOL_HDRS = protocols/xdg-shell-client-protocol.h protocols/wlr-layer-shell-client-protocol.h

# Core module sources
CORE_SRCS = src/core/easing.c src/core/animation.c src/core/layer.c src/core/config.c

# Renderer module sources
RENDERER_SRCS = src/renderer/renderer.c src/renderer/shader.c src/renderer/gles2.c

# Platform module sources
PLATFORM_SRCS = src/platform/platform.c src/platform/wayland.c src/platform/x11.c

# Compositor module sources
COMPOSITOR_SRCS = src/compositor/compositor.c src/compositor/hyprland.c \
                  src/compositor/wayfire.c src/compositor/niri.c \
                  src/compositor/sway.c src/compositor/river.c \
                  src/compositor/generic_wayland.c src/compositor/x11_ewmh.c

# Main module sources
MAIN_SRCS = src/main.c src/hyprlax_main.c src/hyprlax_ctl.c

# Source files - Choose between legacy monolithic or new modular
ifdef USE_LEGACY
SRCS = src/hyprlax.c src/ipc.c $(CORE_SRCS) $(RENDERER_SRCS) $(PLATFORM_SRCS) \
       $(COMPOSITOR_SRCS) $(PROTOCOL_SRCS)
else
SRCS = $(MAIN_SRCS) src/ipc.c $(CORE_SRCS) $(RENDERER_SRCS) $(PLATFORM_SRCS) \
       $(COMPOSITOR_SRCS) $(PROTOCOL_SRCS)
endif
OBJS = $(SRCS:.c=.o)
TARGET = hyprlax

all: $(TARGET)

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

# Compile
%.o: %.c $(PROTOCOL_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(PKG_LIBS) -lm -lX11 -o $@

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

TEST_TARGETS = tests/test_integration tests/test_ipc tests/test_blur tests/test_config tests/test_animation tests/test_easing tests/test_shader tests/test_platform tests/test_compositor tests/test_modules tests/test_renderer tests/test_workspace_changes tests/test_animation_state tests/test_config_validation tests/test_x11_platform
ALL_TESTS = $(filter tests/test_%, $(wildcard tests/test_*.c))
ALL_TEST_TARGETS = $(ALL_TESTS:.c=)

# Individual test rules - updated for Check framework
tests/test_integration: tests/test_integration.c src/ipc.c
	$(CC) $(TEST_CFLAGS) $^ $(TEST_LIBS) -lpthread -o $@

tests/test_ctl: tests/test_ctl.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_renderer: tests/test_renderer.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

tests/test_ipc: tests/test_ipc.c src/ipc.c
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

tests/test_x11_platform: tests/test_x11_platform.c
	$(CC) $(TEST_CFLAGS) $< $(TEST_LIBS) -o $@

# Run all tests
test: $(ALL_TEST_TARGETS)
	@echo "=== Running Full Test Suite ==="
	@failed=0; \
	for test in $(ALL_TEST_TARGETS); do \
		if [ -x $$test ]; then \
			echo "\n--- Running $$test ---"; \
			if ! $$test; then \
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

# Run tests with valgrind for memory leak detection
memcheck: $(ALL_TEST_TARGETS)
	@if ! command -v valgrind >/dev/null 2>&1; then \
		echo "Error: valgrind is not installed."; \
		echo "Install it with: sudo pacman -S valgrind (Arch) or sudo apt-get install valgrind (Debian/Ubuntu)"; \
		exit 1; \
	fi
	@echo "=== Running Tests with Valgrind Memory Check ==="
	@failed=0; \
	for test in $(ALL_TEST_TARGETS); do \
		if [ -x $$test ]; then \
			echo "\n--- Memory check: $$test ---"; \
			if ! $(VALGRIND) $(VALGRIND_FLAGS) --log-file=$$test.valgrind.log $$test > /dev/null 2>&1; then \
				echo "✗ $$test MEMORY ISSUES DETECTED"; \
				cat $$test.valgrind.log; \
				failed=$$((failed + 1)); \
			else \
				echo "✓ $$test MEMORY CLEAN"; \
				rm -f $$test.valgrind.log; \
			fi; \
		fi; \
	done; \
	echo "\n=== Memory Check Summary ==="; \
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

.PHONY: all clean install install-user uninstall uninstall-user test memcheck clean-tests lint lint-fix