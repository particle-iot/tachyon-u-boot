# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Documentation Index

**READ THESE FIRST** - Essential documentation files in this repository:

- **[BUILD.md](BUILD.md)** - How to build U-Boot (Docker/native)
- **[INSTALL-UBOOT.md](INSTALL-UBOOT.md)** - How to install U-Boot via ADB
- **[audio-test-commands.md](audio-test-commands.md)** - Audio testing commands and procedures
- **[ADSP_KERNEL_ENUM_MISMATCH.md](ADSP_KERNEL_ENUM_MISMATCH.md)** - ADSP/kernel enum compatibility issues
- **[PMIC_PROPERTY_MISMATCH.md](PMIC_PROPERTY_MISMATCH.md)** - PMIC property issues
- **[NEXT.md](NEXT.md)** - Next steps and TODOs
- **[README.md](README.md)** - General repository overview

**Related Kernel Documentation:**
- Kernel repository location: `/Users/nicklambourne/Documents/particle/tachyon/tachyon-ubuntu-24.04-kernel/`
- Kernel build script: `tachyon-ubuntu-24.04-kernel/build.sh`
- Kernel install script: `tachyon-ubuntu-24.04-kernel/install.sh`

## Project Overview

This is the **Tachyon U-Boot** repository, containing the bootloader for Particle's Tachyon hardware platform (QCM6490-based). U-Boot loads the kernel and passes the device tree to configure hardware.

## Repository Structure

```
tachyon-u-boot/
├── dts/upstream/src/arm64/qcom/
│   └── qcm6490-tachyon.dts          # Main device tree for Tachyon
├── build.sh                          # Build script (uses Docker)
├── install.sh                        # Installation script (via ADB)
├── .circleci/config.yml             # CI/CD configuration
└── configs/qcm6490_tachyon_config   # U-Boot configuration
```

## Related Repositories

### Kernel Repository
**Location**: `/Users/nicklambourne/Documents/particle/tachyon/tachyon-ubuntu-24.04-kernel/`

The Ubuntu 24.04 kernel (6.8.0-1056-particle) contains:
- Audio drivers: `sound/soc/qcom/qdsp6/` (Q6 ASM, AFE, routing)
- ES8328 codec driver: `sound/soc/codecs/es8328.c`
- Machine driver: `sound/soc/qcom/apq8096.c`

Key kernel files:
- `sound/soc/qcom/qdsp6/q6asm-dai.c` - Frontend DAI for PCM streams
- `sound/soc/qcom/qdsp6/q6afe-dai.c` - Backend DAI for hardware interfaces
- `sound/soc/qcom/qdsp6/q6routing.c` - Audio routing between frontends and backends

### Baseband Processor Firmware Repositories

**Primary (Internal)**: `/Users/nicklambourne/Documents/particle/tachyon/tachyon-quectel-bp-fw/`
Contains the Quectel baseband processor firmware and ADSP firmware.

**Upstream (Public)**: `/Users/nicklambourne/Documents/particle/tachyon/tachyon-quectel-bp-fw-upstream/`
Public version of BP firmware.

Key firmware files:
- `apps_proc/vendor/qcom/proprietary/audio-effects/tachyon/adsp.mdt` - ADSP firmware using APR protocol

### Composer Repository
**Location**: `/Users/nicklambourne/Documents/particle/tachyon/tachyon-composer/`

Builds complete system images. See `/Users/nicklambourne/Documents/particle/tachyon/tachyon-composer/CLAUDE.md` for details.

## Building U-Boot

**For detailed build instructions, see [BUILD.md](BUILD.md)**

### Quick Build Summary

The `build.sh` script provides flexible U-Boot building with automatic detection of the environment:
- **Host Build (Docker)**: Builds U-Boot in a Docker container on Mac/Linux workstations
- **Native Build**: Automatically detects Tachyon device and builds natively without Docker
- **Dependency Management**: Automatically installs missing build tools

