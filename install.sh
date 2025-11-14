#!/bin/bash
set -euo pipefail

# install.sh - Install pre-built u-boot.bin on tachyon device
# This script reads xbl partitions, patches them with u-boot, signs them, and writes them back

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UBOOT_BIN="${SCRIPT_DIR}/u-boot.bin"
QTOOLS_DIR="${QTOOLS_DIR:-/tmp/qtoolsign}"
WORK_DIR="/tmp/tachyon-uboot-install-$$"

# Parse command
COMMAND="${1:-help}"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
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
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$*${NC}"
    echo -e "${GREEN}========================================${NC}"
}

cleanup() {
    if [ -d "$WORK_DIR" ]; then
        rm -rf "$WORK_DIR"
    fi
}

show_help() {
    cat << EOF
Usage: sudo $0 <command>

Commands:
    help            Show this help message
    check           Verify the system is a tachyon device and check prerequisites
    mount           Mount xbl partitions (if they have a filesystem)
    unmount         Unmount xbl partitions
    install         Install u-boot.bin to xbl partitions (default action)

Environment Variables:
    QTOOLS_DIR      Path to qtestsign tools (default: /tmp/qtoolsign)

Examples:
    sudo $0 check          # Check if system is ready for installation
    sudo $0 unmount        # Unmount xbl partitions if mounted
    sudo $0 install        # Install u-boot.bin to device

EOF
    exit 0
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        error "This script must be run as root (use sudo)"
    fi
}

find_xbl_partitions() {
    XBL_A_PART="/dev/disk/by-partlabel/xbl_a"
    XBL_B_PART="/dev/disk/by-partlabel/xbl_b"

    if [ ! -b "$XBL_A_PART" ]; then
        error "xbl_a partition not found at: $XBL_A_PART"
    fi

    if [ ! -b "$XBL_B_PART" ]; then
        error "xbl_b partition not found at: $XBL_B_PART"
    fi

    XBL_A_REAL=$(readlink -f "$XBL_A_PART")
    XBL_B_REAL=$(readlink -f "$XBL_B_PART")
}

verify_tachyon_device() {
    info "Checking if this is a tachyon device..."
    if [ ! -f "/proc/device-tree/model" ]; then
        error "Cannot read /proc/device-tree/model - is this a device tree system?"
    fi

    DEVICE_MODEL=$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0' || echo "")
    if [ -z "$DEVICE_MODEL" ]; then
        error "Cannot read /proc/device-tree/model"
    fi

    info "Device model: $DEVICE_MODEL"

    if [[ ! "$DEVICE_MODEL" =~ [Tt]achyon ]]; then
        error "This does not appear to be a tachyon device (model: $DEVICE_MODEL)"
    fi

    info "✓ Confirmed tachyon device"
}

check_prerequisites() {
    # Check if u-boot.bin exists (only for install command)
    if [ "$COMMAND" = "install" ]; then
        if [ ! -f "$UBOOT_BIN" ]; then
            error "u-boot.bin not found at: $UBOOT_BIN"
        fi
        info "✓ Found u-boot.bin ($(stat -c%s "$UBOOT_BIN") bytes)"
    fi

    # Check for qtestsign tools (only for install command)
    if [ "$COMMAND" = "install" ]; then
        if [ ! -f "$QTOOLS_DIR/patchxbl.py" ]; then
            error "Missing $QTOOLS_DIR/patchxbl.py - qtestsign tools not installed"
        fi

        if [ ! -f "$QTOOLS_DIR/qtestsign.py" ]; then
            error "Missing $QTOOLS_DIR/qtestsign.py - qtestsign tools not installed"
        fi

        info "✓ Found qtestsign tools at $QTOOLS_DIR"
    fi

    # Check for required commands
    local required_cmds="stat"
    if [ "$COMMAND" = "install" ]; then
        required_cmds="dd python3 stat blockdev"
    fi

    for cmd in $required_cmds; do
        if ! command -v "$cmd" &> /dev/null; then
            error "Required command not found: $cmd"
        fi
    done

    info "✓ All required commands available"
}

check_mount_status() {
    if mount | grep -q "$XBL_A_REAL"; then
        return 0  # mounted
    fi
    if mount | grep -q "$XBL_B_REAL"; then
        return 0  # mounted
    fi
    return 1  # not mounted
}

