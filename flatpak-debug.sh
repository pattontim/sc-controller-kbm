#!/bin/bash
# flatpak-debug.sh - SC-Daemon Flatpak Build Debugging Tool
# Provides comprehensive diagnostics for build troubleshooting

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Build directory
BUILD_DIR="${1:-.var/app/build/scc-daemon}"
FLATPAK_DIR="$HOME/$BUILD_DIR"

print_header() {
    echo -e "${BLUE}=== $1 ===${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# Check system dependencies
check_system_deps() {
    print_header "System Dependencies"
    
    local deps=("flatpak" "flatpak-builder" "meson" "ninja" "pkg-config" "git")
    
    for dep in "${deps[@]}"; do
        if command -v "$dep" &> /dev/null; then
            local version=$($dep --version 2>&1 | head -1)
            print_success "$dep: $version"
        else
            print_error "$dep: NOT FOUND"
        fi
    done
}

# Check Flatpak SDKs
check_flatpak_sdks() {
    print_header "Flatpak SDKs/Runtimes"
    
    local required_runtime="org.freedesktop.Platform/x86_64/23.08"
    local required_sdk="org.freedesktop.Sdk/x86_64/23.08"
    
    if flatpak list --app 2>/dev/null | grep -q "$required_runtime"; then
        print_success "Runtime: org.freedesktop.Platform 23.08 installed"
    else
        print_error "Runtime: org.freedesktop.Platform 23.08 NOT INSTALLED"
        echo "    Install with: flatpak install flathub org.freedesktop.Platform//23.08"
    fi
    
    if flatpak list --app 2>/dev/null | grep -q "$required_sdk"; then
        print_success "SDK: org.freedesktop.Sdk 23.08 installed"
    else
        print_error "SDK: org.freedesktop.Sdk 23.08 NOT INSTALLED"
        echo "    Install with: flatpak install flathub org.freedesktop.Sdk//23.08"
    fi
}

# Check manifest
check_manifest() {
    print_header "Manifest File"
    
    if [ -f "com.github.kozec.scc-daemon.yml" ]; then
        print_success "Manifest found: com.github.kozec.scc-daemon.yml"
        echo "    Size: $(stat -f%z com.github.kozec.scc-daemon.yml 2>/dev/null || stat -c%s com.github.kozec.scc-daemon.yml)"
        
        # Check for common sections
        local sections=("app-id" "runtime" "sdk" "modules" "finish-args")
        for section in "${sections[@]}"; do
            if grep -q "^$section:" com.github.kozec.scc-daemon.yml; then
                print_success "  Section '$section' found"
            else
                print_warning "  Section '$section' NOT FOUND"
            fi
        done
    else
        print_error "Manifest not found: com.github.kozec.scc-daemon.yml"
    fi
}

# Validate YAML syntax
validate_yaml() {
    print_header "YAML Validation"
    
    if command -v python3 &> /dev/null; then
        if python3 -c "import yaml; yaml.safe_load(open('com.github.kozec.scc-daemon.yml'))" 2>/dev/null; then
            print_success "YAML syntax is valid"
        else
            print_error "YAML syntax error in manifest"
            python3 -c "import yaml; yaml.safe_load(open('com.github.kozec.scc-daemon.yml'))" 2>&1 || true
        fi
    else
        print_warning "python3 not found, skipping YAML validation"
    fi
}

# Check build files
check_build_files() {
    print_header "Build Files"
    
    local build_files=("meson.build" "src/daemon/meson.build" "src/daemon/drivers/meson.build" "flatpak-wrapper.sh" "scc-daemon.service")
    
    for file in "${build_files[@]}"; do
        if [ -f "$file" ]; then
            print_success "$file exists"
        else
            print_error "$file NOT FOUND"
        fi
    done
}

# Analyze build configuration
analyze_build_config() {
    print_header "Build Configuration Analysis"
    
    echo "Checking meson.build for install directives..."
    local install_count=$(grep -c "install: true" meson.build || echo 0)
    print_info "Found $install_count 'install: true' directives in main meson.build"
    
    if [ "$install_count" -ge 7 ]; then
        print_success "Core libraries configured for installation"
    else
        print_warning "Expected >= 7 install directives, found $install_count"
    fi
    
    echo ""
    echo "Checking daemon meson.build..."
    if grep -q "install: true" src/daemon/meson.build; then
        print_success "scc-daemon executable configured for installation"
    else
        print_warning "scc-daemon not marked for installation"
    fi
    
    echo ""
    echo "Checking driver meson.build..."
    local driver_count=$(grep -c "install: true" src/daemon/drivers/meson.build || echo 0)
    print_info "Found $driver_count install directives for drivers"
    
    if [ "$driver_count" -ge 9 ]; then
        print_success "All drivers configured for installation"
    else
        print_warning "Expected >= 9 install directives for drivers, found $driver_count"
    fi
}

# Check for missing headers (simulate in build container)
check_headers() {
    print_header "Critical Header Files (reference check)"
    
    print_info "These will be checked during Flatpak build:"
    
    local headers=(
        "libusb-1.0/libusb.h:libusb"
        "libudev.h:libudev"
        "zlib.h:zlib"
        "X11/Xlib.h:X11"
        "libevdev/libevdev.h:libevdev"
    )
    
    for header_info in "${headers[@]}"; do
        IFS=':' read -r header lib <<< "$header_info"
        if [ -f "/usr/include/$header" ] 2>/dev/null || [ -f "/usr/local/include/$header" ] 2>/dev/null; then
            print_success "$lib ($header) - found locally"
        else
            print_warning "$lib ($header) - will be provided by Flatpak SDK"
        fi
    done
}

# Show current build status
show_build_status() {
    print_header "Build Directory Status"
    
    if [ -d "$FLATPAK_DIR" ]; then
        local size=$(du -sh "$FLATPAK_DIR" 2>/dev/null | cut -f1)
        print_info "Build directory: $FLATPAK_DIR"
        print_info "Size: $size"
        
        if [ -d "$FLATPAK_DIR/build" ]; then
            print_success "Build subdirectory exists"
            
            if [ -f "$FLATPAK_DIR/build/build.ninja" ]; then
                print_success "Meson build files present"
            fi
        else
            print_warning "Build subdirectory not found (may not have been built yet)"
        fi
    else
        print_warning "Build directory does not exist yet"
        echo "    Will be created at: $FLATPAK_DIR"
    fi
}

# Generate build cache log
generate_cache_info() {
    print_header "Build Cache Information"
    
    if [ -d "$FLATPAK_DIR" ]; then
        local cache_dir="$FLATPAK_DIR/.flatpak-builder"
        if [ -d "$cache_dir" ]; then
            print_info "Cache directory: $cache_dir"
            local cache_size=$(du -sh "$cache_dir" 2>/dev/null | cut -f1)
            print_info "Cache size: $cache_size"
        fi
    else
        print_info "No build cache yet"
    fi
}

# Dry-run information
show_dry_run_command() {
    print_header "Recommended Commands"
    
    echo "1. Check manifest validity (dry run):"
    echo "   ${BLUE}flatpak-builder --user --dry-run ~/.var/app/build/scc-daemon-test com.github.kozec.scc-daemon.yml${NC}"
    echo ""
    echo "2. Full build (with logging):"
    echo "   ${BLUE}flatpak-builder --user --install --force-clean --verbose \\${NC}"
    echo "   ${BLUE}  ~/.var/app/build/scc-daemon com.github.kozec.scc-daemon.yml 2>&1 | tee build.log${NC}"
    echo ""
    echo "3. Debug build (with shell access on failure):"
    echo "   ${BLUE}flatpak-builder --user --install --force-clean --keep-build-dir \\${NC}"
    echo "   ${BLUE}  ~/.var/app/build/scc-daemon com.github.kozec.scc-daemon.yml${NC}"
    echo ""
    echo "4. Extract logs from failed build:"
    echo "   ${BLUE}find ~/.var/app/build/scc-daemon -name '*.log' -type f${NC}"
}

# Show help
show_help() {
    cat << 'EOF'
Usage: flatpak-debug.sh [build_dir] [options]

Options:
    --help              Show this help message
    --full              Run all diagnostics
    --deps              Check system dependencies only
    --sdks              Check Flatpak SDKs only
    --manifest          Validate manifest file
    --config            Analyze build configuration
    --headers           Check for required headers
    --status            Show build directory status
    --clean             Remove build directory and caches
    --logs              Show recent build logs

Examples:
    flatpak-debug.sh                           # Quick diagnostics
    flatpak-debug.sh --full                    # Complete diagnostics
    flatpak-debug.sh --logs                    # Show build logs
    flatpak-debug.sh .var/app/build/scc-debug # Use custom build dir

EOF
}

# Clean build and caches
clean_build() {
    print_header "Cleaning Build"
    
    if [ -d "$FLATPAK_DIR" ]; then
        print_warning "Removing: $FLATPAK_DIR"
        rm -rf "$FLATPAK_DIR"
        print_success "Build directory removed"
    else
        print_info "Build directory doesn't exist, nothing to clean"
    fi
}

# Show recent logs
show_logs() {
    print_header "Recent Build Logs"
    
    if [ -d "$FLATPAK_DIR/logs" ]; then
        find "$FLATPAK_DIR/logs" -name "*.log" -type f -mtime -1 | while read -r logfile; do
            echo ""
            print_info "File: $logfile"
            tail -20 "$logfile"
        done
    else
        print_warning "No build logs found yet"
    fi
}

# Main execution
main() {
    if [ "$#" -eq 0 ]; then
        # Default: quick diagnostics
        check_system_deps
        echo ""
        check_flatpak_sdks
        echo ""
        check_manifest
        echo ""
        check_build_files
        echo ""
        analyze_build_config
        echo ""
        show_build_status
        echo ""
        show_dry_run_command
    else
        case "$1" in
            --help)
                show_help
                ;;
            --full)
                check_system_deps
                echo ""
                check_flatpak_sdks
                echo ""
                check_manifest
                echo ""
                validate_yaml
                echo ""
                check_build_files
                echo ""
                analyze_build_config
                echo ""
                check_headers
                echo ""
                show_build_status
                echo ""
                generate_cache_info
                echo ""
                show_dry_run_command
                ;;
            --deps)
                check_system_deps
                ;;
            --sdks)
                check_flatpak_sdks
                ;;
            --manifest)
                check_manifest
                validate_yaml
                ;;
            --config)
                analyze_build_config
                ;;
            --headers)
                check_headers
                ;;
            --status)
                show_build_status
                generate_cache_info
                ;;
            --clean)
                clean_build
                ;;
            --logs)
                show_logs
                ;;
            *)
                echo "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    fi
}

# Run main function
main "$@"
