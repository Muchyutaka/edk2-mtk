# Tecno MegaPad 11 (T1101) EDK2 board package

SoC: MediaTek MT8781 (Helio G99) — same silicon as MT6789, so this package
reuses `Silicon/MediaTek/MT6789Pkg` unchanged.

## Hardware facts used

| Item | Value | Source |
| --- | --- | --- |
| Panel | 1200 x 1920, IPS 90 Hz | stock specs / DTB |
| LCM | `mt63672e_fhd_dsi_vdo_60hz_idi_dphy_drv` | DTB `/chosen atag,videolfb-lcmname` |
| Framebuffer base | `0x7e605000` | DTB `/chosen atag,videolfb-fb_base_l` |
| LK VRAM size | `0x01be0000` | DTB `/chosen atag,videolfb-vramSize` |
| UEFI FB size | `0x1a5e000` | 1200 x 1920 x 4 B x 3 layers |
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

Note: use `RELEASE`. `DEBUG` with modern clang generates ELF sections that
the old `GenFw` in this repo cannot convert (>1 MB section alignment limit).

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