**Quick Start (from Mac/Linux host):**

```bash
cd /Users/nicklambourne/Documents/particle/tachyon/tachyon-u-boot
./build.sh
```

**Quick Start (on Tachyon device):**

```bash
# Connect to device
adb shell
cd ~/tachyon-u-boot

# Build (auto-detects device, installs dependencies if needed)
./build.sh
```

**Build Output:**
- `u-boot-dtb.bin` - U-Boot with embedded device tree (~1.3MB) **← This is what you need**
- `u-boot` - U-Boot ELF binary
- `System.map` - Symbol map
- `dts/dt.dtb` - Compiled device tree

**Key features:**
- Auto-detects environment (Tachyon device vs host)
- Docker build on host, native build on device
- Automatic dependency installation
- Clean build option: `./build.sh --clean`

**For complete documentation including:**
- Detailed build options and modes
- Custom configurations
- Manual build process
- Device tree modifications
- Troubleshooting

**See [BUILD.md](BUILD.md)**

## Installing U-Boot

**For detailed installation instructions, see [INSTALL-UBOOT.md](INSTALL-UBOOT.md)**

### Quick Install Summary

The `install.sh` script handles bootloader signing and installation with automatic dependency management:
- **Automatic qtoolsign Installation**: Downloads and installs signing tools if missing
- **Safety Checks**: Verifies device, partitions, and file integrity before writing
- **Dual-Partition Install**: Writes to both xbl_a and xbl_b for redundancy
- **Host or Device Execution**: Works via ADB from host or directly on device

**Quick Start (from Mac/Linux host):**

```bash
# 1. Build U-Boot
./build.sh

# 2. Get device serial
adb devices -l

# 3. Install to device (qtoolsign auto-downloads if missing)
./install.sh --adb --serial <DEVICE_SERIAL> --yes install

# 4. Reboot
adb reboot
```

**Key features:**
- Automatically downloads qtoolsign if missing (requires device internet)
- Only accepts `u-boot-dtb.bin` (never falls back to plain `u-boot.bin`)
- WiFi setup via `nmcli` if device needs internet connectivity
- Can run directly on device with `--device` flag
- Manual installation steps available for emergency recovery

**For complete documentation including:**
- Step-by-step installation from host (Mac/Linux)
- Installation directly on device
- WiFi connection troubleshooting
- Manual qtoolsign installation
- Emergency recovery procedures
- All script options and commands

**See [INSTALL-UBOOT.md](INSTALL-UBOOT.md)**

## Audio Subsystem Architecture

### Overview

Tachyon uses Qualcomm's QDSP6 audio architecture with:
- **ES8328** codec via I2C and I2S (PRIMARY_MI2S interface)
- **APR** (Asynchronous Packet Router) protocol for ADSP communication
- **Q6 ASM** (Audio Stream Manager) for frontend PCM streams
- **Q6 AFE** (Audio Front End) for backend hardware interfaces
- **Q6 Routing** component to connect frontends to backends

### Audio Data Flow

```
Application (aplay/ALSA)
    ↓
ALSA PCM device (hw:0,0)
    ↓
MultiMedia1 Frontend (Q6 ASM DAI)
    ↓
Q6 Routing (mixer control enables path)
    ↓
PRIMARY_MI2S_RX Backend (Q6 AFE DAI port 16)
    ↓
I2S bus
    ↓
ES8328 Codec (I2C address 0x10)
    ↓
Speakers/Headphones (LOUT1, ROUT1, LOUT2, ROUT2)
```

### Device Tree Configuration

The audio system is configured in `dts/upstream/src/arm64/qcom/qcm6490-tachyon.dts`:

**Key sections**:

1. **Sound card** (line 250):
```dts
sound {
    compatible = "qcom,apq8096-sndcard";
    model = "tachyon-audio";

    audio-routing =
        "Headphones", "LOUT1",
        "Headphones", "ROUT1",
        "Speakers", "LOUT2",
        "Speakers", "ROUT2",
        "LINPUT1", "Mic Jack",
        "RINPUT1", "Mic Jack";
```

