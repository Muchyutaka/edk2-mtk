# T1101 Native UEFI / Windows 11 ARM64 — USB Worklog

This is a handoff document for continuing tomorrow. It records everything known so far, the current state of the repository, what has been verified on hardware, what is still experimental, and the exact next steps.

## 1. Goal and constraints

### Objective

Build a native EDK2/UEFI `boot.img` for the Tecno MegaPad 11 (T1101, MT8781/Helio G99) that boots Windows 11 ARM64 natively from a USB-C flash drive.

USB keyboard and USB mass storage must work in the UEFI shell so the Windows installer can be started without emulation.

### Hard constraints

- Native bare-metal boot only. No QEMU, emulation, or remote desktop.
- Only the `edk2-mtk` repo should be modified.
- `android_device_tecno_T1101` is generated from aospdtgen and is reference-only. Do not modify it.
- Minimize re-flashing.
- Keep a copy of stock Android `boot` before flashing.
- Use plain `fastboot flash boot`; no `_b` suffix is needed.
- User has a USB-C flash drive and a USB-C OTG hub with three ports. The hub works under Android.
- USB-C plug → USB-A socket adapter direction is correct.
- User copies shell text with Google Lens, so copied values may contain OCR errors. Verify suspicious hex values.
- Windows 11 ARM64 image is not installed yet; native Windows boot remains untested.

## 2. Known hardware and firmware facts

### Device

| Item | Value |
|---|---|
| Device | Tecno MegaPad 11 |
| Model | T1101 |
| SoC | MT8781 / Helio G99 |
| Related SoC used as base | MT6789 / MT8781 are effectively the same silicon |
| Current slot | `b` |
| Slot count | 2 |
| Bootloader version | `t1101_m1101_0e22c0f14_202604101038` |
| Panel | `nt36672e_fhd_dsi_vdo_60hz_jdi_dphy_drv` |
| Resolution | 1200×1920 |
| Framebuffer stride | 1216 pixels |
| Live framebuffer base | `0xfe41a000` |
| Live VRAM size | `0x01ac0000` |
| FPS field | `6000` |
| On-DTB stale framebuffer base | `0x7e605000` |
| On-DTB stale VRAM size | `0x01be0000` |

The on-disk DTB value for the framebuffer is stale because LK rewrites it before jumping to the kernel. Always use the live value `0xfe41a000`.

### USB hardware

- USB2 controller node: `/soc/usb0@11200000`
- Compatible: `mediatek,mt6789-usb20`
- `dr_mode = "otg"`
- `usb-role-switch`
- MUSB controller base: `0x11200000`
- USB PHY base: `0x11110800`
- MTK target base: `0x0480 + 8 * epnum + offset`
- `MUSB_DEVCTL` offset: `0x60`
- `MUSB_DEVCTL_SESSION`: `0x01`
- `USB_L1INTS`: `0xA0`
- `USB_L1INTM`: `0xA4`
- `MUSB_RXTOG`: `0x80`
- `MUSB_RXTOGEN`: `0x82`
- `MUSB_TXTOG`: `0x84`
- `MUSB_TXTOGEN`: `0x86`
- Endpoint registers are indexed at `0x10 + offset`
- FIFOs are at `0x20 + 4 * epnum`

For MTK MUSB, use the dedicated toggle registers `MUSB_RXTOG` and `MUSB_TXTOG`, not only the CSR DATA0/DATA1 bits. Linux `musb_host.c` does not use `MUSB_CSR0_H_WR_DATATOGGLE` in the host EP0 path, so no EP0 toggle rewrite is needed.

Linux reference: `/tmp/opencode/musb_host.c`.

### PMIC / VBUS hardware

- I2C node: `/soc/i2c@11017000`
- Compatible: `mediatek,mt6983-i2c`
- Register layout: MediaTek I2C v2, not v3.
- Slave address register:
  - v2 base `0x04` exists, but MT6983 uses the expanded register at `0x94`.
  - Use `0x94` for the PMIC slave address.
- Register address size: 2 bytes, big-endian.
- Register data size: 1 byte.
- MT6375 PMU slave: `0x34`
- MT6375 TCPC slave: `0x4E`
- `usb-otg-vbus` regulator node: `/soc/i2c@11017000/mt6375@34/chg/otg`

I2C reference files:

- `/tmp/opencode/i2c-mt6983.c` — kernel driver with `mt_i2c_regs_v2` and `mt6983_compat`
- `/tmp/opencode/i2c-mt65xx-old.c` — older reference
- `/tmp/opencode/uboot-mtk-i2c.c` — U-Boot PIO/DMA transfer flow
- `/home/yutaka/.local/share/opencode/tool-output/tool_20e42b91c001xjffPwUyEJlfgi` — saved `mt6375-charger.c`

### MT6983 I2C v2 register offsets

From `mt_i2c_regs_v2`:

