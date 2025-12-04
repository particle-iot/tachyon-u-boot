# Installing U-Boot to Tachyon via ADB

## Prerequisites

1. **Built U-Boot binary**: `u-boot-dtb.bin` must exist in the current directory
2. **ADB connection**: Device must be connected and visible via `adb devices`
3. **qtoolsign tools**: Automatically downloaded and installed by install.sh if missing (requires device internet connectivity)

## Step 1: Build U-Boot

```bash
cd /Users/nicklambourne/Documents/particle/tachyon/tachyon-u-boot
./build.sh
```

This creates `u-boot-dtb.bin` (approximately 1.3MB).

## Step 2: Check Device Connection

```bash
adb devices -l
```

You should see your device listed. Copy the serial number (long hex string).

Example output:
```
List of devices attached
a3c44498557bcd745e08c674f8793d90c274b09c0c86ae134459c1e290b41964 device usb:0-1.1.3 transport_id:2
```

## Step 3: Ensure Device Has Internet Connection (for automatic qtoolsign download)

The install.sh script will automatically download and install qtoolsign tools if missing. This requires device internet connectivity.

Check device internet connection:
```bash
adb shell 'ping -c 3 8.8.8.8'
```

If device has no internet, connect to WiFi:
```bash
# Connect via ADB shell
adb shell

# List available WiFi networks
nmcli dev wifi list

# Connect to network
nmcli dev wifi connect <network-name> password <password>

# Verify connectivity
ping -c 3 8.8.8.8

# Exit shell
exit
```

**Note**: If you prefer to manually install qtoolsign instead of automatic download, see the "Manual qtoolsign Installation" section at the end of this document.

## Step 4: Install U-Boot

### Method 1: Using Serial Number (Recommended)

```bash
./install.sh --adb --serial <DEVICE_SERIAL> --yes install
```

Example:
```bash
./install.sh --adb --serial a3c44498557bcd745e08c674f8793d90c274b09c0c86ae134459c1e290b41964 --yes install
```

### Method 2: Without Serial (if only one device connected)

```bash
./install.sh --adb --yes install
```

## What the Install Script Does

The `install.sh` script performs these steps automatically:

1. **Verify prerequisites**
   - Checks if device is a Tachyon
   - Verifies `u-boot-dtb.bin` exists (NOT plain `u-boot.bin`)
   - Checks for qtoolsign tools on device
   - **If qtoolsign is missing**: Automatically downloads and installs it
     - Verifies device internet connectivity
     - Installs git and pip3 if missing
     - Clones qtoolsign from https://github.com/msm8916-mainline/qtestsign.git
     - Installs Python dependencies (cryptography)

2. **Locate XBL partitions**
   - Finds `xbl_a` (usually `/dev/sdb1`)
   - Finds `xbl_b` (usually `/dev/sdc1`)

3. **Read current XBL partition**
   - Reads `xbl_a` into memory (3.7MB)

4. **Patch bootloader with U-Boot**
   - Uses `patchxbl.py` to inject `u-boot-dtb.bin` into the XBL ELF
   - Replaces the xbl_core segment with U-Boot

5. **Sign patched bootloader**
   - Uses `qtestsign.py` to sign the patched XBL
   - Creates proper ELF headers and hash segments

6. **Write to both partitions**
   - Writes signed bootloader to `xbl_a`
   - Writes signed bootloader to `xbl_b` (redundant copy)
   - Syncs to ensure writes complete

## Installation Output

You'll see progress through each step:

```
========================================
1) VERIFY PREREQUISITES
========================================
✓ Confirmed tachyon device
✓ Found u-boot.bin (1309528 bytes)
✓ Found qtestsign tools at /tmp/qtoolsign

========================================
2) LOCATE XBL PARTITIONS
========================================
✓ Found xbl_a: /dev/sdb1
✓ Found xbl_b: /dev/sdc1

========================================
3) CREATE WORKING DIRECTORY
========================================
✓ Working directory: /tmp/tachyon-uboot-install-XXXXX

========================================
4) READ XBL_A PARTITION
========================================
✓ Read xbl_a (3690496 bytes)

========================================
5) PATCH BOOTLOADER WITH U-BOOT
========================================
✓ Patched xbl (2435600 bytes)

========================================
6) SIGN PATCHED BOOTLOADER
========================================
✓ Signed xbl (2439696 bytes)

========================================
7) WRITE TO XBL_A PARTITION
========================================
✓ Written to xbl_a

========================================
8) WRITE TO XBL_B PARTITION
========================================
✓ Written to xbl_b

========================================
✓ INSTALLATION COMPLETE
========================================
```

## Step 5: Reboot Device

```bash
adb reboot
```

Wait for device to come back online:

```bash
# Check when device is back
adb wait-for-device
adb shell 'uname -r'
```

## Verification

After reboot, verify U-Boot is running:

```bash
adb shell 'dmesg | grep "U-Boot"'
```

Should show:
```
[    0.000000] efi: EFI v2.11 by Das U-Boot
```

## Flags Explained

- `--adb`: Use ADB to connect to device (instead of running directly on device)
- `--serial <SERIAL>`: Specify device serial number (required if multiple devices)
- `--yes`: Auto-confirm installation (skip interactive prompt)
- `install`: The action to perform

## Safety Notes

⚠️ **IMPORTANT**:
- Installation modifies the bootloader partitions (`xbl_a` and `xbl_b`)
- Writes to both partitions for redundancy
- Always ensure you have a way to recover if boot fails
- The `--yes` flag skips the safety confirmation prompt

## Installing on Device (Direct)

You can also run the installation script directly on the Tachyon device instead of via ADB from a host.