2. **Frontend DAI Link** (line 263):
```dts
mm1-dai-link {
    link-name = "MultiMedia1";
    cpu {
        sound-dai = <&q6asmdai 0>;  /* MULTIMEDIA1 */
    };
};
```

3. **Backend DAI Link** (line 271):
```dts
primary-mi2s-dai-link {
    link-name = "Primary MI2S";
    cpu {
        sound-dai = <&q6afedai 16>;  /* PRIMARY_MI2S_RX = 16 */
    };

    platform {
        sound-dai = <&q6routing>;
    };

    codec {
        sound-dai = <&es8328>;
    };
};
```

4. **Q6 ASM DAI** (line 1417):
```dts
q6asmdai: dais {
    compatible = "qcom,q6asm-dais";
    #sound-dai-cells = <1>;
    #address-cells = <1>;
    #size-cells = <0>;
    /* NOTE: iommus property removed - ADSP firmware uses physical addresses */
};
```

### Critical Device Tree Fix

**IMPORTANT**: The `iommus` property must NOT be present on the q6asmdai node.

**Reason**: The ADSP firmware (`tachyon/adsp.mdt`) uses APR protocol and expects physical memory addresses. If the `iommus` property is present, the kernel assigns q6asm-dai to an IOMMU group and translates addresses, causing memory allocation failures during audio playback.

**Location**: `dts/upstream/src/arm64/qcom/qcm6490-tachyon.dts` line ~1421

**Incorrect** (causes crashes):
```dts
q6asmdai: dais {
    compatible = "qcom,q6asm-dais";
    iommus = <&apps_smmu 0x1821 0x0>;  /* WRONG - causes memory errors */
};
```

**Correct**:
```dts
q6asmdai: dais {
    compatible = "qcom,q6asm-dais";
    /* No iommus property */
};
```

### ES8328 Codec Configuration

The ES8328 codec is configured at line 1468:

```dts
&i2c0 {
    es8328: codec@10 {
        compatible = "everest,es8328";
        reg = <0x10>;
        clocks = <&lpass_audiocc 9>;  /* LPASS_CDC_VA_MCLK */
        clock-names = "mclk";
        #sound-dai-cells = <0>;
    };
};
```

**Key details**:
- I2C address: 0x10
- MCLK: 9.6MHz (from LPASS_CDC_VA_MCLK)
- Kernel driver maps 9.6MHz to 12.288MHz constraints (commit 525e49388470)

### LPASS Clock Controller

The hardware LPASS clock controller is intentionally disabled (line 1454):

```dts
&lpasscc {
    status = "disabled";  /* Q6 AFE provides clocks instead */
};
```

This is expected and correct - Q6 AFE manages the audio clocks.

## Testing Audio

### Check System Status

```bash
# Check kernel version
adb shell 'uname -r'
# Should show: 6.8.0-1056-particle

# Check sound cards
adb shell 'cat /proc/asound/cards'
# Should show: card0: tachyonaudio

# Check PCM devices
adb shell 'aplay -l'
# Should show: card 0, device 0 (MultiMedia1)

# Check codec initialization
adb shell 'dmesg | grep es8328'
# Should show: "Mapping Qualcomm 9.6MHz sysclk to 12.288MHz constraints"

# Check IOMMU assignment (should be empty)
adb shell 'dmesg | grep "q6asm-dai.*iommu"'
# Should show nothing (no IOMMU group assignment)

# Check for backend routing error
adb shell 'dmesg | grep "no backend DAIs enabled"'
# This message indicates routing needs to be configured
```

### Check Available Mixer Controls

```bash
# List all mixer controls
adb shell 'amixer -c 0 scontrols'

# Check for critical routing control
adb shell 'amixer -c 0 scontrols | grep "PRI_MI2S_RX Audio Mixer MultiMedia1"'
# This control MUST exist for audio to work
```