| Register | Offset |
|---|---:|
| `DATA_PORT` | `0x00` |
| `SLAVE_ADDR` | `0x04` |
| `SLAVE_ADDR1` | `0x94` |
| `INTR_MASK` | `0x08` |
| `INTR_STAT` | `0x0C` |
| `CONTROL` | `0x10` |
| `TRANSFER_LEN` | `0x14` |
| `TRANSAC_LEN` | `0x18` |
| `DELAY_LEN` | `0x1C` |
| `TIMING` | `0x20` |
| `START` | `0x24` |
| `EXT_CONF` | `0x28` |
| `LTIMING` | `0x2C` |
| `HS` | `0x30` |
| `IO_CONFIG` | `0x34` |
| `FIFO_ADDR_CLR` | `0x38` |
| `SDA_TIMING` | `0x3C` |
| `TRANSFER_LEN_AUX` | `0x44` |
| `CLOCK_DIV` | `0x48` |
| `SOFTRESET` | `0x50` |
| `SCL_MIS_COMP_POINT` | `0x90` |
| `FIFO_STAT` | `0xF4` |
| `FIFO_THRESH` | `0xF8` |
| `DCM_EN` | `0xF88` |
| `MULTI_DMA` | `0xF8C` |

DMA base: `0x10217400`.

Important control bits:

| Bit | Meaning |
|---:|---|
| `BIT0` | `I2C_CONTROL_WRAPPER` |
| `BIT1` / `0x1 << 1` | `I2C_CONTROL_RS` |
| `BIT2` / `0x1 << 2` | `I2C_CONTROL_DMA_EN` |
| `BIT3` / `0x1 << 3` | `I2C_CONTROL_CLK_EXT_EN` |
| `BIT4` / `0x1 << 4` | `I2C_CONTROL_DIR_CHANGE` |
| `BIT5` / `0x1 << 5` | `I2C_CONTROL_ACKERR_DET_EN` |
| `BIT8` / `0x1 << 8` | `I2C_CONTROL_DMAACK_EN` |
| `BIT9` / `0x1 << 9` | `I2C_CONTROL_ASYNC_MODE` |

Interrupt / status bits:

| Bit | Meaning |
|---:|---|
| `BIT0` | `I2C_TRANSAC_COMP` |
| `BIT1` | `I2C_ACKERR` |
| `BIT2` | `I2C_HS_NACKERR` |
| `BIT3` | `I2C_ARB_LOST` |
| `BIT4` | `I2C_RS_TRANSFER` |

### MT6375 OTG VBUS registers

| Register | Value |
|---|---:|
| `MT6375_REG_TM_PAS_CODE1` | `0x107` |
| `MT6375_REG_CHG_TOP1` | `0x120` |
| `MT6375_REG_CHG_TOP2` | `0x121` |
| `MT6375_REG_CHG_VSYS` | `0x129` |
| `MT6375_REG_OTG_V` | `0x131` |
| `MT6375_REG_OTG_C` | `0x132` |
| `MT6375_REG_CHG_DUMY0` | `0x135` |
| `MT6375_REG_CHG_HD_BUCK5` | `0x140` |

Masks:

| Mask | Value |
|---|---:|
| `MT6375_MSK_OTG_EN` | `0x04` |
| `MT6375_MSK_OTG_CV` | `0x3F` |
| `MT6375_MSK_OTG_CC` | `0x07` |
| `MT6375_MSK_CLK_FREQ` | `0xC0` |
| `MT6375_MSK_COMP_CLAMP` | `0x03` |
| `MT6375_MSK_BUCK_RAMPOFT` | `0xC0` |
| `MT6375_MSK_BLEED_DIS_EN` | `0x80` |

Linux `mt6375_set_boost_param()` sequence:

| Register | Mask | Boost value |
|---|---:|---:|
| `0x121` | `0xC0` | `0x00` |
| `0x135` | `0x03` | `0x03` |
| `0x140` | `0xC0` | `0xC0` |
| `0x129` | `0x80` | `0x00` |

Enable sequence:

1. Write `0x69` to `0x107` to enable human-mode access.
2. Apply the boost fields above.
3. Write OTG voltage: `5.00 V`.
   - Regulator: min `4850000 uV`, step `25000 uV`, `linear_min_sel = 20`.
   - `(5000000 - 4850000) / 25000 = 6`
   - Selector = `20 + 6 = 26 = 0x1A`.
   - Write `0x1A` to `0x131`.
4. Write OTG current: `2.4 A`.
   - Current table: `500, 700, 1100, 1300, 1800, 2100, 2400 mA`.
   - Index 6.
   - Write `0x06` to `0x132`.
5. Read `0x120`, set `OTG_EN` bit `0x04`, clear `HZ` bit `0x08`, write back.
6. Wait about 50 ms.

## 3. Repository layout and decisions

### Board package

- Board package: `Platform/Tecno/t1101Pkg`
- Base package: `Platform/Xiaomi/emeraldPkg`
- Reason: MT6789 and MT8781 are effectively the same silicon.
- SoC DSC reused unchanged: `Silicon/MediaTek/MT6789Pkg/MT6789.dsc`

### Image construction

