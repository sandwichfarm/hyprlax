# Makefile for Hyprlax Wayfire Plugin

# Plugin name (Wayfire expects lib prefix)
PLUGIN_NAME = libhyprlax
TARGET = $(PLUGIN_NAME).so

# Source files (full rendering version)
SRCS = libhyprlax.cpp
OBJS = $(SRCS:.cpp=.o)

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -fPIC -O3 -march=native
# Try to get GLM include path directly if pkg-config fails
GLM_INCLUDE = $(shell pkg-config --cflags glm 2>/dev/null || echo "")
ifeq ($(GLM_INCLUDE),)
    # Fallback to common GLM locations
    CXXFLAGS += -I/usr/include
else
    CXXFLAGS += $(GLM_INCLUDE)
endif
CXXFLAGS += $(shell pkg-config --cflags wayfire wlroots-0.19)
LDFLAGS = -shared
LIBS = $(shell pkg-config --libs wayfire wlroots-0.19)

# Installation paths (can be overridden)
PREFIX ?= /usr
PLUGIN_DIR ?= $(PREFIX)/lib/wayfire

# Build rules
.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	@echo "Installing plugin to $(PLUGIN_DIR)"
	install -D -m 755 $(TARGET) $(PLUGIN_DIR)/$(TARGET)
	@# Proactively move any older plugin filename out of the way to avoid confusion
	@if [ -f "$(PLUGIN_DIR)/hyprlax-wayfire-plugin.so" ]; then \
		echo "Backing up existing $(PLUGIN_DIR)/hyprlax-wayfire-plugin.so -> $(PLUGIN_DIR)/hyprlax-wayfire-plugin.so.bak"; \
		mv -f "$(PLUGIN_DIR)/hyprlax-wayfire-plugin.so" "$(PLUGIN_DIR)/hyprlax-wayfire-plugin.so.bak"; \
	fi
	@echo "Plugin installed as $(PLUGIN_DIR)/$(TARGET)"
	@echo "Hint: Ensure WAYFIRE_PLUGIN_PATH includes $(PLUGIN_DIR) and enable [workarounds] enable_so_unloading = true for hot reloads."

uninstall:
	rm -f $(PLUGIN_DIR)/$(TARGET)

# Development helpers
check-deps:
	@echo "Checking for required development files..."
	@pkg-config --exists wayfire && echo "✓ Wayfire development files found" || echo "✗ Wayfire development files not found"
	@pkg-config --exists wlroots-0.19 && echo "✓ wlroots development files found" || echo "✗ wlroots development files not found"
	@test -f /usr/include/glm/glm.hpp && echo "✓ GLM headers found" || echo "✗ GLM headers not found - install glm-dev/libglm-dev"
	@echo ""
	@echo "Versions:"
	@pkg-config --modversion wayfire 2>/dev/null || true
	@pkg-config --modversion wlroots-0.19 2>/dev/null || true

info:
	@echo "Plugin: $(PLUGIN_NAME)"
	@echo "Target: $(TARGET)"
	@echo "Install directory: $(PLUGIN_DIR)"
	@echo "Compiler flags: $(CXXFLAGS)"
	@echo "Linker flags: $(LDFLAGS) $(LIBS)"