### Enable Audio Routing

Audio routing must be enabled via ALSA mixer controls before playback will work:

```bash
# Enable routing from MultiMedia1 frontend to PRIMARY_MI2S_RX backend
adb shell 'amixer -c 0 cset name="PRI_MI2S_RX Audio Mixer MultiMedia1" 1'
```

**Why this is needed**: Qualcomm's DPCM (Dynamic PCM) architecture requires explicit routing between frontend streams (MultiMedia1) and backend hardware interfaces (PRIMARY_MI2S_RX). Without enabling this mixer control, the audio path is not connected and playback will fail with "Unable to install hw params".

### Test Audio Playback

```bash
# Push test audio file
adb push /path/to/test.wav /tmp/test.wav

# Enable routing
adb shell 'amixer -c 0 cset name="PRI_MI2S_RX Audio Mixer MultiMedia1" 1'

# Test playback (short duration to avoid crashes)
adb shell 'timeout 1s aplay -D hw:0,0 /tmp/test.wav'
```

### Complete Audio Setup Script

For convenient testing, use this script:

```bash
#!/bin/bash
# audio-test.sh

DEVICE_SERIAL="<your-device-serial>"

# Enable routing
adb -s $DEVICE_SERIAL shell 'amixer -c 0 cset name="PRI_MI2S_RX Audio Mixer MultiMedia1" 1'

# Optional: Configure ES8328 codec outputs
adb -s $DEVICE_SERIAL shell 'amixer -c 0 cset name="Output 1" 20'  # Headphones volume
adb -s $DEVICE_SERIAL shell 'amixer -c 0 cset name="Output 2" 20'  # Speakers volume
adb -s $DEVICE_SERIAL shell 'amixer -c 0 cset name="PCM Volume" 192'

# Test playback
adb -s $DEVICE_SERIAL shell 'aplay -D hw:0,0 /tmp/test.wav'
```

## Known Issues

### Audio Playback Crashes (CRITICAL)

**Status**: PARTIALLY DIAGNOSED - Crash location identified

**Symptom**: Device hard crashes/reboots immediately when audio playback is attempted, even with /dev/zero as input.

**What works**:
- ✅ Sound card registers successfully
- ✅ ES8328 codec initializes with 9.6MHz sysclk
- ✅ Routing can be enabled (`PRI_MI2S_RX Audio Mixer MultiMedia1` control exists)
- ✅ Audio device opens successfully
- ✅ Hardware parameters can be negotiated

**What fails**:
- ❌ **Device crashes IMMEDIATELY during `snd_pcm_prepare()` call**
- ❌ Crash happens even with 0.1 second timeout and /dev/zero input
- ❌ **No kernel panic trace** - hard lockup/watchdog reset
- ❌ Occurs before any actual audio data is transferred

**Root cause**: **CONFIRMED - DMA buffer memory mapping failure**

The crash occurs in `q6asm_dai_prepare()` at line 250-253 in `sound/soc/qcom/qdsp6/q6asm-dai.c`:
```c
ret = q6asm_map_memory_regions(substream->stream, prtd->audio_client,
                               prtd->phys,
                               (prtd->pcm_size / prtd->periods),
                               prtd->periods);
```

**Detailed Analysis**:

1. **DMA Buffer Allocation** (line 1185-1186):
   ```c
   return snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
                                       component->dev, size);
   ```
   - Allocates DMA buffers using `SNDRV_DMA_TYPE_DEV`
   - Buffers are allocated from general system memory
   - Physical address stored in `substream->dma_buffer.addr`

2. **Physical Address Setup** (line 435-438 in `q6asm_dai_open()`):
   ```c
   if (pdata->sid < 0)
       prtd->phys = substream->dma_buffer.addr;
   else
       prtd->phys = substream->dma_buffer.addr | (pdata->sid << 32);
   ```
   - With iommus property removed, `pdata->sid = -1`
   - Uses plain physical address (no IOMMU translation)
   - This address is passed to ADSP firmware via APR