- Keep `mkbootimg --header_version 4`.
- Stock `boot.img` is also header v4.
- Use GNU `aarch64-linux-gnu-ld`; LLD rejects EDK2 absolute relocations.
- `FD_BASE = 0x50000000`
- `FD_SIZE = 0x00700000`
- FDF `NumBlocks = 0x700`
- BootShim `UEFI_BASE=0x50000000`, `UEFI_SIZE=0x00700000`

### Debug vs release console

- DEBUG: framebuffer debug console and red/green framebuffer self-test.
- RELEASE: UART serial console and no self-test.

### Framebuffer fix

- Live base: `0xfe41a000`
- Stride: `1216` pixels.
- Reason: `3 * 1216 * 1920 * 4 = 0x1AB4000`, plus `0xC000` padding fits live `vramSize 0x01AC0000`.
- `1200` stride caused row shift and diagonal “moving static”.
- `PcdMipiFrameBufferLayers|3` comes from `Silicon/MediaTek/MT6789Pkg/MT6789.dsc`.

## 4. Known-good images and release state

GitHub release:

https://github.com/Muchyutaka/edk2-mtk/releases/tag/t1101-uefi-test

### Verified known-good DEBUG image

- Path: `/home/yutaka/edk2-mtk/Build/boot-t1101-debug.img`
- MD5: `1baca0653cf68a2539772e58562f7550`
- URL: https://github.com/Muchyutaka/edk2-mtk/releases/download/t1101-uefi-test/boot-t1101-debug.img
- Status: Reached native `UEFI Interactive Shell v2.2` on hardware.
- This image does not include the new MUSB host driver.

Observed shell mappings from this image:

- `FS1`: `MemoryMapped(0xB,0x50000000,0x506FFFFF)`
- `FS0`: `MemoryMapped(0xB,0x47503000,0x47B4F7FF)`
- `BLK0`: `VirtualDisk(0x66F00000,0x6C6FFFFF,0)`

`FS0`, `FS1`, and `BLK0` are firmware-volume/ramdisk mappings, not USB installer storage.

### RELEASE image

- Path: `/home/yutaka/edk2-mtk/Build/boot-t1101-release.img`
- MD5: `26a3b1093d7ceb85a28c131d39677b39`
- URL: https://github.com/Muchyutaka/edk2-mtk/releases/download/t1101-uefi-test/boot-t1101-release.img
- Status: Built with the same stride fix, but not as extensively verified as DEBUG.

### Experimental MUSB DEBUG image

- Path: `/home/yutaka/edk2-mtk/Build/boot-t1101.img`
- MD5: `6f6d1fc421f237db7bce3de9b788de09`
- Status: Built successfully after adding the first version of `MusbHostDxe`, before the later I2C/VBUS and endpoint-mapping edits in this session.
- Not copied to `boot-t1101-debug.img`.
- Not uploaded.

### Stale asset warning

The `boot-t1101.img` asset on the GitHub release is stale. Do not use it. The earlier
`boot-t1101-musb-test.img` was hardware-tested and failed because MT6375 VBUS could not be
enabled (I2C `ACKERR`). The PIO-based `boot-t1101-vbus-i2c-fix.img` was then hardware-tested and
failed with an I2C timeout. The current experimental asset is `boot-t1101-vbus-dma-fix.img`; see
sections 15 and 16.

## 5. Flash and boot commands

Backup stock boot:

```bash
fastboot flash boot /home/yutaka/edk2-mtk/Build/boot-stock.img
```

Flash current known-good DEBUG image:

```bash
fastboot flash boot /home/yutaka/edk2-mtk/Build/boot-t1101-debug.img
fastboot reboot
```

`fastboot boot boot-t1101.img` failed with:

```text
FAILED (remote: 'unknown command')
```

So use `fastboot flash boot`, not `fastboot boot`.

## 6. Build command

```bash
cd /home/yutaka/edk2-mtk
export PATH="/home/yutaka/llvm-toolchain:/home/yutaka/aarch64-toolchain/usr/bin:/home/yutaka/acpica-toolchain/usr/bin:/home/yutaka/edk2-env/bin:$PATH"
export CLANG38_BIN="/home/yutaka/llvm-toolchain/"
/home/yutaka/edk2-env/bin/python Platform/Tecno/t1101Pkg/PlatformBuild.py "TARGET=DEBUG"
```

`/home/yutaka/llvm-toolchain/clang` is a local wrapper around `/usr/sbin/clang` that uses GNU
`/home/yutaka/aarch64-toolchain/usr/bin/aarch64-linux-gnu-ld` for AArch64 links; LLD rejects
EDK2 absolute relocations. `iasl` was extracted locally into `/home/yutaka/acpica-toolchain`.

If FVMAIN overflows:

- Increase `BLD_*_FD_SIZE` in `Platform/Tecno/t1101Pkg/PlatformBuild.py`.
- Increase FDF `NumBlocks` in `Platform/Tecno/t1101Pkg/t1101.fdf`.
- Increase BootShim `UEFI_SIZE`.
- Or remove nonessential modules such as `UsbMouseDxe`.

