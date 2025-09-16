#!/bin/bash

# hyprlax remote installer script
# Downloads and installs pre-built binaries from GitHub releases

set -e

# Version of this installer
INSTALLER_VERSION="1.0.0"

# GitHub repository
GITHUB_REPO="sandwichfarm/hyprlax"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
INSTALL_TYPE="user"
FORCE_INSTALL=0
VERSION="latest"

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
Usage: curl -sSL https://hyprlax.com/install.sh | bash [-s -- OPTIONS]

OPTIONS:
    -s, --system      Install system-wide (requires sudo)
    -u, --user        Install for current user only (default)
    -v, --version     Install specific version (default: latest)
    -f, --force       Force reinstall even if same version exists
    -h, --help        Show this help message

EXAMPLES:
    curl -sSL https://hyprlax.com/install.sh | bash
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --system
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --version v1.2.3
EOF
    exit 0
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
        -v|--version)
            VERSION="$2"
            shift 2
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

# Get binary paths based on install type
get_binary_paths() {
    if [ "$INSTALL_TYPE" = "system" ]; then
        echo "/usr/local/bin/hyprlax"
    else
        echo "$HOME/.local/bin/hyprlax"
    fi
}

# Get hyprlax binary path based on install type
get_hyprlax_path() {
    if [ "$INSTALL_TYPE" = "system" ]; then
        echo "/usr/local/bin/hyprlax"
    else
        echo "$HOME/.local/bin/hyprlax"
    fi
}

# Note: hyprlax-ctl is now integrated into hyprlax as 'hyprlax ctl'
# This function is kept for backward compatibility during upgrades

# Get installed version
get_installed_version() {
    local binary_path=$(get_hyprlax_path)
    
    if [ -f "$binary_path" ] && [ -x "$binary_path" ]; then
        # Extract version number from hyprlax --version output
        local full_version=$("$binary_path" --version 2>/dev/null || echo "unknown")
        
        if [ "$full_version" != "unknown" ]; then
            # Extract just the version number (e.g., "1.2.3" from "hyprlax 1.2.3\nSmooth parallax...")
            echo "$full_version" | head -1 | grep -oP '\d+\.\d+\.\d+' || echo "unknown"
        else
            echo "unknown"
        fi
    else
        echo "none"
    fi
}

# Get latest version from GitHub
get_latest_version() {
    local latest=$(curl -sSL "https://api.github.com/repos/${GITHUB_REPO}/releases/latest" | \
                   grep '"tag_name"' | \
                   sed -E 's/.*"([^"]+)".*/\1/')
    
    if [ -z "$latest" ]; then
        print_error "Failed to fetch latest version from GitHub"
        exit 1
    fi
    
    echo "$latest"
}