3. **The Problem**:
   - The ADSP firmware expects DMA buffers in a **specific memory range** it can access
   - System memory allocated by `SNDRV_DMA_TYPE_DEV` may be outside ADSP's accessible range
   - When `q6asm_map_memory_regions` tries to pass these addresses to ADSP, it causes a hard crash
   - No kernel panic because the crash likely occurs in ADSP firmware or during bus access

**Evidence**:
- Crash timing: Happens during `prepare` phase, not during data transfer
- Test with `/dev/zero` and 0.1s timeout still crashes immediately
- No kernel panic trace in serial console (indicates hardware-level crash)
- Serial console shows immediate reboot after "Playing WAVE..." message

**Memory Ranges**:

From device tree and kernel sources:
- ADSP firmware region: `0x86700000` - `0x88F00000` (40MB, defined in sc7280.dtsi)
- System memory: Various ranges, allocated dynamically
- **Gap**: No defined audio DMA buffer pool or carveout region

**Possible Solutions** (in order of likelihood):

1. **Reserved Memory Region** (RECOMMENDED):
   - Add a reserved memory region in device tree specifically for audio DMA
   - Example from other Qualcomm devices:
     ```dts
     reserved-memory {
         audio_heap: audio@XXXXXXXX {
             reg = <0x0 0xXXXXXXXX 0x0 0x400000>; /* 4MB */
             no-map;
         };
     };
     ```
   - Reference this region in audio subsystem
   - Requires finding the correct memory address range accessible to ADSP

2. **CMA (Contiguous Memory Allocator)**:
   - Modify q6asm-dai.c to use `SNDRV_DMA_TYPE_DEV` with proper DMA constraints
   - Set `dma-ranges` property in device tree for ADSP subsystem
   - May require kernel patches

3. **Compare with working 20.04 kernel**:
   - Check if 20.04 kernel (5.4.219) has patches that change DMA allocation
   - Look for differences in q6asm-dai.c between 5.4.219 and 6.8.0
   - Check device tree differences

4. **ADSP Memory Accessibility**:
   - Investigate Qualcomm documentation for ADSP memory map
   - Determine valid physical address ranges for ADSP DMA
   - May need to configure SMMU (System MMU) differently

**Testing Done**:
```bash
# Minimal test that still crashes:
adb shell 'amixer -c 0 cset name="PRI_MI2S_RX Audio Mixer MultiMedia1" 1'
adb shell 'timeout 0.1s aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 /dev/zero'
# Result: Hard crash/reboot, no kernel panic
```

**Next Steps**:
1. Compare 20.04 kernel q6asm-dai.c with 24.04 version
2. Check for audio-specific reserved memory in working systems
3. Consult Qualcomm documentation for ADSP DMA memory requirements
4. Consider adding audio DMA buffer carveout region in device tree

**Workaround**: None currently. Audio playback is completely non-functional on Ubuntu 24.04.

### Missing TinyALSA Tools on Ubuntu 24.04

The `tinymix` and `tinyplay` utilities from TinyALSA are not installed on Ubuntu 24.04 builds. These tools provide simpler control of Qualcomm audio hardware.

**Workaround**: Use standard ALSA tools (`amixer`, `aplay`) instead.

### LPASS Clock Controller Probe Failure

**Message**: `lpass_audio_cc-sc7280: probe of 3300000.clock-controller failed with error -110`

**Status**: Expected and harmless

**Reason**: Hardware LPASS clock controller is intentionally disabled in device tree. Q6 AFE provides clocks instead.

## Debugging Audio Issues

### Enable Kernel Logging