The latest successful build, including the I2C/VBUS code, still fit with only 48 bytes free:

```text
FVMAIN [99%Full] 6699008 (0x663800) total, 6698960 (0x6637d0) used, 48 (0x30) free
```

## 7. Completed work

### Toolchain and build fixes

- Built local toolchain.
- Patched local-only build issues.
- Produced working DEBUG and RELEASE images.
- Patched `Conf/tools_def.txt`.
- Patched local `GenFw`.
- Patched BaseTools GCC support.
- Disabled nasm ext-dep.
- Removed `-g` and `-flto`.
- The `Common/edk2` submodule is locally dirty (` m Common/edk2`). These are local-only build fixes.

### Display / shell

- Restored stock Android boot from `/home/yutaka/edk2-mtk/Build/boot-stock.img`.
- User also recovered with Penumbra preloader tool.
- Fixed display issues:
  - Live FB base.
  - Literal-pool corruption.
  - Cache clean.
  - Layer clearing.
  - 1216-pixel stride.
- Latest verified DEBUG image reached native `UEFI Interactive Shell v2.2`.
- README updated with verified shell status.

### USB stack investigation

- Confirmed no USB host controller driver exists in the repo/submodules.
- FDF originally only had:
  - `UsbBusDxe`
  - `UsbMouseDxe`
  - `UsbMassStorageDxe`
- Added `UsbKbDxe` to `t1101.fdf`.
- Commit `fffb3a7` pushed to `t1101-support`.
- Research found no T1101 ACPI `.asl`/`.aml`, no MT6375/VBUS/TCPC/role-switch code, and no DTB-to-ACPI converter.
- `AcpiPlatformDxe` exists but has no board tables to install.

### MUSB host driver

Created:

- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/Drivers/MusbHostDxe/MusbHostDxe.c`
- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/Drivers/MusbHostDxe/MusbHostDxe.inf`

Wired into:

- `Platform/Tecno/t1101Pkg/t1101.dsc`
- `Platform/Tecno/t1101Pkg/t1101.fdf`

Ordering: `MusbHostDxe.inf` is before `UsbBusDxe` in the FDF.

The first version of this driver built successfully as part of the experimental image with MD5 `6f6d1fc421f237db7bce3de9b788de09`.

### Commits already pushed

- `0998bac`
- `68e1547`
- `0950065`
- `d370c8e`
- `4ad2d13`
- `d1a807f`
- `01ff06c`
- `fffb3a7`

## 8. Current state (important)

All board-local MUSB/I2C/VBUS source changes and this worklog are committed locally. The
`Common/edk2` dirty state remains local-only and is required for this build environment.

### Critical warning

The current source **has been rebuilt successfully**, but the resulting image has **not yet been
tested on hardware**.

The following parts are still experimental and hardware-unverified:

- I2C register and DMA definitions.
- I2C PIO/DMA read/write helpers.
- MT6375 VBUS enable sequence.
- Root hub port count change.
- Port reset reinitialization.
- RX FIFO flush fix.
- Bulk/interrupt endpoint remapping.
- Debug messages.

Flash the test image only when ready to collect DEBUG and shell output. Keep the known-good
DEBUG image for fallback.

## 9. Changes made in the latest MUSB/VBUS editing pass

All changes below are in:

`/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/Drivers/MusbHostDxe/MusbHostDxe.c`

### Includes

Added:

```c
#include <Library/CacheMaintenanceLib.h>
```

Reason: I2C DMA buffers need `WriteBackDataCacheRange()` and `InvalidateDataCacheRange()`.

### MT6983 I2C definitions

Added definitions for:

- `MTK_I2C_BASE = 0x11017000ULL`
- `MTK_I2C_DMA_BASE = 0x10217400ULL`
- `MTK_I2C_PMU_SLAVE = 0x34`
- I2C v2 register offsets
- `MTK_I2C_START = 0x24`
- `MTK_I2C_SLAVE_ADDR1 = 0x94`
- DMA register offsets and control bits
- Interrupt status bits

Also added:

```c
typedef union {
  UINT8   Bytes[16];
  UINT64  Alignment[2];
} MUSB_I2C_BUFFER;

STATIC MUSB_I2C_BUFFER mI2cTxBuf;
STATIC MUSB_I2C_BUFFER mI2cRxBuf;
```

These are static global buffers inside the firmware image, so their physical addresses are below 4 GB and suitable for the AP DMA controller.

### I2C helper functions

Added:

- `MtkI2cRead16()`
- `MtkI2cWrite16()`
- `MtkI2cInitHardware()`
- `MtkI2cWaitComplete()`
- `MtkI2cWriteBytes()`
- `MtkI2cReadBytes()`
- `MtkI2cRead8()`
- `MtkI2cWrite8()`

Notes:

- `MtkI2cInitHardware()` preserves timing-related registers programmed by LK:
  - `TIMING`
  - `EXT_CONF`
  - `LTIMING`
  - `HS`
  - `IO_CONFIG`
  - `SDA_TIMING`
  - `CLOCK_DIV`
  - `SCL_MIS_COMP`
  - `DELAY_LEN`
- It then resets DMA and controller handshake bits and restores timing.
- Writes use a single write transaction with 2-byte big-endian register address followed by data.
- Reads use a write-then-read combined transaction with:
  - `TRANSFER_LEN = 2`
  - `TRANSFER_LEN_AUX = Length`
  - `TRANSAC_LEN = 2`
  - `CONTROL_DIR_CHANGE | CONTROL_RS`
  - `RS_MUL_TRIG | RS_MUL_CNFG`
- Cache is cleaned before DMA TX and invalidated after DMA RX.

### MT6375 VBUS enable

Added:

```c
MtkEnableUsbVbus()
```

It performs the sequence from section 2.

Current implementation:

- Writes `0x69` to `0x107`.
- Applies boost fields:
  - `0x121`: mask `0xC0`, value `0x00`
  - `0x135`: mask `0x03`, value `0x03`
  - `0x140`: mask `0xC0`, value `0xC0`
  - `0x129`: mask `0x80`, value `0x00`
- Writes `0x1A` to `0x131` for 5.00 V.
- Writes `0x06` to `0x132` for 2.4 A.
- Reads `0x120`, sets `0x04`, clears `0x08`, writes back.
- Waits 50 ms.
- Reads back `0x120` and prints a DEBUG message.

### MUSB device state

Added field to `MUSB_DEV`:

```c
BOOLEAN VbusEnabled;
```

### `MusbInitHardware()`

Added:

```c
EFI_STATUS Status;
```

Added VBUS enable on first initialization:

```c
if (!mMusb.VbusEnabled) {
  Status = MtkEnableUsbVbus ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN,
            "MusbHostDxe: MT6375 VBUS enable failed (%r)\n", Status));
  } else {
    mMusb.VbusEnabled = TRUE;
  }
}
```

Failure is non-fatal so enumeration can still be attempted and DEBUG output can show what happens.

### New connection helpers

Added:

- `MusbPollConnected()`
- `MusbUpdateSpeed()`

`MusbPollConnected()` checks both:

- `MUSB_INTRUSB & MUSB_INTR_CONNECT`
- `MUSB_DEVCTL` host mode and VBUS valid

`MusbUpdateSpeed()` maps:

- `LSDEV` → `EFI_USB_SPEED_LOW`
- `FSDEV` → `EFI_USB_SPEED_FULL`
- `HSMODE` → `EFI_USB_SPEED_HIGH`
- default → `EFI_USB_SPEED_HIGH`

`MusbInitHardware()` now uses these helpers instead of duplicating inline logic.

### Root hub port numbering

EDK2 `UsbBusDxe` uses zero-based port indexes:

- `UsbRootHubEnumeration()` loops `Index = 0; Index < RootHub->NumOfPort; Index++`
- `UsbEnumeratePort()` receives `Port` starting at zero.
- TT port number is converted to 1-based only later.

Therefore the root hub now reports one port:

```c
1,  /* NumPorts */
0   /* NumPorts64 */
```

Previously it reported two ports while only accepting `PortNumber == 0`.

### Root hub DEBUG

`MusbGetRootHubPortStatus()` now prints at `DEBUG_VERBOSE`:

```text
MusbHostDxe: root port status=.... change=....
```

### Port reset handling

`EfiUsbPortReset` now:

1. Asserts the port bit in `MUSB_POWER`.
2. Waits 50 ms.
3. Clears the bit.
4. Waits 10 ms.
5. Sets `mMusb.Initialized = FALSE`.
6. Calls `MusbInitHardware()` again.
7. Marks the port enabled.
8. Sets `USB_PORT_STAT_C_RESET`.

Added `EFI_STATUS Status;` to `MusbSetRootHubPortFeature()`.

Reason: Linux/U-Boot MUSB reset generally needs controller reinitialization after the reset pulse.

### RX FIFO flush fix

Old code set `MUSB_RXCSR_RXPKTRDY` while also setting `FLUSHFIFO`.

New code clears `RXPKTRDY` while setting `FLUSHFIFO`:

```c
Csr = (UINT16) ((MusbRead16 (MUSB_RXCSR) & MUSB_RXCSR_H_WZC_BITS) &
                ~MUSB_RXCSR_RXPKTRDY);
Csr |= MUSB_RXCSR_FLUSHFIFO;
MusbWrite16 (MUSB_RXCSR, Csr);
MusbWrite16 (MUSB_RXCSR, Csr);
```

### Bulk transfer fix

Old code looped over `DataBuffersNumber` but reused the same `Length` for every buffer and hard-coded hardware endpoint 1.

New behavior:

- Reject `DataBuffersNumber != 1` with `EFI_UNSUPPORTED`.
- Initialize `Length = *DataLength`.
- Bulk IN uses hardware endpoint 1.
- Bulk OUT uses hardware endpoint 2.
- Removed unused `Index` variable.

