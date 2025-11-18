#!/usr/bin/env bash
set -euo pipefail

# build.sh - Build u-boot for tachyon device
# This script can build either in Docker or natively on the host

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
TMP_DIR="${PROJECT_DIR}/.tmp"

# Default values
USE_DOCKER=""  # Will be auto-detected
DEVICE_CONFIG="qcm6490_tachyon_defconfig"
BUILD_TARGET="u-boot-dtb.bin"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
AUTO_MODE=true

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

error() {
    echo -e "${RED}ERROR: $*${NC}" >&2
    exit 1
}

info() {
    echo -e "${GREEN}INFO: $*${NC}"
}

warn() {
    echo -e "${YELLOW}WARN: $*${NC}"
}

section() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$*${NC}"
    echo -e "${BLUE}========================================${NC}"
}

is_tachyon_device() {
    # Check if running on a tachyon device
    if [ -f "/proc/device-tree/model" ]; then
        local model=$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0' || echo "")
        if [[ "$model" =~ [Tt]achyon ]]; then
            return 0
        fi
    fi
    return 1
}

detect_build_mode() {
    if [ -n "$USE_DOCKER" ]; then
        # User explicitly set the mode
        return
    fi

    if is_tachyon_device; then
        info "Detected tachyon device - using native build mode"
        USE_DOCKER=false
    else
        info "Not running on tachyon device - using Docker build mode"
        if ! command -v docker &> /dev/null; then
            warn "Docker not found, falling back to native build"
            warn "Install Docker or use --native flag"
            USE_DOCKER=false
        else
            USE_DOCKER=true
        fi
    fi
}

show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Build u-boot for tachyon device, either in Docker or natively.

By default, the script auto-detects:
  - On tachyon device: builds natively and auto-installs dependencies
  - On host machine: uses Docker container

Options:
    -h, --help              Show this help message
    -d, --docker            Force Docker build (override auto-detection)
    -n, --native            Force native build (override auto-detection)
    -c, --config CONFIG     Device config to use (default: qcm6490_tachyon_defconfig)
    -j, --jobs JOBS         Number of parallel jobs (default: auto-detect)
    -t, --target TARGET     Build target (default: u-boot-dtb.bin)
    --list-configs          List available device configurations
    --clean                 Clean build artifacts before building
    --install-deps          Install native build dependencies and exit

Device Configs:
    qcm6490_tachyon_defconfig    Tachyon device (default)
    qcm6490_defconfig            Generic QCM6490

Examples:
    $0                          # Auto-detect and build
    $0 --docker                 # Force Docker build
    $0 --native                 # Force native build
    $0 --config qcm6490_defconfig --docker
    $0 --native --jobs 8        # Native build with 8 jobs
    $0 --list-configs           # Show available configs
    $0 --install-deps           # Install dependencies only

Environment Variables:
    CROSS_COMPILE              Cross-compiler prefix (e.g., aarch64-linux-gnu-)
    ARCH                       Target architecture (default: arm64)

EOF
    exit 0
}

