#!/bin/bash

# hyprlax installer script with upgrade support

set -e

# Version of this installer
INSTALLER_VERSION="1.2.0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
INSTALL_TYPE=""  # Will be set interactively if not specified
BUILD_ONLY=0
FORCE_INSTALL=0
IS_UPGRADE=0
EXISTING_VERSION=""
NEW_VERSION=""

# Print colored output
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_step() {
    echo -e "${CYAN}→${NC} $1"
}

# Show usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

OPTIONS:
    -s, --system      Install system-wide (requires sudo)
    -u, --user        Install for current user only
    -b, --build-only  Only build, don't install
    -f, --force       Force reinstall even if same version exists
    -h, --help        Show this help message

EXAMPLES:
    $0                # Interactive installation
    $0 --system       # Install/upgrade system-wide
    $0 --user         # Install/upgrade for current user
    $0 --force        # Force reinstall
    $0 --build-only   # Only build the binary
EOF
    exit 0
}

# Prompt for installation type
prompt_install_type() {
    if [ -n "$INSTALL_TYPE" ]; then
        # Already set via command line
        return
    fi
    
    echo "================================"
    echo "   Installation Location"
    echo "================================"
    echo
    print_info "Please select installation location:"
    echo
    echo "  ${GREEN}1)${NC} System-wide ${CYAN}(/usr/local/bin)${NC} ${GREEN}[RECOMMENDED]${NC}"
    echo "     • Available to all users"
    echo "     • Works with compositor autostart (exec-once)"
    echo "     • Requires sudo for installation"
    echo
    echo "  ${YELLOW}2)${NC} User-specific ${CYAN}(~/.local/bin)${NC}"
    echo "     • Only available to current user"
    echo "     • ${YELLOW}May not work with compositor autostart${NC}"
    echo "     • No sudo required"
    echo
    
    while true; do
        read -p "Select option [1-2] (default: 1): " -n 1 -r choice
        echo
        
        case "$choice" in
            1|"")
                INSTALL_TYPE="system"
                print_success "Selected: System-wide installation"
                break
                ;;
            2)
                INSTALL_TYPE="user"
                print_warning "Selected: User installation"
                print_warning "Note: You may need to use full path in exec-once"
                break
                ;;
            *)
                print_error "Invalid choice. Please select 1 or 2."
                ;;
        esac
    done
    echo
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -s|--system)
            INSTALL_TYPE="system"
            shift
            ;;
        -u|--user)
            INSTALL_TYPE="user"
            shift
            ;;
        -b|--build-only)
            BUILD_ONLY=1
            shift
            ;;
        -f|--force)
            FORCE_INSTALL=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
done

# Get binary path based on install type
get_binary_path() {
    if [ "$INSTALL_TYPE" = "system" ]; then
        echo "/usr/local/bin/hyprlax"
    else
        echo "$HOME/.local/bin/hyprlax"
    fi
}

# Check if hyprlax is already installed
check_existing_installation() {
    local binary_path=$(get_binary_path)
    
    # Check both user and system installations
    local user_binary="$HOME/.local/bin/hyprlax"
    local system_binary="/usr/local/bin/hyprlax"
    
    if [ -f "$user_binary" ] || [ -f "$system_binary" ]; then
        IS_UPGRADE=1
        
        # Get version of existing installation
        if [ -f "$binary_path" ] && [ -x "$binary_path" ]; then
            # Try to get version - hyprlax --version returns multiple lines, extract just the version number
            local full_version=$("$binary_path" --version 2>/dev/null || echo "unknown")
            
            if [ "$full_version" != "unknown" ]; then
                # Extract just the version number (e.g., "1.2.3" from "hyprlax 1.2.3\nSmooth parallax...")
                EXISTING_VERSION=$(echo "$full_version" | head -1 | grep -oP '\d+\.\d+\.\d+' || echo "$full_version")
            else
                EXISTING_VERSION="unknown"
            fi
            
            if [ "$EXISTING_VERSION" = "unknown" ] || [ -z "$EXISTING_VERSION" ]; then
                # Fallback: check file modification time
                local mod_date=$(stat -c %y "$binary_path" 2>/dev/null | cut -d' ' -f1 || echo "unknown")
                EXISTING_VERSION="installed on $mod_date"
            fi
        fi
        
        # Check if hyprlax is currently running
        if pgrep -x hyprlax > /dev/null; then
            print_warning "hyprlax is currently running"
            print_step "It will be restarted after upgrade"
        fi
        
        return 0
    fi
    
    return 1
}