Reason: EDK2 `UsbIoBulkTransfer()` always passes `BufNum = 1`, so rejecting multi-buffer callers is safer than transferring the wrong length.

### Interrupt endpoint remapping

Changed:

- Synchronous interrupt IN: hardware endpoint 3.
- Async interrupt IN allocation: hardware endpoints 4 through 7.

Previously synchronous and async both used endpoint 2 or started from 2, which could collide.

Intended endpoint map:

| Hardware EP | Use |
|---:|---|
| 0 | Control |
| 1 | Bulk IN |
| 2 | Bulk OUT |
| 3 | Sync interrupt IN |
| 4–7 | Async interrupt IN |

## 10. Verification status and checklist

### Build verification — completed 2026-11-25

The current source was rebuilt with the command in section 6. `MusbHostDxe.c` compiled cleanly,
the image linked, and FVMAIN fit with 40 bytes free after `UsbMouseDxe` was removed from the FDF.
Linker warnings about RWX LOAD segments are expected for EDK2 modules with this GNU linker setup.

Produced test image:

```text
/home/yutaka/edk2-mtk/Build/boot-t1101-vbus-dma-fix.img
MD5:    7049a620f6f10449e0958aa8998c088c
SHA256: 68b1cc5d88ef9d824d72ee23739911c27d182b86af985fca700bf564a75bb179
URL:    https://github.com/Muchyutaka/edk2-mtk/releases/download/t1101-uefi-test/boot-t1101-vbus-dma-fix.img
```

Earlier hardware-tested images:

- `boot-t1101-musb-test.img` — MD5 `9bb98f106eaee16e8d6ae241f7399256`, I2C `ACKERR`.
- `boot-t1101-vbus-i2c-fix.img` — MD5 `f84d8d33e91cd5d78854748a301651e5`, I2C timeout.

### Hardware verification

Flash only after a clean build. On boot, look for DEBUG lines like:

```text
MusbHostDxe: MT6375 OTG VBUS enabled (TOP1=xx)
MusbHostDxe: HWVERS=.. EPINFO=.. RAMINFO=.. CONFIGDATA=..
MusbHostDxe: connect=yes devctl=.. power=.. speed=.. vbus=..
MusbHostDxe: reset root port
MusbHostDxe: root port status=.... change=....
```

Then in the shell ask the user for:

```text
map -r
map
drivers
```

A still photo is better than OCR if possible.

### Success criteria

USB is working when one of these appears:

- A USB flash drive appears as a new `FSx:` or `BLKx:` device after `map -r`.
- A USB keyboard responds in the shell.
- `drivers` shows `USB Keyboard`, `USB Mass Storage`, or `Usb Io` devices attached to the MUSB controller.

`FS0`/`FS1`/`BLK0` from the known-good image are not USB devices.

## 11. Remaining blockers

### USB

- No hardware-tested `EFI_USB2_HC_PROTOCOL`/`UsbIo` yet.
- Hub works under Android but not yet under UEFI.
- The first MUSB image reached the shell and reported `connect=no`; no device was powered.
- VBUS via MT6375 failed on hardware with I2C `stat=0003` (`COMP | ACKERR`).
- The likely cause was sending both bytes of the 16-bit vendor register number on the I2C bus;
  only the low byte is the wire register for the PMU bank.
- The current PIO-based image has been built but not yet hardware-tested.

### Windows runtime

Even after USB works, native Windows 11 ARM64 will likely need:

- ACPI tables for USB, display, touch, and buttons.
- HID/touch drivers.
- Secure Boot / TPM behavior.
- GPU / WDDM support.
- Vendor `_HID` for Windows USB controllers.
- `_CID "PNP0D20"` only for EHCI-compatible hardware.
- `_CID "PNP0D10"` only for XHCI-compatible hardware.
- Closest in-tree ACPI templates:
  - `Common/edk2-platforms/Platform/ARM/JunoPkg/ConfigurationManager/ConfigurationManagerDxe/AslTables/SsdtJunoUsb.asl`
  - `Common/edk2-platforms/Platform/RaspberryPi/AcpiTables/Xhci.asl`

### Firmware services

- `HAS_BROKEN_TIMER=TRUE` sets `PcdPlatformBootTimeOut=0`.
- Variable services are fake.
- `MT6358RtcLib SetTime` returns `EFI_UNSUPPORTED`.

## 12. Next steps, ordered

1. Keep MD5 `1baca0653cf68a2539772e58562f7550` as the known-good fallback.
2. Flash the new test image once with hub, keyboard, and flash drive attached:
   ```bash
   fastboot flash boot /home/yutaka/edk2-mtk/Build/boot-t1101-vbus-dma-fix.img
   fastboot reboot
   ```
3. Collect the DEBUG lines listed in section 10 and the shell output from:
   ```text
   map -r
   map
   drivers
   ```
   A still photo is better than Google Lens OCR when possible.
