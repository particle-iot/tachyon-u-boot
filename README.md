## Tachyon
### Device Tree

Device Tree for Tachyon is currently bundled into U-boot to simplify development/migration between Quectel and upstream builds.

It's located in [dts/upstream/src/arm64/qcom/qcm6490-tachyon.dts](https://github.com/particle-iot-inc/tachyon-u-boot/blob/tachyon/dts/upstream/src/arm64/qcom/qcm6490-tachyon.dts).

`dts/upstream` is synced to https://github.com/particle-iot-inc/tachyon-linux-qcom-devicetree-rebasing, which is an extract of Devicetree from https://git.codelinaro.org/clo/le/canonical-kernel/ubuntu/source/linux-qcom/noble/-/tree/canonical/master-next?ref_type=heads, which contains some changes that haven't been upstreamed yet (e.g. I've initially had to add PCIE0 support manually, now it's in base sc7280.dtsi).

### Building

```console
$ export CROSS_COMPILE=aarch64-linux-gnu-
$ make qcm6490_tachyon_config
$ make -j8
```

### Patching XBL and re-signing

XBL binary (xbl.elf) needs to be taken from latest 20.04 Quectel Ubuntu build.

```console
$ patchxbl -o xbl.elf -c u-boot-dtb.bin xbl.elf
$ qtestsign -v6 abl -o xbl.mbn xbl.elf
$ qtestsign -v6 aboot -o u-boot.mbn u-boot.elf
```

### Flashing

```console
$ edl --loader=prog_firehose_ddr.elf w xbl_a xbl.mbn
$ edl --loader=prog_firehose_ddr.elf w xbl_b xbl.mbn
$ edl --loader=prog_firehose_ddr.elf reset
```