list_configs() {
    section "AVAILABLE DEVICE CONFIGURATIONS"

    if [ -d "${PROJECT_DIR}/configs" ]; then
        echo "Device configurations in configs/:"
        echo ""
        ls -1 "${PROJECT_DIR}/configs"/*defconfig 2>/dev/null | while read -r config; do
            basename "$config"
        done
    else
        error "configs/ directory not found"
    fi

    exit 0
}

check_native_prerequisites() {
    section "CHECKING NATIVE BUILD PREREQUISITES"

    local missing_tools=()
    local missing_packages=()

    # Essential build tools
    for tool in make gcc python3 bison flex bc xxd; do
        if ! command -v "$tool" &> /dev/null; then
            missing_tools+=("$tool")
        fi
    done

    if [ ${#missing_tools[@]} -gt 0 ]; then
        error "Missing required tools: ${missing_tools[*]}. Run: $0 --install-deps"
    fi

    info "✓ Essential build tools found"

    # Check for required header files by trying to compile a test
    local temp_test_file="/tmp/uboot_prereq_test_$$.c"

    # Test for OpenSSL headers
    cat > "$temp_test_file" << 'EOF'
#include <openssl/evp.h>
#include <openssl/ssl.h>
int main() { return 0; }
EOF

    if ! gcc -o /dev/null "$temp_test_file" 2>/dev/null; then
        missing_packages+=("libssl-dev")
    fi
    rm -f "$temp_test_file"

    # Test for gnutls headers
    cat > "$temp_test_file" << 'EOF'
#include <gnutls/gnutls.h>
int main() { return 0; }
EOF

    if ! gcc -o /dev/null "$temp_test_file" 2>/dev/null; then
        missing_packages+=("libgnutls28-dev")
    fi
    rm -f "$temp_test_file"

    # Check for device tree compiler
    if ! command -v dtc &> /dev/null; then
        missing_packages+=("device-tree-compiler")
    fi

    # Check for python packages
    if ! python3 -c "import elftools" 2>/dev/null; then
        missing_packages+=("python3-pyelftools")
    fi

    if [ ${#missing_packages[@]} -gt 0 ]; then
        echo ""
        echo -e "${RED}ERROR: Missing required packages: ${missing_packages[*]}${NC}" >&2
        echo ""
        echo "To install missing dependencies, run ONE of:"
        echo ""
        echo "  1. Auto-install with this script:"
        echo "     sudo $0 --install-deps"
        echo ""
        echo "  2. Manual install (Ubuntu/Debian):"
        echo "     sudo apt-get install ${missing_packages[*]}"
        echo ""
        exit 1
    fi

    info "✓ Required development libraries found"

    # Check for cross-compiler if CROSS_COMPILE is set
    if [ -n "${CROSS_COMPILE:-}" ]; then
        if ! command -v "${CROSS_COMPILE}gcc" &> /dev/null; then
            error "Cross-compiler not found: ${CROSS_COMPILE}gcc"
        fi
        info "✓ Cross-compiler found: ${CROSS_COMPILE}gcc"
    else
        warn "CROSS_COMPILE not set - attempting native build"
        warn "For ARM64 target, you may need to set CROSS_COMPILE=aarch64-linux-gnu-"
    fi

    info "✓ Prerequisites check complete"
}

install_native_prerequisites() {
    section "INSTALLING NATIVE BUILD PREREQUISITES"

    # Detect OS
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
    elif command -v uname &> /dev/null; then
        OS=$(uname -s | tr '[:upper:]' '[:lower:]')
    else
        error "Unable to detect operating system"
    fi

    info "Detected OS: $OS"

    case "$OS" in
        ubuntu|debian)
            info "Installing build dependencies for Debian/Ubuntu..."
            info "This matches the dependencies from the CircleCI build environment"
            sudo apt-get update
            sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
                gcc \
                gcc-aarch64-linux-gnu \
                bc \
                bison \
                build-essential \
                coccinelle \
                device-tree-compiler \
                dfu-util \
                efitools \
                flex \
                gdisk \
                graphviz \
                imagemagick \
                libgnutls28-dev \
                libguestfs-tools \
                libncurses-dev \
                libpython3-dev \
                libsdl2-dev \
                libssl-dev \
                lz4 \
                lzma \
                lzma-alone \
                openssl \
                pkg-config \
                python3 \
                python3-asteval \
                python3-coverage \
                python3-filelock \
                python3-pkg-resources \
                python3-pycryptodome \
                python3-pyelftools \
                python3-pytest \
                python3-pytest-xdist \
                python3-sphinxcontrib.apidoc \
                python3-sphinx-rtd-theme \
                python3-subunit \
                python3-testtools \
                python3-venv \
                swig \
                uuid-dev \
                xxd \
                zip
            info "✓ Dependencies installed"
            ;;

        fedora|rhel|centos)
            info "Installing build dependencies for Fedora/RHEL/CentOS..."
            sudo dnf install -y \
                gcc \
                gcc-c++ \
                make \
                bison \
                flex \
                openssl-devel \
                dtc \
                python3 \
                python3-devel \
                python3-setuptools \
                python3-pyelftools \
                bc \
                gcc-aarch64-linux-gnu \
                gcc-arm-linux-gnu
            info "✓ Dependencies installed"
            ;;

        arch|manjaro)
            info "Installing build dependencies for Arch/Manjaro..."
            sudo pacman -S --needed --noconfirm \
                base-devel \
                bison \
                flex \
                openssl \
                dtc \
                python \
                python-setuptools \
                python-pyelftools \
                bc \
                aarch64-linux-gnu-gcc \
                arm-none-eabi-gcc
            info "✓ Dependencies installed"
            ;;

        darwin)
            info "Installing build dependencies for macOS..."
            if ! command -v brew &> /dev/null; then
                error "Homebrew not found. Please install from https://brew.sh/"
            fi
            brew install \
                gnu-sed \
                bison \
                flex \
                openssl \
                dtc \
                python3
            warn "Cross-compiler for ARM64 not available via Homebrew"
            warn "Consider using Docker build: $0 --docker"
            ;;

        *)
            error "Unsupported OS: $OS. Please install dependencies manually or use Docker build."
            ;;
    esac
}

build_docker() {
    section "BUILDING IN DOCKER CONTAINER"

    local docker_image="uboot-tachyon:latest"
    local dockerfile="${PROJECT_DIR}/Dockerfile.tachyon"

    # Check if Docker is available
    if ! command -v docker &> /dev/null; then
        error "Docker not found. Please install Docker or use --native build."
    fi

    info "Docker image: $docker_image"

    # Build Docker image if it doesn't exist
    if ! docker image inspect "$docker_image" &> /dev/null; then
        if [ -f "$dockerfile" ]; then
            info "Building Docker image from $dockerfile..."
            info "This will take a minute on first run..."

            docker build -t "$docker_image" -f "$dockerfile" "${PROJECT_DIR}/"

            info "✓ Docker image built successfully"
        else
            error "Dockerfile not found at: $dockerfile"
        fi
    else
        info "✓ Using existing Docker image"
    fi

    # Ensure .tmp exists
    mkdir -p "${TMP_DIR}"

    info "Running build in Docker container..."
    info "Config: $DEVICE_CONFIG"
    info "Target: $BUILD_TARGET"
    info "Jobs: $JOBS"

    docker run --rm \
        -v "${PROJECT_DIR}:/project" \
        -v "${TMP_DIR}:/tmp/work" \
        -w /project \
        "$docker_image" \
        bash -c "export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- && make ${DEVICE_CONFIG} && make -j${JOBS} ${BUILD_TARGET}"

    info "✓ Docker build complete"
}

build_native() {
    section "BUILDING NATIVELY"

    # On tachyon device, install dependencies if needed
    if is_tachyon_device; then
        info "Building on tachyon device"

        # Check if essential tools are installed
        local missing_tools=()
        for tool in make gcc python3 bison flex; do
            if ! command -v "$tool" &> /dev/null; then
                missing_tools+=("$tool")
            fi
        done

        if [ ${#missing_tools[@]} -gt 0 ]; then
            warn "Missing build tools: ${missing_tools[*]}"
            info "Installing build dependencies..."
            install_native_prerequisites
        fi
    fi

    check_native_prerequisites

    info "Config: $DEVICE_CONFIG"
    info "Target: $BUILD_TARGET"
    info "Jobs: $JOBS"

    # Set default ARCH if not set
    export ARCH=${ARCH:-arm64}

    # Auto-detect and set CROSS_COMPILE if needed
    if [ -z "${CROSS_COMPILE:-}" ]; then
        local host_arch=$(uname -m)

        # If we're building arm64 but not on arm64, we need cross-compile
        if [ "$ARCH" = "arm64" ] && [ "$host_arch" != "aarch64" ] && [ "$host_arch" != "arm64" ]; then
            if command -v aarch64-linux-gnu-gcc &> /dev/null; then
                export CROSS_COMPILE=aarch64-linux-gnu-
                info "Auto-detected cross-compiler: $CROSS_COMPILE"
            else
                error "Cross-compiler needed but not found. Please install gcc-aarch64-linux-gnu or set CROSS_COMPILE"
            fi
        fi
    fi

    info "Architecture: $ARCH"
    if [ -n "${CROSS_COMPILE:-}" ]; then
        info "Cross-compiler: $CROSS_COMPILE"
    else
        info "Native compilation (no cross-compiler)"
    fi

    # Change to project directory
    cd "$PROJECT_DIR"

    # Configure
    info "Configuring u-boot..."
    make "$DEVICE_CONFIG"

    # Build
    info "Building u-boot..."
    make -j"${JOBS}" "$BUILD_TARGET"

    info "✓ Native build complete"
}

show_build_output() {
    section "BUILD OUTPUT"

    if [ -f "${PROJECT_DIR}/u-boot-dtb.bin" ]; then
        local size=$(stat -c%s "${PROJECT_DIR}/u-boot-dtb.bin" 2>/dev/null || stat -f%z "${PROJECT_DIR}/u-boot-dtb.bin" 2>/dev/null)
        info "✓ u-boot-dtb.bin created (${size} bytes)"
        info "  Location: ${PROJECT_DIR}/u-boot-dtb.bin"
    else
        warn "u-boot-dtb.bin not found"
    fi

    if [ -f "${PROJECT_DIR}/u-boot.bin" ]; then
        local size=$(stat -c%s "${PROJECT_DIR}/u-boot.bin" 2>/dev/null || stat -f%z "${PROJECT_DIR}/u-boot.bin" 2>/dev/null)
        info "✓ u-boot.bin created (${size} bytes)"
        info "  Location: ${PROJECT_DIR}/u-boot.bin"
    fi

    echo ""
    info "To install on device, copy u-boot.bin to the project directory and run:"
    info "  sudo ./install.sh install"
}

clean_build() {
    section "CLEANING BUILD ARTIFACTS"

    info "Running make distclean..."
    make distclean || make mrproper || true

    info "✓ Clean complete"
}

# Parse command line arguments
DO_CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            ;;
        -d|--docker)
            USE_DOCKER=true
            AUTO_MODE=false
            shift
            ;;
        -n|--native)
            USE_DOCKER=false
            AUTO_MODE=false
            shift
            ;;
        -c|--config)
            DEVICE_CONFIG="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -t|--target)
            BUILD_TARGET="$2"
            shift 2
            ;;
        --list-configs)
            list_configs
            ;;
        --clean)
            DO_CLEAN=true
            shift
            ;;
        --install-deps)
            install_native_prerequisites
            exit 0
            ;;
        *)
            error "Unknown option: $1. Use --help for usage."
            ;;
    esac
done

# Main execution
section "U-BOOT BUILD FOR TACHYON"

# Auto-detect build mode if not explicitly set
if [ "$AUTO_MODE" = true ]; then
    detect_build_mode
fi

info "Build mode: $([ "$USE_DOCKER" = true ] && echo "Docker" || echo "Native")"
info "Device config: $DEVICE_CONFIG"

# Verify config exists
if [ ! -f "${PROJECT_DIR}/configs/${DEVICE_CONFIG}" ]; then
    error "Config not found: ${DEVICE_CONFIG}. Use --list-configs to see available configs."
fi

# Clean if requested
if [ "$DO_CLEAN" = true ]; then
    clean_build
fi

# Build
if [ "$USE_DOCKER" = true ]; then
    build_docker
else
    build_native
fi

# Show output
show_build_output

section "✓ BUILD COMPLETE"