4. If VBUS DEBUG fails, verify I2C first:
   - Confirm `MTK_I2C_SLAVE_ADDR1 = 0x94` (`0x34 << 1`).
   - Confirm I2C5 clocks: `impc=...`, `ifrao0=...`, `ifrao5=...`.
   - Confirm only the low byte of each MT6375 register number is sent.
   - Confirm the error-register dump from `MtkI2cWaitComplete()`.
   - Confirm MT6375 slave `0x34`.
5. If VBUS succeeds but no connect is seen, verify PHY recover and `DEVCTL_SESSION`.
6. If connect is seen but enumeration fails, inspect EP0 control transfer DEBUG and CSR0 handling.
7. If keyboard enumerates but flash drive does not, inspect bulk endpoint mapping and toggles.
8. Restore the known-good DEBUG image if the shell fails to start or becomes unusable.
9. Only after USB works, consider ACPI for Windows.

## 13. Files of interest

### Actively modified

- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/Drivers/MusbHostDxe/MusbHostDxe.c`
- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/Drivers/MusbHostDxe/MusbHostDxe.inf`
- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/t1101.dsc`
- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/t1101.fdf`

### Board/build

- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/PlatformBuild.py`
- `/home/yutaka/edk2-mtk/BootShim/BootShim.S`
- `/home/yutaka/edk2-mtk/BootShim/Makefile`
- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c`
- `/home/yutaka/edk2-mtk/Platform/Tecno/t1101Pkg/README.md`

### EDK2 USB

- `/home/yutaka/edk2-mtk/Common/edk2/MdeModulePkg/Bus/Usb/UsbBusDxe/UsbHub.c`
- `/home/yutaka/edk2-mtk/Common/edk2/MdeModulePkg/Bus/Usb/UsbBusDxe/UsbEnumer.c`
- `/home/yutaka/edk2-mtk/Common/edk2/MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBus.c`
- `/home/yutaka/edk2-mtk/Common/edk2/MdeModulePkg/Bus/Usb/UsbBusDxe/UsbUtility.c`
- `/home/yutaka/edk2-mtk/Common/edk2/MdePkg/Include/Protocol/Usb2HostController.h`

### ACPI templates

- `Common/edk2-platforms/Platform/ARM/JunoPkg/ConfigurationManager/ConfigurationManagerDxe/AslTables/SsdtJunoUsb.asl`
- `Common/edk2-platforms/Platform/RaspberryPi/AcpiTables/Xhci.asl`

### Extracted/reference data

- `/home/yutaka/stock-extract/vb/dtb` — real DTB starts at offset `0x40`
- `/tmp/opencode/t1101.dts` — extracted DTB blob using `dd ... skip=64`
- `/tmp/opencode/parse_dtb.py` — simple DTB property parser used in this session
- `/tmp/opencode/musb_host.c` — Linux MUSB host reference
- `/tmp/opencode/mt6375-mfd.c` — MT6375 MFD regmap reference
- `/tmp/opencode/i2c-mt6983.c` — MT6983 I2C driver reference
- `/tmp/opencode/uboot-mtk-i2c.c` — U-Boot MTK I2C reference

## 14. One-line status

The native UEFI shell is working on hardware with MD5 `1baca0653cf68a2539772e58562f7550`; two VBUS attempts failed on hardware, and the corrected DMA-based one-byte-register test image is ready at MD5 `7049a620f6f10449e0958aa8998c088c`, but USB support remains hardware-unverified.

## 15. MT6375 I2C/VBUS correction — 2026-11-25

### Hardware result from the previous image

`boot-t1101-musb-test.img` reached the shell, so the base firmware is still healthy. The new driver
reported:

```text
MusbHostDxe: I2C error stat=0003
MusbHostDxe: MT6375 VBUS enable failed (Device Error)
MusbHostDxe: connect=no devctl=99 power=E0 speed=2 vbus=3
```

`stat=0003` means `I2C_TRANSAC_COMP | I2C_ACKERR`, so the MT6375 NACKed an I2C access and VBUS
never appeared.

### Root cause found in the vendor driver

The Linux MT6375 MFD driver uses 16-bit register numbers, but the high byte is a bank selector,
not part of the I2C register address:

- Bank 0: TCPC, slave `0x4E`
- Bank 1: PMU, slave `0x34`
- Bank 2: BM, slave `0x1A`
- Bank 3: HK1, slave `0x4A`
- Bank 4: HK2, slave `0x64`
- Bank 5: TM, slave `0x3F`

For the PMU bank, `i2c_smbus_write_i2c_block_data()` / `i2c_smbus_read_i2c_block_data()` use only
the low byte. The old UEFI code sent both bytes (`0x01`, then `0x20` etc.), which is the wrong wire
format and explains the NACK.

### Changes in this pass

- Send only `(UINT8) Register` on the I2C bus for all MT6375 accesses.
- Converted MT6375 register accesses from AP DMA to FIFO PIO, matching the vendor driver for
  transfers of 16 bytes or less.