# Compare versions
compare_versions() {
    local version1="$1"
    local version2="$2"
    
    # Remove 'v' prefix if present
    version1=${version1#v}
    version2=${version2#v}
    
    # Convert to comparable format
    local v1_comparable=$(echo "$version1" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
    local v2_comparable=$(echo "$version2" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
    
    if [ "$v1_comparable" -gt "$v2_comparable" ]; then
        echo "1"  # version1 > version2
    elif [ "$v1_comparable" -lt "$v2_comparable" ]; then
        echo "-1" # version1 < version2
    else
        echo "0"  # version1 == version2
    fi
}

# Detect system architecture
detect_arch() {
    local arch=$(uname -m)
    case $arch in
        x86_64|amd64)
            echo "x86_64"
            ;;
        aarch64|arm64)
            echo "aarch64"
            ;;
        *)
            print_error "Unsupported architecture: $arch"
            print_info "Please build from source: https://github.com/${GITHUB_REPO}"
            exit 1
            ;;
    esac
}

# Download binary
download_binary() {
    local version="$1"
    local arch="$2"
    local temp_file="/tmp/hyprlax-download"
    
    # Construct download URL
    local download_url="https://github.com/${GITHUB_REPO}/releases/download/${version}/hyprlax-${arch}"
    
    print_step "Downloading hyprlax ${version} for ${arch}..." >&2
    
    if curl -sSL "$download_url" -o "$temp_file"; then
        # Verify it's actually a binary
        if file "$temp_file" | grep -q "ELF"; then
            print_success "Download of ${binary_name} successful" >&2
            echo "$temp_file"  # Only output the filename to stdout
        else
            print_error "Downloaded ${binary_name} file is not a valid binary" >&2
            rm -f "$temp_file"
            
            # Check if this architecture is available
            print_warning "Binary ${binary_name} for ${arch} might not be available" >&2
            print_info "Available binaries:" >&2
            curl -sSL "https://api.github.com/repos/${GITHUB_REPO}/releases/tags/${version}" | \
                grep '"name"' | grep "hyprlax" | sed 's/.*"hyprlax/  - hyprlax/' | sed 's/".*//' >&2
            
            print_info "Please build from source: https://github.com/${GITHUB_REPO}" >&2
            exit 1
        fi
    else
        print_error "Failed to download ${binary_name} binary" >&2
        print_info "URL attempted: $download_url" >&2
        exit 1
    fi
}

# Install binary
install_single_binary() {
    local source_file="$1"
    local binary_path="$2"
    local binary_name="$3"
    local install_dir=$(dirname "$binary_path")
    
    # Create installation directory if needed
    if [ "$INSTALL_TYPE" = "user" ]; then
        mkdir -p "$install_dir"
    else
        sudo mkdir -p "$install_dir"
    fi
    
    # Make binary executable
    chmod +x "$source_file"
    
    # Install the binary
    if [ "$INSTALL_TYPE" = "system" ]; then
        print_step "Installing ${binary_name} to $binary_path (requires sudo)..."
        sudo mv "$source_file" "$binary_path"
    else
        print_step "Installing ${binary_name} to $binary_path..."
        mv "$source_file" "$binary_path"
    fi
    
    print_success "${binary_name} installation complete"
}

# Install binary
install_binaries() {
    local hyprlax_file="$1"
    local hyprlax_path=$(get_hyprlax_path)
    local install_dir=$(dirname "$hyprlax_path")
    
    # Install hyprlax binary
    install_single_binary "$hyprlax_file" "$hyprlax_path" "hyprlax"
    
    print_success "hyprlax installed successfully"
    print_info "Control interface integrated: use 'hyprlax ctl <command>'"
    
    # Check if directory is in PATH
    if [ "$INSTALL_TYPE" = "user" ] && [[ ":$PATH:" != *":$install_dir:"* ]]; then
        print_warning "$install_dir is not in your PATH"
        print_info "Add this to your shell config (.bashrc, .zshrc, etc.):"
        print_info "  export PATH=\"$install_dir:\$PATH\""
    fi
}

# Main installation flow
main() {
    echo "================================"
    echo "     hyprlax Installer"
    echo "================================"
    echo
    
    # Detect architecture
    ARCH=$(detect_arch)
    print_info "Detected architecture: $ARCH"
    
    # Get installed version
    INSTALLED_VERSION=$(get_installed_version)
    
    # Determine version to install
    if [ "$VERSION" = "latest" ]; then
        VERSION=$(get_latest_version)
        print_info "Latest version available: $VERSION"
    else
        print_info "Requested version: $VERSION"
    fi
    
    # Remove 'v' prefix for comparison
    VERSION_NUM=${VERSION#v}
    
    # Check if already installed
    if [ "$INSTALLED_VERSION" != "none" ] && [ "$INSTALLED_VERSION" != "unknown" ]; then
        print_info "Currently installed: $INSTALLED_VERSION"
        
        # Compare versions
        CMP=$(compare_versions "$INSTALLED_VERSION" "$VERSION_NUM")
        
        if [ "$CMP" = "0" ] && [ "$FORCE_INSTALL" = "0" ]; then
            print_success "Version $VERSION is already installed"
            print_info "Use --force to reinstall"
            exit 0
        elif [ "$CMP" = "1" ]; then
            print_warning "Installed version ($INSTALLED_VERSION) is newer than requested ($VERSION_NUM)"
            if [ "$FORCE_INSTALL" = "0" ]; then
                print_info "Use --force to downgrade"
                exit 0
            fi
        elif [ "$CMP" = "-1" ]; then
            print_success "Upgrade available: $INSTALLED_VERSION → $VERSION_NUM"
        fi
        
        # Check if hyprlax is running
        if pgrep -x hyprlax > /dev/null; then
            print_warning "hyprlax is currently running"
            print_step "It will be restarted after installation"
            
            # Save wallpaper path if possible
            WALLPAPER_PATH=$(ps aux | grep "[h]yprlax" | grep -oE '/[^ ]+\.(jpg|png)' | head -1 || echo "")
            
            print_step "Stopping hyprlax..."
            pkill hyprlax || true
            sleep 0.5
        fi
    else
        print_info "No existing installation found"
    fi
    
    # Confirm installation
    if [ "$FORCE_INSTALL" = "0" ] && [ "$INSTALLED_VERSION" != "none" ]; then
        echo
        # Use /dev/tty for input when piped through bash
        if [ -t 0 ]; then
            read -p "Do you want to proceed with installation? (y/N) " -n 1 -r
        else
            read -p "Do you want to proceed with installation? (y/N) " -n 1 -r < /dev/tty
        fi
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_info "Installation cancelled"
            exit 0
        fi
    fi
    
    # Download binary
    HYPRLAX_FILE=$(download_binary "$VERSION" "$ARCH")
    
    # Backup existing installation
    if [ "$INSTALLED_VERSION" != "none" ]; then
        local hyprlax_path=$(get_hyprlax_path)
        
        if [ -f "$hyprlax_path" ]; then
            local backup_path="${hyprlax_path}.backup.$(date +%Y%m%d_%H%M%S)"
            print_step "Backing up existing hyprlax binary..."
            if [ "$INSTALL_TYPE" = "system" ]; then
                sudo cp "$hyprlax_path" "$backup_path"
            else
                cp "$hyprlax_path" "$backup_path"
            fi
        fi
        
        # Clean up old hyprlax-ctl if it exists (now integrated)
        if [ "$INSTALL_TYPE" = "system" ]; then
            if [ -f "/usr/local/bin/hyprlax-ctl" ]; then
                print_step "Removing old hyprlax-ctl (now integrated)..."
                sudo rm -f "/usr/local/bin/hyprlax-ctl"
            fi
        else
            if [ -f "$HOME/.local/bin/hyprlax-ctl" ]; then
                print_step "Removing old hyprlax-ctl (now integrated)..."
                rm -f "$HOME/.local/bin/hyprlax-ctl"
            fi
        fi
    fi
    
    # Install binary
    install_binaries "$HYPRLAX_FILE"
    
    # Restart if it was running
    if [ -n "$WALLPAPER_PATH" ] && [ -f "$WALLPAPER_PATH" ]; then
        print_step "Restarting hyprlax..."
        nohup hyprlax "$WALLPAPER_PATH" > /dev/null 2>&1 &
        sleep 0.5
        if pgrep -x hyprlax > /dev/null; then
            print_success "hyprlax restarted with previous wallpaper"
        fi
    fi
    
    # Show completion message
    echo
    echo "================================"
    echo "   Installation Complete!"
    echo "================================"
    echo
    
    if [ "$INSTALLED_VERSION" = "none" ]; then
        print_info "hyprlax $VERSION_NUM has been installed"
        echo
        print_info "To get started:"
        print_step "1. Add to your compositor config:"
        echo "      exec-once = hyprlax /path/to/wallpaper.jpg"
        print_step "2. Reload your compositor or logout/login"
    else
        print_success "hyprlax has been updated to $VERSION_NUM"
    fi
    
    echo
    print_info "For more information:"
    print_step "GitHub: https://github.com/${GITHUB_REPO}"
    print_step "Usage: hyprlax --help"
    print_step "Runtime control: hyprlax ctl --help"
}

# Run main function
main