```bash
# Increase kernel log level
adb shell 'echo 8 | sudo tee /proc/sys/kernel/printk'

# Clear dmesg buffer
adb shell 'dmesg -C'

# Run audio test
adb shell 'amixer -c 0 cset name="PRI_MI2S_RX Audio Mixer MultiMedia1" 1 && aplay -D hw:0,0 /tmp/test.wav'

# Check for errors
adb shell 'dmesg | tail -100'
```

### Check DAPM (Dynamic Audio Power Management)

```bash
# Check audio power state
adb shell 'cat /sys/kernel/debug/asoc/tachyon-audio/dapm/bias_level'
# Should show: Off (when idle) or On (during playback)

# Check Headphones widget connections
adb shell 'cat /sys/kernel/debug/asoc/tachyon-audio/dapm/Headphones'

# List all DAPM widgets
adb shell 'ls /sys/kernel/debug/asoc/tachyon-audio/dapm/'
```

### Check Component Registration

```bash
# List all ASoC components
adb shell 'cat /sys/kernel/debug/asoc/components'

# Expected components:
# - 3700000.remoteproc:glink-edge:apr:service@4:dais  (Q6 ASM)
# - 3700000.remoteproc:glink-edge:apr:service@7:dais  (Q6 AFE)
# - 3700000.remoteproc:glink-edge:apr:service@8:routing  (Q6 Routing)
# - es8328.0-0010  (ES8328 codec)
```

### Check DAI Registration

```bash
# List all registered DAIs
adb shell 'cat /sys/kernel/debug/asoc/dais'

# Verify PRIMARY_MI2S_RX exists
adb shell 'cat /sys/kernel/debug/asoc/dais | grep PRI_MI2S_RX'
```

## CI/CD

The repository uses CircleCI for automated builds and releases.

**Configuration**: `.circleci/config.yml`

**Build trigger**: Push to `tachyon` branch or pull request

**Build steps**:
1. Install dependencies (gcc-aarch64-linux-gnu, device-tree-compiler, etc.)
2. Build U-Boot using `make qcm6490_tachyon_config && make -j$(nproc)`
3. Package artifacts: `u-boot*, System.map, dt.dtb, .config`
4. Upload to S3: `s3://BUCKET/release/tachyon-u-boot-VERSION.zip`
5. Create GitHub release (on `tachyon` branch only)

**Versioning**:
- Uses semantic versioning from git tags
- Format: `MAJOR.MINOR.PATCH`
- Non-default branches get prerelease versions: `LAST_VERSION+build.COMMIT_SHA`

## Important Notes

1. **Always rebuild U-Boot after device tree changes**: Device tree is embedded in u-boot-dtb.bin
2. **IOMMU property**: Never add `iommus` property to q6asmdai - causes memory errors
3. **Routing must be enabled**: Audio won't work without setting `PRI_MI2S_RX Audio Mixer MultiMedia1` control
4. **Device may crash during playback**: Known issue, cause under investigation
5. **qtoolsign required**: U-Boot must be signed before installation - install.sh handles this

## References

- Qualcomm QDSP6 audio documentation: `sound/soc/qcom/qdsp6/` in kernel
- ES8328 datasheet: Available from Everest Semiconductor
- U-Boot documentation: https://u-boot.readthedocs.io/
- ALSA documentation: https://www.alsa-project.org/wiki/Documentation
- Tachyon documentation: https://developer.particle.io/tachyon/

## Getting Help

When reporting audio issues, include:
1. Kernel version: `adb shell 'uname -r'`
2. U-Boot build size: `ls -lh u-boot-dtb.bin`
3. Device tree check: `adb shell 'cat /sys/firmware/devicetree/base/soc*/remoteproc*/glink-edge/apr/service@7/dais/iommus'` (should error)
4. Sound card status: `adb shell 'cat /proc/asound/cards'`
5. dmesg errors: `adb shell 'dmesg | grep -E "es8328|q6asm|error|fail"'`
6. Mixer control status: `adb shell 'amixer -c 0 cget name="PRI_MI2S_RX Audio Mixer MultiMedia1"'`