# Command: check
cmd_check() {
    section "CHECKING TACHYON DEVICE"

    verify_tachyon_device

    section "LOCATING XBL PARTITIONS"
    find_xbl_partitions

    info "✓ Found xbl_a: $XBL_A_REAL"
    info "✓ Found xbl_b: $XBL_B_REAL"

    # Get partition sizes
    XBL_A_SIZE=$(blockdev --getsize64 "$XBL_A_PART" 2>/dev/null || stat -c%s "$XBL_A_PART" 2>/dev/null || echo "unknown")
    XBL_B_SIZE=$(blockdev --getsize64 "$XBL_B_PART" 2>/dev/null || stat -c%s "$XBL_B_PART" 2>/dev/null || echo "unknown")

    info "  xbl_a size: $XBL_A_SIZE bytes"
    info "  xbl_b size: $XBL_B_SIZE bytes"

    section "CHECKING MOUNT STATUS"
    if check_mount_status; then
        warn "One or more xbl partitions are mounted:"
        mount | grep -E "($XBL_A_REAL|$XBL_B_REAL)" || true
        info "Run '$0 unmount' to unmount them"
    else
        info "✓ Partitions are not mounted"
    fi

    section "CHECKING PREREQUISITES"
    check_prerequisites

    if [ -f "$UBOOT_BIN" ]; then
        info "✓ Found u-boot.bin ($(stat -c%s "$UBOOT_BIN") bytes)"
    else
        warn "u-boot.bin not found at: $UBOOT_BIN"
        info "  (Required for 'install' command)"
    fi

    if [ -f "$QTOOLS_DIR/patchxbl.py" ] && [ -f "$QTOOLS_DIR/qtestsign.py" ]; then
        info "✓ Found qtestsign tools at $QTOOLS_DIR"
    else
        warn "qtestsign tools not found at: $QTOOLS_DIR"
        info "  (Required for 'install' command)"
    fi

    section "✓ CHECK COMPLETE"
    info "System appears to be ready for u-boot installation"
}

# Command: unmount
cmd_unmount() {
    check_root

    section "UNMOUNTING XBL PARTITIONS"

    find_xbl_partitions

    info "Checking xbl_a: $XBL_A_REAL"
    if mount | grep -q "$XBL_A_REAL"; then
        info "Unmounting xbl_a..."
        umount "$XBL_A_REAL" || error "Failed to unmount xbl_a"
        info "✓ Unmounted xbl_a"
    else
        info "  xbl_a is not mounted"
    fi

    info "Checking xbl_b: $XBL_B_REAL"
    if mount | grep -q "$XBL_B_REAL"; then
        info "Unmounting xbl_b..."
        umount "$XBL_B_REAL" || error "Failed to unmount xbl_b"
        info "✓ Unmounted xbl_b"
    else
        info "  xbl_b is not mounted"
    fi

    section "✓ UNMOUNT COMPLETE"
}

# Command: mount
cmd_mount() {
    check_root

    section "MOUNTING XBL PARTITIONS"

    find_xbl_partitions

    warn "XBL partitions are raw bootloader partitions and typically don't have filesystems"
    warn "Mounting them is unusual and likely not what you want"
    warn "This command is provided for diagnostic purposes only"

    error "Mount operation not supported for raw bootloader partitions"
}