- Aligned transfer setup with the vendor MT6983 driver:
  - write: `CONTROL = ACKERR | CLK_EXT`, `SLAVE_ADDR1 = 0x34 << 1`, `TRANSFER_LEN = Length + 1`,
    `TRANSAC_LEN = 1`, `START = BIT0`
  - read: `CONTROL = ACKERR | CLK_EXT | DIR_CHG | RS`, `TRANSFER_LEN = 1`,
    `TRANSFER_LEN_AUX = Length`, `TRANSAC_LEN = 2`, `START = BIT0`
- Enabled the I2C5 clock chain explicitly:
  - `IMP_IIC_WRAP_C` at `0x1101B000`, set offset `0xE08`, bit `1`
  - `infracfg_ao` at `0x10001000`, I2C pseudo bit `11` at `0x80`, AP DMA bit `31` at `0xD0`
- Added a detailed register dump on I2C error: `SLAVE_ADDR1`, `CONTROL`, `TRANSFER_LEN`,
  `TRANSFER_LEN_AUX`, `TRANSAC_LEN`, `FIFO_STAT`, `TIMING`, `EXT_CONF`, and `CLOCK_DIV`.
- Removed `UsbMouseDxe.inf` from `t1101.fdf` to keep FVMAIN within the available space.

### Image from this pass

```text
/home/yutaka/edk2-mtk/Build/boot-t1101-vbus-i2c-fix.img
MD5:    f84d8d33e91cd5d78854748a301651e5
SHA256: ed5b96ccce085d1ac55996b076121e27d2f6173e92d14e1e51a4e331b3cbff22
FVMAIN: 40 bytes free
```

This image was hardware-tested and is described in section 16.

## 16. Hardware result for the PIO image and DMA fallback — 2026-11-25

### User report decoded

The user supplied a description generated from stills/OCR rather than copied text. After accounting
for OCR errors, the reported lines map to this firmware:

```text
MusbHostDxe: I2C clocks impc=0x00000002 ifrao0=0x00000000 ifrao5=0x00000000
MusbHostDxe: I2C timeout stat=0000
MusbHostDxe: MT6375 VBUS enable failed (Time out)
MusbHostDxe: HWVERS=.. EPINFO=.. RAMINFO=.. CONFIGDATA=3f
MusbHostDxe: FIFO config ends at 0x1ec0
MusbHostDxe: connect=no ...
MusbHostDxe: no device detected on USB2 OTG port (Device Error)
```

Notes:

- `AuthBootSet`/`MusbHostInit` are OCR distortions of `MusbHostDxe`.
- `input`/`ifraud`/`ifrad` are OCR distortions of `impc`/`ifrao0`/`ifrao5`.
- `I2C license status=0000` is almost certainly `I2C timeout stat=0000`.
- `HSIC VBUS enable failed` is almost certainly `MT6375 VBUS enable failed`.

The shell still started and all EDK2 USB drivers loaded, so this is not a firmware-wide regression.

One inconsistency: the report mentions `Loading driver ... UsbMouseDxe.efi`. That module is not in
FVMAIN for `boot-t1101-vbus-i2c-fix.img` or `boot-t1101-vbus-dma-fix.img`; its GUID
`2D2E62AA-9ECF-43B7-8219-94E7FC713DFE` is absent from `FVMAIN.Fv.txt`, while `UsbKbDxe` is
present. If that line is real rather than OCR noise, the device booted an older image. Re-flash the
exact file named in section 16 before judging this result.

### Interpretation

The clock dump shows `impc=0x00000002`, so the I2C5 main gate was enabled. The `ifrao0` and
`ifrao5` values appear to be zero, though OCR may have dropped hex digits. More importantly, the
failure changed from `ACKERR` to a pure timeout: the controller did not report completion or an
error at all.

That means the one-byte register hypothesis is still unproven, but the PIO transfer setup itself
is not working on this board. The previous DMA-based implementation did produce a complete I2C
interrupt with `ACKERR`, so it is the better baseline.

### Change made

Restored the previous DMA-based I2C implementation, while keeping:

- the one-byte MT6375 register fix,
- the explicit I2C5 clock enables,
- the detailed I2C error dump,
- `UsbMouseDxe.inf` removed from the FDF.

The DMA control bits, transfer lengths, and start bits are back to the known-working values from
commit `0645c22`; only the wire register byte count changed from two bytes to one.

### Current image

```text
/home/yutaka/edk2-mtk/Build/boot-t1101-vbus-dma-fix.img
MD5:   7049a620f6f10449e0958aa8998c088c
SHA256: 68b1cc5d88ef9d824d72ee23739911c27d182b86af985fca700bf564a75bb179
FVMAIN: 40 bytes free
```

Flash command:

```bash
fastboot flash boot /home/yutaka/edk2-mtk/Build/boot-t1101-vbus-dma-fix.img
fastboot reboot
```

If this still fails, the next diagnostic should be a still photo (not OCR) of the `I2C clocks`,
`I2C error`, and `VBUS enable` lines, because the exact `ifrao0`/`ifrao5` and `stat` values decide
whether the remaining issue is clock gating or MT6375 register programming.