# Check dependencies
check_dependencies() {
    if [ $IS_UPGRADE -eq 1 ]; then
        print_step "Checking dependencies for upgrade..."
    else
        print_info "Checking dependencies..."
    fi
    
    local missing_deps=()
    
    # Check for required commands
    for cmd in gcc make pkg-config wayland-scanner; do
        if ! command -v $cmd &> /dev/null; then
            missing_deps+=($cmd)
        fi
    done
    
    # Check for required libraries using pkg-config
    for lib in wayland-client wayland-protocols wayland-egl egl glesv2; do
        if ! pkg-config --exists $lib 2>/dev/null; then
            missing_deps+=($lib)
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_info "Please install the missing dependencies:"
        
        # Detect distro and suggest install command
        if [ -f /etc/arch-release ]; then
            print_info "  sudo pacman -S base-devel wayland wayland-protocols mesa"
        elif [ -f /etc/debian_version ]; then
            print_info "  sudo apt install build-essential libwayland-dev wayland-protocols libegl1-mesa-dev libgles2-mesa-dev pkg-config"
        elif [ -f /etc/fedora-release ]; then
            print_info "  sudo dnf install gcc make wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGLES-devel pkg-config"
        else
            print_info "  Install: gcc, make, wayland development libraries, EGL/GLES2 libraries"
        fi
        
        exit 1
    fi
    
    print_success "All dependencies satisfied"
}

# Backup existing installation
backup_existing() {
    local binary_path=$(get_binary_path)
    
    if [ -f "$binary_path" ]; then
        local backup_path="${binary_path}.backup.$(date +%Y%m%d_%H%M%S)"
        print_step "Backing up existing binary to $(basename $backup_path)"
        
        if [ "$INSTALL_TYPE" = "system" ]; then
            sudo cp "$binary_path" "$backup_path"
        else
            cp "$binary_path" "$backup_path"
        fi
        
        print_success "Backup created"
    fi
}