# Command: install
cmd_install() {
    check_root
    trap cleanup EXIT

    section "1) VERIFY PREREQUISITES"

    verify_tachyon_device
    check_prerequisites

    section "2) LOCATE XBL PARTITIONS"

    find_xbl_partitions

    info "✓ Found xbl_a: $XBL_A_REAL"
    info "✓ Found xbl_b: $XBL_B_REAL"

    # Check if partitions are mounted
    if mount | grep -q "$XBL_A_REAL"; then
        error "xbl_a partition ($XBL_A_REAL) is mounted. Run '$0 unmount' first."
    fi

    if mount | grep -q "$XBL_B_REAL"; then
        error "xbl_b partition ($XBL_B_REAL) is mounted. Run '$0 unmount' first."
    fi

    info "✓ Partitions are not mounted"

    # Get partition sizes
    XBL_A_SIZE=$(blockdev --getsize64 "$XBL_A_PART")
    XBL_B_SIZE=$(blockdev --getsize64 "$XBL_B_PART")

    info "  xbl_a size: $XBL_A_SIZE bytes"
    info "  xbl_b size: $XBL_B_SIZE bytes"

    section "3) CREATE WORKING DIRECTORY"

    mkdir -p "$WORK_DIR"
    info "✓ Working directory: $WORK_DIR"

    section "4) READ XBL_A PARTITION"

    info "Reading xbl_a partition into memory..."
    dd if="$XBL_A_PART" of="$WORK_DIR/xbl_original.elf" bs=4096 status=progress

    XBL_ORIG_SIZE=$(stat -c%s "$WORK_DIR/xbl_original.elf")
    info "✓ Read xbl_a ($XBL_ORIG_SIZE bytes)"

    section "5) PATCH BOOTLOADER WITH U-BOOT"

    info "Patching xbl with u-boot.bin using patchxbl.py..."
    python3 "$QTOOLS_DIR/patchxbl.py" \
        -o "$WORK_DIR/xbl_patched.elf" \
        -c "$UBOOT_BIN" \
        "$WORK_DIR/xbl_original.elf"

    XBL_PATCHED_SIZE=$(stat -c%s "$WORK_DIR/xbl_patched.elf")
    info "✓ Patched xbl ($XBL_PATCHED_SIZE bytes)"

    section "6) SIGN PATCHED BOOTLOADER"

    info "Signing patched xbl using qtestsign.py..."
    python3 "$QTOOLS_DIR/qtestsign.py" -v6 abl \
        -o "$WORK_DIR/xbl_final.elf" \
        "$WORK_DIR/xbl_patched.elf"

    XBL_FINAL_SIZE=$(stat -c%s "$WORK_DIR/xbl_final.elf")
    info "✓ Signed xbl ($XBL_FINAL_SIZE bytes)"

    # Verify final size fits in partition
    if [ "$XBL_FINAL_SIZE" -gt "$XBL_A_SIZE" ]; then
        error "Signed xbl ($XBL_FINAL_SIZE bytes) is larger than partition ($XBL_A_SIZE bytes)"
    fi

    info "✓ Verified signed xbl fits in partition"

    section "7) WRITE TO XBL_A PARTITION"

    warn "About to write to xbl_a partition: $XBL_A_REAL"
    warn "This will modify the bootloader. Make sure you have a backup!"
    read -p "Continue? (yes/no): " confirm

    if [ "$confirm" != "yes" ]; then
        error "Installation cancelled by user"
    fi

    info "Writing signed xbl to xbl_a..."
    dd if="$WORK_DIR/xbl_final.elf" of="$XBL_A_PART" bs=4096 status=progress
    sync

    info "✓ Written to xbl_a"

    section "8) WRITE TO XBL_B PARTITION"

    info "Writing signed xbl to xbl_b (redundant copy)..."
    dd if="$WORK_DIR/xbl_final.elf" of="$XBL_B_PART" bs=4096 status=progress
    sync

    info "✓ Written to xbl_b"

    section "9) VERIFY WRITES"

    info "Verifying xbl_a..."
    dd if="$XBL_A_PART" of="$WORK_DIR/xbl_a_verify.elf" bs=4096 count=$((XBL_FINAL_SIZE / 4096 + 1)) status=none

    if ! cmp -n "$XBL_FINAL_SIZE" "$WORK_DIR/xbl_final.elf" "$WORK_DIR/xbl_a_verify.elf"; then
        error "Verification failed for xbl_a - written data does not match!"
    fi

    info "✓ xbl_a verified"

    info "Verifying xbl_b..."
    dd if="$XBL_B_PART" of="$WORK_DIR/xbl_b_verify.elf" bs=4096 count=$((XBL_FINAL_SIZE / 4096 + 1)) status=none

    if ! cmp -n "$XBL_FINAL_SIZE" "$WORK_DIR/xbl_final.elf" "$WORK_DIR/xbl_b_verify.elf"; then
        error "Verification failed for xbl_b - written data does not match!"
    fi

    info "✓ xbl_b verified"

    section "✓ INSTALLATION COMPLETE"

    info "u-boot.bin has been successfully installed to both xbl_a and xbl_b partitions"
    info "You can now reboot the device to boot with the new u-boot"
    warn "Make sure you have a way to recover if the device fails to boot!"

    echo ""
}

# Main command dispatcher
case "$COMMAND" in
    help)
        show_help
        ;;
    check)
        cmd_check
        ;;
    mount)
        cmd_mount
        ;;
    unmount)
        cmd_unmount
        ;;
    install)
        cmd_install
        ;;
    *)
        error "Unknown command: $COMMAND. Run '$0 help' for usage."
        ;;
esac
