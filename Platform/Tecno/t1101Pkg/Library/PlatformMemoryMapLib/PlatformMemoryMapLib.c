#include <Library/BaseLib.h>
#include <Library/PlatformMemoryMapLib.h>

/*
 * Tecno MegaPad 11 (T1101, MT8781) memory map.
 *
 * Copied from the known-good MT6789 emerald layout. SoC-level carveouts
 * (BL31 / TrustZone / SSPM / SCP) are shared between MT6789 and MT8781.
 * The framebuffer window was moved to the T1101 LK value found in the
 * stock DTB: /chosen atag,videolfb-fb_base_l = 0x7e605000,
 * vramSize = 0x01be0000, panel 1200x1920.
 *
 * Framebuffer region size: 1200 * 1920 * 4 * 3 layers = 0x1A5E000.
 */
static ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {
/*                                                    EFI_RESOURCE_ EFI_RESOURCE_ATTRIBUTE_ EFI_MEMORY_TYPE ARM_REGION_ATTRIBUTE_
     MemLabel(32 Char.),  MemBase,    MemSize, BuildHob, ResourceType, ResourceAttribute, MemoryType, CacheAttributes
--------------------- Register ---------------------*/
    {"Periphs",           0x00000000, 0x1B000000,  AddMem, MEM_RES, UNCACHEABLE,  RtCode,   NS_DEVICE},

    //--------------------- DDR --------------------- */
    // 0x040000000 - 0x240000000 (8 GB)
    {"DDR Memory 1",      0x040000000, 0x000C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN}, // 12MB
    {"UEFI Stack",        0x040C00000, 0x000040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},    // 256KB
    {"CPU Vectors",       0x040C40000, 0x000010000, AddMem, SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK},    // 64KB
    {"DXE Heap",          0x040C50000, 0x0075b0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // ~117MB
    {"BL31",              0x048200000, 0x000200000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 2MB
    {"DDR Memory 2",      0x048400000, 0x007c00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 124MB
    {"UEFI FD",           0x050000000, 0x000a00000, AddMem, SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK},    // 10MB
    {"DDR Memory 3",      0x050a00000, 0x016500000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 360MB
    {"RamDisk",           0x066f00000, 0x006000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 96MB (Just in case)
    {"DDR Memory 4",      0x06cf00000, 0x003100000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 49MB
    {"TrustZone Mem",     0x070000000, 0x008800000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 136MB
    {"DDR Memory 5",      0x078800000, 0x005200000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 82MB
    {"TrustZone SHM",     0x07da00000, 0x000400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 4MB
    {"TrustZone SecMem",  0x07de00000, 0x000800000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 8MB
    {"DDR Memory 6",      0x07e600000, 0x000005000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 20KB (up to framebuffer)
    // T1101 framebuffer: 3 layers of 1200x1920x4 = 0x1A5E000
    {"Framebuffer",       0x07e605000, 0x001a5e000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
    {"DDR Memory 7",      0x080003000, 0x00efd0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // up to 0x08f000000
    {"SCP IPC",           0x08f000000, 0x0005b0000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // ~5 MB
    {"DDR Memory 8",      0x08f5b0000, 0x00ffb0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 255MB
    {"SSPM IPC",          0x09f560000, 0x000110000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 1MB
    {"DDR Memory 9",      0x09f670000, 0x000800000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 8MB
    {"SSPM Mem",          0x09fe70000, 0x000180000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 1.5MB
    {"DDR Memory 10",     0x09fff0000, 0x01fa10000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 506MB
    {"SCP Mem",           0x0bfa00000, 0x000400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},    // 4MB
    {"DDR Memory 11",     0x0bfe00000, 0x03e2d0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 995MB
    {"DDR Memory 12",     0x0fffff000, 0x140001000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},    // 5GB

//------------------- Terminator for MMU ---------------------
{"Terminator", 0, 0, 0, 0, 0, 0, 0}};

ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap()
{
  return gDeviceMemoryDescriptorEx;
}
