# Tecno MegaPad 11 (T1101) EDK2 board package

SoC: MediaTek MT8781 (Helio G99) — same silicon as MT6789, so this package
reuses `Silicon/MediaTek/MT6789Pkg` unchanged.

## Hardware facts used

| Item | Value | Source |
| --- | --- | --- |
| Panel | 1200 x 1920, IPS 60 Hz | running stock firmware |
| LCM | `nt36672e_fhd_dsi_vdo_60hz_jdi_dphy_drv` | running `/proc/device-tree/chosen/atag,videolfb-lcmname` |
| Framebuffer base | `0xfe41a000` | running `/proc/device-tree/chosen/atag,videolfb-fb_base_l` (LK rewrites this; on-disk DTB value `0x7e605000` is stale) |
| LK VRAM size | `0x01ac0000` | running `/proc/device-tree/chosen/atag,videolfb-vramSize` |
| UEFI FB stride | `1216` pixels | inferred from live `vramSize`: 1216 x 1920 x 4 B x 3 layers = `0x1ab4000` (+32 KB padding); panel itself is 1200 wide |
| UEFI FB size | `0x1ac0000` | live LK VRAM size |
| DRAM | 8 GB @ `0x40000000` | device specs, emerald layout |
| RAM disk | 96 MB @ `0x66f00000` | inherited from emerald |
| UEFI FD | 10 MB @ `0x50000000` | inherited from emerald |

## Build

Requirements:
- Python 3 with `edk2toolext` + `edk2toollib` (from tianocore GitHub repos)
- `aarch64-linux-gnu-gcc` + `aarch64-linux-gnu-objcopy` (for BootShim)
- `iasl` (ACPI compiler)
- `clang` + AArch64-capable `ld` (GNU `aarch64-linux-gnu-ld` recommended;
  `ld.lld` is too strict about EDK2's absolute relocations)
- `mkbootimg`

```sh
./build.sh -s -t RELEASE Tecno/t1101Pkg    # one-time setup
./build.sh -t RELEASE Tecno/t1101Pkg
```

Output: `Build/boot-t1101.img` (mkbootimg, header version 4).

DEBUG builds include an early BootShim framebuffer self-test using the live
LK `fb_base`: the screen fills red with a green 1200x240 block at the top-left,
the range is cleaned to DRAM with `dc civac`, and it is held briefly before
EDK2 starts. RELEASE builds skip this test. This is compile-gated by
`T1101_FB_TEST`, so other board packages are unaffected.

The local toolchain setup used for these builds patches `GenFw` for modern
clang output; a clean checkout may need that same local-only fix to rebuild
DEBUG.

## Status

Verified on hardware: the DEBUG image reaches `UEFI Interactive Shell v2.2`
with working framebuffer console output at `0xfe41a000`, 1216-pixel stride.
The shell exposes the UEFI FVs as `FS0`/`FS1` and a 96 MB ramdisk as `BLK0`.

## Flash

Back up `preloader`, `boot`, `nvram`/`nvdata`, `vbmeta` first.

```sh
fastboot flash boot Build/boot-t1101.img
```

This replaces the stock LK in `boot`; Android will not boot until the stock
`boot.img` is restored. If your stock boot image uses a different header
version, adjust `--header_version` in `PlatformBuild.py`.

## Known gaps for Windows 11

This port currently targets a bootable UEFI + framebuffer (Linux-first, like
upstream edk2-mtk). Windows on ARM additionally needs ACPI tables describing
the display, touch controller, USB, buttons, etc. That work is not included
here yet.