**Prerequisites on Device:**
- `u-boot-dtb.bin` file on device
- Internet connection (for qtoolsign download)
- sudo access

**Installation Steps:**

```bash
# Connect to device
adb shell

# Navigate to directory with u-boot-dtb.bin
cd ~/tachyon-u-boot

# Run installation
sudo ./install.sh --device install
```

**What happens:**
1. Same auto-installation process as ADB mode
2. Qtoolsign auto-downloaded if missing
3. Bootloader signed and installed locally
4. No file pushing needed (already on device)

## Troubleshooting

### Error: "Device has no internet connectivity"

The install.sh script requires internet to automatically download qtoolsign tools if they're missing.

**Solution - Connect to WiFi:**
```bash
# 1. Connect to device
adb shell

# 2. List available networks
nmcli dev wifi list

# 3. Connect to WiFi network
nmcli dev wifi connect <network-name> password <password>

# 4. Verify connectivity
ping -c 3 8.8.8.8

# 5. Exit and retry installation
exit
./install.sh --adb --serial <DEVICE_SERIAL> --yes install
```

### Error: "pip3: command not found"

If pip3 is not installed on the device:

```bash
adb shell 'sudo apt-get update && sudo apt-get install -y python3-pip'
```

### Error: "git: command not found"

If git is not installed on the device:

```bash
adb shell 'sudo apt-get update && sudo apt-get install -y git'
```

### Error: "u-boot-dtb.bin not found"

Build U-Boot first:
```bash
./build.sh
```

### Error: "Device not found" or "more than one device"

Specify device serial explicitly:
```bash
./install.sh --adb --serial <YOUR_SERIAL> --yes install
```

### Check qtoolsign Installation

Verify qtoolsign is installed correctly:
```bash
adb shell 'ls -la /tmp/qtoolsign/patchxbl.py /tmp/qtoolsign/qtestsign.py'
```

### Device won't boot after installation

If device fails to boot, you may need to:
1. Use fastboot to recover
2. Flash stock bootloader
3. Contact support for recovery procedures

## Installation Script Features

**Automatic Dependency Management:**
- Detects missing qtoolsign tools
- Checks internet connectivity before download
- Verifies git and pip3 availability
- Downloads and configures qtoolsign automatically
- Installs Python dependencies

**Safety Features:**
- Verifies device model is Tachyon
- Checks partitions are not mounted
- Confirms partition sizes
- Validates signed bootloader size fits in partition
- Auto-confirmation with `--yes` flag or manual prompt

**Dual-Partition Redundancy:**
- Writes to both xbl_a and xbl_b
- Ensures bootloader survives single partition corruption

**Available Commands:**

```bash
# Check device readiness
./install.sh --adb --serial <SERIAL> check

# Unmount partitions if needed
./install.sh --adb --serial <SERIAL> unmount

# Install with confirmation prompt
./install.sh --adb --serial <SERIAL> install

# Install with auto-confirmation
./install.sh --adb --serial <SERIAL> --yes install

# Show help
./install.sh --help
```

**Environment Variables:**

```bash
# Set default serial
export ADB_SERIAL=d03b587c1803d886610d0cf84d93923dd7c2f4191bbc7dddf72189fafe64917c
./install.sh --adb install

# Use custom qtoolsign location
export QTOOLS_DIR=/opt/qtoolsign
./install.sh --adb --serial <SERIAL> install
```

## Complete Installation Command (One-liner)

```bash
cd /Users/nicklambourne/Documents/particle/tachyon/tachyon-u-boot && \
./build.sh && \
./install.sh --adb --serial a3c44498557bcd745e08c674f8793d90c274b09c0c86ae134459c1e290b41964 --yes install && \
adb reboot
```

Replace `a3c44498557bcd745e08c674f8793d90c274b09c0c86ae134459c1e290b41964` with your actual device serial.

## Manual qtoolsign Installation (Emergency Recovery)

If automatic qtoolsign installation fails and you need to manually install:

```bash
# Connect to device
adb shell

# Ensure git and pip3 are installed
sudo apt-get update
sudo apt-get install -y git python3-pip

# Remove old qtoolsign if present
sudo rm -rf /tmp/qtoolsign

# Clone qtoolsign
cd /tmp
git clone https://github.com/msm8916-mainline/qtestsign.git
cd qtoolsign

# Install Python dependencies
sudo pip3 install --break-system-packages -r requirements.txt

# Verify installation
ls -la patchxbl.py qtestsign.py

# Exit shell
exit
```

Then retry the install.sh script.

## Manual Installation Steps (Emergency Recovery)

If install.sh fails completely and you need to manually install, follow these steps on the device:

```bash
# Connect to device
adb shell

# Ensure qtoolsign is installed
ls /tmp/qtoolsign/patchxbl.py /tmp/qtoolsign/qtestsign.py

# Patch bootloader with U-Boot
sudo python3 /tmp/qtoolsign/patchxbl.py \
    -c /tmp/u-boot-dtb.bin \
    -o /tmp/xbl-patched.elf \
    /dev/disk/by-partlabel/xbl_a

# Sign patched bootloader
sudo python3 /tmp/qtoolsign/qtestsign.py -v6 abl \
    -o /tmp/xbl-signed.elf \
    /tmp/xbl-patched.elf

# Write to both partitions
sudo dd if=/tmp/xbl-signed.elf of=/dev/disk/by-partlabel/xbl_a bs=4096
sudo dd if=/tmp/xbl-signed.elf of=/dev/disk/by-partlabel/xbl_b bs=4096

# Sync and reboot
sudo sync
sudo reboot
```

**Note**: The install.sh script handles all of this automatically, including qtoolsign installation. Manual steps are only needed for emergency recovery.
