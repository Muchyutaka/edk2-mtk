[Defines]
  PLATFORM_NAME                  = t1101
  PLATFORM_GUID                  = EFEC5439-60FD-40C0-BC90-DA88AC95699E
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = t1101Pkg/t1101.fdf
  DEVICE_DXE_FV_COMPONENTS       = t1101Pkg/t1101.fdf.inc

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x200000000 # 8GB
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x40C40000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x40C00000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000
  gMediaTekTokenSpaceGuid.PcdUefiMemPoolBase|0x40C50000
  gMediaTekTokenSpaceGuid.PcdUefiMemPoolSize|0x075b0000

  # Simple Framebuffer
  # Tecno T1101 (MT8781): live values read from the running stock firmware
  # at /proc/device-tree/chosen/atag,videolfb-*. LK rewrites these at boot,
  # so the value in the on-disk DTB (0x7e605000) is stale.
  #   fb_base_l   = 0xfe41a000
  #   fb_base_h   = 0x00000000
  #   vramSize    = 0x01ac0000
  #   lcmname     = nt36672e_fhd_dsi_vdo_60hz_jdi_dphy_drv
  # Panel is 1200 wide, but LK pads each row to 1216 pixels (128-byte
  # aligned). 3 x 1216 x 1920 x 4 = 0x1AB4000, which fits the live
  # vramSize 0x1AC0000 with 32 KB left over. Using 1200 here makes every
  # row shift by 16 pixels and text renders as diagonal static.
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferWidth|1216
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferHeight|1920
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferAddress|0xfe41a000

[LibraryClasses]
  PlatformMemoryMapLib|t1101Pkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf

!include Silicon/MediaTek/MT6789Pkg/MT6789.dsc
