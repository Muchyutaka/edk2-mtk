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
  # Tecno T1101 (MT8781): values from stock DTB /chosen atag,videolfb-*
  #   fb_base_l   = 0x7e605000
  #   vramSize    = 0x01be0000
  #   lcmname     = mt63672e_fhd_dsi_vdo_60hz_idi_dphy_drv
  # 1200x1920 x 4 bytes x 3 layers = 0x1A5E000
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferWidth|1200
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferHeight|1920
  gMediaTekTokenSpaceGuid.PcdMipiFrameBufferAddress|0x7e605000

[LibraryClasses]
  PlatformMemoryMapLib|t1101Pkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf

!include Silicon/MediaTek/MT6789Pkg/MT6789.dsc