# Build hyprlax
build() {
    # First get the version from source before building
    if [ -f "src/include/hyprlax_internal.h" ]; then
        NEW_VERSION=$(grep -oP '#define HYPRLAX_VERSION "\K[^"]+' src/include/hyprlax_internal.h 2>/dev/null || echo "unknown")
    elif [ -f "src/hyprlax.c" ]; then
        NEW_VERSION=$(grep -oP '#define HYPRLAX_VERSION "\K[^"]+' src/hyprlax.c 2>/dev/null || echo "unknown")
    fi
    
    if [ $IS_UPGRADE -eq 1 ]; then
        print_step "Building new version..."
        
        # Show version comparison
        echo
        print_info "Version information:"
        print_step "  Installed version: ${CYAN}$EXISTING_VERSION${NC}"
        print_step "  Available version: ${GREEN}$NEW_VERSION${NC}"
        
        # Check if versions are the same (if we can determine them)
        if [ "$EXISTING_VERSION" = "$NEW_VERSION" ] && [ "$NEW_VERSION" != "unknown" ] && [ $FORCE_INSTALL -eq 0 ]; then
            print_warning "Same version already installed!"
            echo
            read -p "Do you want to reinstall anyway? (y/N) " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                print_info "Installation cancelled"
                exit 0
            fi
        elif [ "$NEW_VERSION" != "unknown" ] && [ "$EXISTING_VERSION" != "unknown" ]; then
            # Proper semantic version comparison
            local existing_comparable=$(echo "$EXISTING_VERSION" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
            local new_comparable=$(echo "$NEW_VERSION" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
            
            if [ -n "$existing_comparable" ] && [ -n "$new_comparable" ]; then
                if [ "$new_comparable" -lt "$existing_comparable" ]; then
                    print_warning "You are installing an older version!"
                fi
            fi
        fi
        echo
    else
        print_info "Building hyprlax..."
        if [ "$NEW_VERSION" != "unknown" ]; then
            print_step "Version to install: ${GREEN}$NEW_VERSION${NC}"
        fi
    fi
    
    if [ ! -f "Makefile" ]; then
        print_error "Makefile not found! Are you in the hyprlax directory?"
        exit 1
    fi
    
    # Clean and build
    make clean > /dev/null 2>&1
    
    if make > /dev/null 2>&1; then
        print_success "Build successful"
        
        # Verify version of built binary
        if [ -f "./hyprlax" ]; then
            local built_version=$(./hyprlax --version 2>/dev/null || echo "")
            if [ -n "$built_version" ] && [ "$built_version" != "$NEW_VERSION" ]; then
                NEW_VERSION="$built_version"
            fi
        fi
    else
        print_error "Build failed!"
        # Show actual build output on failure
        make
        exit 1
    fi
}

# Install hyprlax
install() {
    if [ $BUILD_ONLY -eq 1 ]; then
        print_success "Build complete. Binary available at: ./hyprlax"
        print_info "Note: hyprlax-ctl is now integrated as 'hyprlax ctl'"
        return
    fi
    
    local was_running=0
    local wallpaper_path=""
    
    # If upgrading, save current wallpaper and stop hyprlax
    if [ $IS_UPGRADE -eq 1 ]; then
        if pgrep -x hyprlax > /dev/null; then
            was_running=1
            # Try to get current wallpaper from process args
            wallpaper_path=$(ps aux | grep "[h]yprlax" | grep -oE '/[^ ]+\.(jpg|png)' | head -1 || echo "")
            
            print_step "Stopping existing hyprlax process..."
            pkill hyprlax || true
            sleep 0.5
        fi
        
        backup_existing
    fi
    
    if [ "$INSTALL_TYPE" = "system" ]; then
        if [ $IS_UPGRADE -eq 1 ]; then
            print_step "Upgrading system-wide installation..."
        else
            print_info "Installing system-wide (requires sudo)..."
        fi
        
        if sudo make install; then
            print_success "Installed to /usr/local/bin/hyprlax"
            print_info "Control interface: hyprlax ctl <command>"
        else
            print_error "Installation failed!"
            exit 1
        fi
    else
        if [ $IS_UPGRADE -eq 1 ]; then
            print_step "Upgrading user installation..."
        else
            print_info "Installing for current user..."
        fi
        
        # Create ~/.local/bin if it doesn't exist
        mkdir -p ~/.local/bin
        
        if make install-user; then
            print_success "Installed to ~/.local/bin/hyprlax"
            print_info "Control interface: hyprlax ctl <command>"
            
            # Check if ~/.local/bin is in PATH
            if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
                print_warning "~/.local/bin is not in your PATH"
                print_info "Add this to your shell config (.bashrc, .zshrc, etc.):"
                print_info "  export PATH=\"\$HOME/.local/bin:\$PATH\""
            fi
        else
            print_error "Installation failed!"
            exit 1
        fi
    fi
    
    # Restart hyprlax if it was running before upgrade
    if [ $was_running -eq 1 ] && [ -n "$wallpaper_path" ]; then
        print_step "Restarting hyprlax with previous wallpaper..."
        nohup hyprlax "$wallpaper_path" > /dev/null 2>&1 &
        sleep 0.5  # Give it a moment to start
        if pgrep -x hyprlax > /dev/null; then
            print_success "hyprlax restarted successfully"
        else
            print_warning "Failed to restart hyprlax automatically"
            print_info "Start it manually with: hyprlax $wallpaper_path"
        fi
    fi
}

# Create example config
create_example_config() {
    local config_dir="$HOME/.config/hyprlax"
    
    if [ $IS_UPGRADE -eq 1 ]; then
        if [ -f "$config_dir/example.conf" ]; then
            print_step "Updating example configuration..."
        else
            print_step "Creating example configuration..."
        fi
    else
        print_info "Creating example configuration..."
    fi
    
    mkdir -p "$config_dir"
    
    cat > "$config_dir/example.conf" << 'EOF'
# Example hyprlax configuration for Hyprland
# Add this to your ~/.config/hypr/hyprland.conf

# Kill any existing wallpaper daemons
exec-once = pkill swww-daemon; pkill hyprpaper; pkill hyprlax

# Start hyprlax with your wallpaper
# Basic usage (works after system-wide installation):
exec-once = hyprlax ~/Pictures/wallpaper.jpg

# If installed to ~/.local/bin, use full path:
# exec-once = ~/.local/bin/hyprlax ~/Pictures/wallpaper.jpg

# With custom settings:
# exec-once = hyprlax -d 1.0 -s 200 -e expo ~/Pictures/wallpaper.jpg

# Advanced options:
# -s, --shift <pixels>     Pixels to shift per workspace (default: 200)
# -d, --duration <seconds> Animation duration (default: 1.0)
# -e, --easing <type>      Easing function (linear, quad, cubic, quart, quint, sine, expo, circ, back, elastic)
# --delay <seconds>        Delay before animation starts
# --fps <rate>             Target frame rate (default: 144)

# Keybinding to change wallpaper:
# bind = $mainMod, W, exec, pkill hyprlax && hyprlax $(find ~/Pictures -type f \( -name "*.jpg" -o -name "*.png" \) | shuf -n 1)
EOF
    
    print_success "Example configuration saved to: $config_dir/example.conf"
}

# Show completion message
show_completion_message() {
    echo
    
    if [ $IS_UPGRADE -eq 1 ]; then
        echo "================================"
        echo "   hyprlax Upgrade Complete!"
        echo "================================"
        echo
        
        # Always show version information when available
        if [ "$EXISTING_VERSION" != "unknown" ] || [ "$NEW_VERSION" != "unknown" ]; then
            print_success "Version information:"
            if [ "$EXISTING_VERSION" != "unknown" ]; then
                print_step "  Previous version: ${CYAN}$EXISTING_VERSION${NC}"
            else
                print_step "  Previous version: ${CYAN}(could not determine)${NC}"
            fi
            
            if [ "$NEW_VERSION" != "unknown" ] && [ "$NEW_VERSION" != "latest" ]; then
                print_step "  Installed version: ${GREEN}$NEW_VERSION${NC}"
            else
                print_step "  Installed version: ${GREEN}(latest from source)${NC}"
            fi
        else
            print_success "Successfully upgraded hyprlax"
        fi
        
        echo
        print_info "Changes in this version:"
        print_step "Check release notes at: https://github.com/yourusername/hyprlax/releases"
        
        if pgrep -x hyprlax > /dev/null; then
            echo
            print_success "hyprlax is running with your previous configuration"
        else
            echo
            print_info "Start hyprlax with:"
            print_step "hyprlax /path/to/wallpaper.jpg"
        fi
    else
        echo "================================"
        echo "   hyprlax Installation Complete!"
        echo "================================"
        echo
        
        print_success "First time installation successful!"
        echo
        print_info "Next steps:"
        print_step "1. Add hyprlax to your Hyprland config:"
        echo "      exec-once = hyprlax /path/to/wallpaper.jpg"
        print_step "2. Reload Hyprland (Super+Shift+R) or logout/login"
        print_step "3. Check the example config at: ~/.config/hyprlax/example.conf"
    fi
    
    echo
    print_info "To uninstall, run:"
    if [ "$INSTALL_TYPE" = "system" ]; then
        print_step "sudo make uninstall"
    else
        print_step "make uninstall-user"
    fi
}

# Main installation flow
main() {
    if [ $BUILD_ONLY -eq 0 ]; then
        # Check for existing installation
        check_existing_installation || true
        
        # Prompt for installation type if not specified
        prompt_install_type
        
        if [ $IS_UPGRADE -eq 1 ]; then
            echo "================================"
            echo "     hyprlax Upgrade"
            echo "================================"
            echo
            
            print_info "Existing installation detected"
            
            # Get the new version from source before asking to upgrade
            if [ -f "src/include/hyprlax_internal.h" ]; then
                NEW_VERSION=$(grep -oP '#define HYPRLAX_VERSION "\K[^"]+' src/include/hyprlax_internal.h 2>/dev/null || echo "unknown")
            elif [ -f "src/hyprlax.c" ]; then
                NEW_VERSION=$(grep -oP '#define HYPRLAX_VERSION "\K[^"]+' src/hyprlax.c 2>/dev/null || echo "unknown")
            fi
            
            # Display version information clearly
            print_info "Version comparison:"
            if [ "$EXISTING_VERSION" != "unknown" ]; then
                print_step "  Currently installed: ${CYAN}$EXISTING_VERSION${NC}"
            else
                print_step "  Currently installed: ${CYAN}(version unknown)${NC}"
            fi
            
            if [ "$NEW_VERSION" != "unknown" ]; then
                print_step "  Available to install: ${GREEN}$NEW_VERSION${NC}"
            else
                print_step "  Available to install: ${GREEN}(latest from source)${NC}"
            fi
            
            # Check if it's actually an upgrade, downgrade, or same version
            if [ "$EXISTING_VERSION" = "$NEW_VERSION" ] && [ "$NEW_VERSION" != "unknown" ]; then
                print_warning "Same version already installed!"
            elif [ "$NEW_VERSION" != "unknown" ] && [ "$EXISTING_VERSION" != "unknown" ]; then
                # Proper semantic version comparison
                # Convert versions to comparable format (e.g., 1.2.3 -> 001002003)
                local existing_comparable=$(echo "$EXISTING_VERSION" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
                local new_comparable=$(echo "$NEW_VERSION" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
                
                if [ -n "$existing_comparable" ] && [ -n "$new_comparable" ]; then
                    if [ "$new_comparable" -gt "$existing_comparable" ]; then
                        print_success "Upgrade available!"
                    elif [ "$new_comparable" -lt "$existing_comparable" ]; then
                        print_warning "This would downgrade your installation!"
                    fi
                fi
            fi
            
            if [ $FORCE_INSTALL -eq 0 ]; then
                echo
                read -p "Do you want to proceed? (y/N) " -n 1 -r
                echo
                if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                    print_info "Installation cancelled"
                    exit 0
                fi
            else
                print_info "Force install enabled - proceeding"
            fi
        else
            echo "================================"
            echo "     hyprlax Installer"
            echo "================================"
            echo
        fi
    fi
    
    check_dependencies
    build
    install
    create_example_config
    show_completion_message
}

main