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
BOLD='\033[1m'
NC='\033[0m' # No Color

# Default values
INSTALL_TYPE="user"
FORCE_INSTALL=0
VERSION="latest"
VERSION_2=0
INCLUDE_PRERELEASES=0
ALLOW_DOWNGRADE=0

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
    -2, --v2          Install latest version 2.x.x release
    -p, --prerelease  Include prereleases when using --v2
    -d, --downgrade   Allow downgrades without confirmation
    -f, --force       Force reinstall even if same version exists
    -h, --help        Show this help message

EXAMPLES:
    curl -sSL https://hyprlax.com/install.sh | bash
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --system
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --version v1.2.3
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --v2
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --v2 --prerelease
    curl -sSL https://hyprlax.com/install.sh | bash -s -- --version v1.0.0 --downgrade
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
        -2|--v2)
            VERSION_2=1
            shift
            ;;
        -p|--prerelease)
            INCLUDE_PRERELEASES=1
            shift
            ;;
        -d|--downgrade)
            ALLOW_DOWNGRADE=1
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

# Get binary paths based on install type
get_binary_paths() {
    if [ "$INSTALL_TYPE" = "system" ]; then
        echo "/usr/local/bin/hyprlax /usr/local/bin/hyprlax-ctl"
    else
        echo "$HOME/.local/bin/hyprlax $HOME/.local/bin/hyprlax-ctl"
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

# Get hyprlax-ctl binary path based on install type  
get_ctl_path() {
    if [ "$INSTALL_TYPE" = "system" ]; then
        echo "/usr/local/bin/hyprlax-ctl"
    else
        echo "$HOME/.local/bin/hyprlax-ctl"
    fi
}

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

# Check for all hyprlax installations on the system
find_all_installations() {
    local installations=""
    
    # Check common installation paths
    local paths=(
        "/usr/local/bin/hyprlax"
        "/usr/bin/hyprlax"
        "$HOME/.local/bin/hyprlax"
        "/opt/hyprlax/bin/hyprlax"
    )
    
    for path in "${paths[@]}"; do
        if [ -f "$path" ] && [ -x "$path" ]; then
            local version=$("$path" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' || echo "unknown")
            installations="${installations}${path}:${version}|"
        fi
    done
    
    # Also check what's in PATH
    local in_path=$(which hyprlax 2>/dev/null || echo "")
    if [ -n "$in_path" ]; then
        local version=$("$in_path" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' || echo "unknown")
        installations="${installations}PATH:${in_path}:${version}|"
    fi
    
    echo "${installations%|}"  # Remove trailing |
}

# Determine which binary would be active after installation
get_active_binary_after_install() {
    local install_path="$1"
    
    # Check PATH order
    IFS=':' read -ra PATH_ARRAY <<< "$PATH"
    for dir in "${PATH_ARRAY[@]}"; do
        # Check if this directory would have hyprlax
        if [ "$dir/hyprlax" = "$install_path" ]; then
            echo "$install_path"
            return
        elif [ -f "$dir/hyprlax" ] && [ -x "$dir/hyprlax" ]; then
            # Another hyprlax exists earlier in PATH
            echo "$dir/hyprlax"
            return
        fi
    done
    
    echo "$install_path"
}

# Present installation options to user
prompt_installation_choice() {
    local target_version="$1"
    local target_path="$2"
    local installations="$3"
    
    echo
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}🔍 Multiple hyprlax installations detected${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo
    
    # Show existing installations
    echo -e "${YELLOW}Existing installations:${NC}"
    IFS='|' read -ra INSTALLS <<< "$installations"
    for install in "${INSTALLS[@]}"; do
        if [[ "$install" == PATH:* ]]; then
            IFS=':' read -r _ path version <<< "$install"
            echo -e "  ${GREEN}→ $path${NC} (version: $version) ${CYAN}[CURRENTLY ACTIVE]${NC}"
        else
            IFS=':' read -r path version <<< "$install"
            echo -e "  → $path (version: $version)"
        fi
    done
    
    echo
    echo -e "${YELLOW}Target installation:${NC}"
    echo -e "  ${BLUE}→ $target_path${NC} (version: $target_version)"
    
    # Determine what would be active after installation
    local active_after=$(get_active_binary_after_install "$target_path")
    echo
    if [ "$active_after" != "$target_path" ]; then
        echo -e "${RED}⚠️  WARNING: After installation, the active binary would be:${NC}"
        echo -e "  ${RED}$active_after${NC}"
        echo -e "  ${YELLOW}(not your newly installed version!)${NC}"
    else
        echo -e "${GREEN}✓ Your new installation would be the active binary${NC}"
    fi
    
    echo
    echo -e "${CYAN}Installation options:${NC}"
    echo -e "  ${BOLD}1)${NC} Install to $target_path anyway"
    echo -e "  ${BOLD}2)${NC} Remove ALL other installations and install fresh"
    echo -e "  ${BOLD}3)${NC} Cancel installation"
    
    if [ "$active_after" != "$target_path" ]; then
        echo
        echo -e "  ${YELLOW}Recommended: Option 2 to avoid conflicts${NC}"
    fi
    
    echo
    echo -n -e "${CYAN}Please choose (1-3): ${NC}"
    
    # Read user choice
    if [ -t 0 ]; then
        read -r CHOICE
    else
        read -r CHOICE < /dev/tty
    fi
    
    echo "$CHOICE"
}

# Remove all hyprlax installations
remove_all_installations() {
    local installations="$1"
    
    echo
    print_step "Removing existing installations..."
    
    IFS='|' read -ra INSTALLS <<< "$installations"
    for install in "${INSTALLS[@]}"; do
        if [[ "$install" == PATH:* ]]; then
            IFS=':' read -r _ path _ <<< "$install"
        else
            IFS=':' read -r path _ <<< "$install"
        fi
        
        if [ -f "$path" ]; then
            # Determine if we need sudo
            if [ -w "$(dirname "$path")" ]; then
                rm -f "$path"
                print_success "Removed $path"
            else
                sudo rm -f "$path"
                print_success "Removed $path (with sudo)"
            fi
            
            # Also remove hyprlax-ctl if it exists
            local ctl_path="${path%-hyprlax}-hyprlax-ctl"
            if [ "$ctl_path" != "$path" ] && [ -f "$ctl_path" ]; then
                if [ -w "$(dirname "$ctl_path")" ]; then
                    rm -f "$ctl_path"
                else
                    sudo rm -f "$ctl_path"
                fi
                print_success "Removed $ctl_path"
            fi
        fi
    done
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

# Get latest v2.x.x release from GitHub
get_latest_v2_version() {
    local include_pre="$1"
    local api_url="https://api.github.com/repos/${GITHUB_REPO}/releases"
    
    # If including prereleases, fetch all releases, otherwise just non-prereleases
    local releases_json=$(curl -sSL "$api_url")
    
    if [ -z "$releases_json" ]; then
        print_error "Failed to fetch releases from GitHub"
        exit 1
    fi
    
    # Extract all v2.x.x tags based on prerelease preference
    local v2_versions
    if [ "$include_pre" = "1" ]; then
        # Include prereleases
        v2_versions=$(echo "$releases_json" | \
            grep -B1 '"tag_name"' | \
            grep '"tag_name"' | \
            sed -E 's/.*"tag_name"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/' | \
            grep -E '^v2\.')
    else
        # Exclude prereleases - look for releases where prerelease is false
        v2_versions=$(echo "$releases_json" | \
            python3 -c "
import sys, json
try:
    releases = json.load(sys.stdin)
    for r in releases:
        if not r.get('prerelease', True) and r.get('tag_name', '').startswith('v2.'):
            print(r['tag_name'])
except:
    pass
" 2>/dev/null)
        
        # Fallback to jq if python3 is not available
        if [ -z "$v2_versions" ] && command -v jq &> /dev/null; then
            v2_versions=$(echo "$releases_json" | \
                jq -r '.[] | select(.prerelease == false) | select(.tag_name | startswith("v2.")) | .tag_name')
        fi
        
        # Final fallback - parse JSON manually (less reliable)
        if [ -z "$v2_versions" ]; then
            # This is a crude parser that looks for patterns
            v2_versions=$(echo "$releases_json" | \
                awk '/"tag_name".*v2\./ {
                    tag = $0
                    gsub(/.*"tag_name"[[:space:]]*:[[:space:]]*"/, "", tag)
                    gsub(/".*/, "", tag)
                    current_tag = tag
                }
                /"prerelease"[[:space:]]*:[[:space:]]*false/ {
                    if (current_tag && substr(current_tag, 1, 2) == "v2") {
                        print current_tag
                        current_tag = ""
                    }
                }')
        fi
    fi
    
    if [ -z "$v2_versions" ]; then
        print_error "No v2.x.x releases found"
        if [ "$include_pre" = "0" ]; then
            print_info "Try with --prerelease flag to include pre-release versions"
        fi
        exit 1
    fi
    
    # Sort versions and get the latest
    # Convert versions to sortable format and find max
    local latest_v2=""
    local max_version="000000000"
    
    while IFS= read -r version; do
        # Remove 'v' prefix and any prerelease suffix for comparison
        local clean_version="${version#v}"
        local base_version="${clean_version%%-*}"
        
        # Convert to sortable format (e.g., 2.1.0 -> 002001000)
        local sortable=$(echo "$base_version" | awk -F. '{printf "%03d%03d%03d", $1, $2, $3}' 2>/dev/null)
        
        if [ "$sortable" -gt "$max_version" ]; then
            max_version="$sortable"
            latest_v2="$version"
        fi
    done <<< "$v2_versions"
    
    if [ -z "$latest_v2" ]; then
        print_error "Could not determine latest v2 version"
        exit 1
    fi
    
    echo "$latest_v2"
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

# Check if version is v2.x.x
is_v2_version() {
    local version="$1"
    # Remove 'v' prefix if present and check if it starts with 2.
    version=${version#v}
    if [[ "$version" =~ ^2\. ]]; then
        echo "1"
    else
        echo "0"
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
    local binary_name="$3"  # either "hyprlax" or "hyprlax-ctl"
    local temp_file="/tmp/${binary_name}-download"
    
    # Construct download URL
    local download_url="https://github.com/${GITHUB_REPO}/releases/download/${version}/${binary_name}-${arch}"
    
    print_step "Downloading ${binary_name} ${version} for ${arch}..." >&2
    
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

# Install both binaries (or just hyprlax for v2)
install_binaries() {
    local hyprlax_file="$1"
    local ctl_file="$2"
    local is_v2="$3"
    local hyprlax_path=$(get_hyprlax_path)
    local ctl_path=$(get_ctl_path)
    local install_dir=$(dirname "$hyprlax_path")
    
    # Always install hyprlax
    install_single_binary "$hyprlax_file" "$hyprlax_path" "hyprlax"
    
    # Only install hyprlax-ctl for v1.x versions
    if [ "$is_v2" = "0" ]; then
        install_single_binary "$ctl_file" "$ctl_path" "hyprlax-ctl"
        print_success "Both binaries installed successfully"
    else
        # For v2, remove old hyprlax-ctl if it exists
        if [ -f "$ctl_path" ]; then
            print_step "Removing obsolete hyprlax-ctl (integrated in v2)..."
            if [ "$INSTALL_TYPE" = "system" ]; then
                sudo rm -f "$ctl_path"
            else
                rm -f "$ctl_path"
            fi
        fi
        print_success "hyprlax v2 installed successfully (ctl functionality integrated)"
    fi
    
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
    if [ "$VERSION_2" = "1" ]; then
        # Get latest v2.x.x version
        VERSION=$(get_latest_v2_version "$INCLUDE_PRERELEASES")
        if [ "$INCLUDE_PRERELEASES" = "1" ]; then
            print_info "Latest v2.x.x version (including prereleases): $VERSION"
        else
            print_info "Latest stable v2.x.x version: $VERSION"
        fi
    elif [ "$VERSION" = "latest" ]; then
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
            # Downgrade warning
            echo
            echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
            echo -e "${RED}⚠️  DOWNGRADE WARNING ⚠️${NC}"
            echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
            echo
            echo -e "${YELLOW}Current version:${NC} ${GREEN}$INSTALLED_VERSION${NC}"
            echo -e "${YELLOW}Target version:${NC}  ${RED}$VERSION_NUM${NC} ${RED}(OLDER)${NC}"
            echo
            echo -e "${RED}You are attempting to DOWNGRADE hyprlax!${NC}"
            echo -e "${YELLOW}This may cause compatibility issues or data loss.${NC}"
            echo
            
            if [ "$ALLOW_DOWNGRADE" = "0" ] && [ "$FORCE_INSTALL" = "0" ]; then
                # Ask for confirmation
                echo -e "${CYAN}Do you want to proceed with the downgrade?${NC}"
                echo -e "${CYAN}Type ${YELLOW}yes${CYAN} to continue or ${YELLOW}no${CYAN} to cancel:${NC} "
                
                # Use /dev/tty for input when piped through bash
                if [ -t 0 ]; then
                    read -r RESPONSE
                else
                    read -r RESPONSE < /dev/tty
                fi
                
                if [ "$RESPONSE" != "yes" ]; then
                    print_info "Downgrade cancelled"
                    exit 0
                fi
                echo
                print_warning "Proceeding with downgrade..."
            elif [ "$ALLOW_DOWNGRADE" = "1" ]; then
                print_warning "Downgrade allowed via --downgrade flag"
            elif [ "$FORCE_INSTALL" = "1" ]; then
                print_warning "Downgrade forced via --force flag"
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
    
    # Check if this is a v2 version
    IS_V2=$(is_v2_version "$VERSION")
    
    # Check for multiple installations
    local all_installs=$(find_all_installations)
    local target_path=$(get_hyprlax_path)
    
    if [ -n "$all_installs" ]; then
        # Count the number of installations
        local install_count=$(echo "$all_installs" | tr '|' '\n' | wc -l)
        
        # Check if there would be a conflict
        local active_after=$(get_active_binary_after_install "$target_path")
        
        # If there are multiple installations or a conflict would occur, prompt the user
        if [ "$install_count" -gt 1 ] || [ "$active_after" != "$target_path" ]; then
            local user_choice=$(prompt_installation_choice "$VERSION_NUM" "$target_path" "$all_installs")
            
            case "$user_choice" in
                1)
                    print_info "Proceeding with installation to $target_path"
                    print_warning "Remember: $active_after will be the active binary!"
                    ;;
                2)
                    remove_all_installations "$all_installs"
                    print_success "All existing installations removed"
                    ;;
                3)
                    print_info "Installation cancelled"
                    exit 0
                    ;;
                *)
                    print_error "Invalid choice. Installation cancelled"
                    exit 1
                    ;;
            esac
        fi
    fi
    
    # Download binaries
    HYPRLAX_FILE=$(download_binary "$VERSION" "$ARCH" "hyprlax")
    
    # Only download hyprlax-ctl for v1.x versions
    if [ "$IS_V2" = "0" ]; then
        CTL_FILE=$(download_binary "$VERSION" "$ARCH" "hyprlax-ctl")
    else
        CTL_FILE=""
        print_info "Skipping hyprlax-ctl download (integrated in v2)"
    fi
    
    # Backup existing installation
    if [ "$INSTALLED_VERSION" != "none" ]; then
        local hyprlax_path=$(get_hyprlax_path)
        local ctl_path=$(get_ctl_path)
        
        if [ -f "$hyprlax_path" ]; then
            local backup_path="${hyprlax_path}.backup.$(date +%Y%m%d_%H%M%S)"
            print_step "Backing up existing hyprlax binary..."
            if [ "$INSTALL_TYPE" = "system" ]; then
                sudo cp "$hyprlax_path" "$backup_path"
            else
                cp "$hyprlax_path" "$backup_path"
            fi
        fi
        
        # Only backup hyprlax-ctl for v1 -> v1 upgrades
        if [ "$IS_V2" = "0" ] && [ -f "$ctl_path" ]; then
            local backup_path="${ctl_path}.backup.$(date +%Y%m%d_%H%M%S)"
            print_step "Backing up existing hyprlax-ctl binary..."
            if [ "$INSTALL_TYPE" = "system" ]; then
                sudo cp "$ctl_path" "$backup_path"
            else
                cp "$ctl_path" "$backup_path"
            fi
        fi
    fi
    
    # Install binaries (both for v1, just hyprlax for v2)
    install_binaries "$HYPRLAX_FILE" "$CTL_FILE" "$IS_V2"
    
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
        if [ "$IS_V2" = "1" ]; then
            print_info "hyprlax v2 $VERSION_NUM has been installed"
            echo
            print_info "Note: hyprlax-ctl functionality is now integrated into the main binary"
        else
            print_info "hyprlax and hyprlax-ctl $VERSION_NUM have been installed"
        fi
        echo
        print_info "To get started:"
        print_step "1. Add to your Hyprland config:"
        echo "      exec-once = hyprlax /path/to/wallpaper.jpg"
        print_step "2. Reload Hyprland or logout/login"
    else
        if [ "$IS_V2" = "1" ]; then
            print_success "hyprlax has been updated to v2 $VERSION_NUM"
            print_info "Note: hyprlax-ctl functionality is now integrated into the main binary"
        else
            print_success "hyprlax and hyprlax-ctl have been updated to $VERSION_NUM"
        fi
    fi
    
    echo
    print_info "For more information:"
    print_step "GitHub: https://github.com/${GITHUB_REPO}"
    if [ "$IS_V2" = "1" ]; then
        print_step "Usage: hyprlax --help"
    else
        print_step "Usage: hyprlax --help, hyprlax-ctl --help"
    fi
}

# Run main function
main