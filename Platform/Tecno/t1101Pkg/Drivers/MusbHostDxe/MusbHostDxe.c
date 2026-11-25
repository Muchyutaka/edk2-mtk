/** @file
  Minimal MediaTek MUSB USB2 host controller driver for Tecno T1101.

  This produces EFI_USB2_HC_PROTOCOL for the USB2 OTG controller at 0x11200000.
  It is a small polling PIO implementation based on the register programming
  used by the Linux/U-Boot Inventra MUSB host driver.  It supports control,
  bulk and interrupt IN transfers, which is enough for USB keyboards and
  USB mass-storage boot.

  Copyright (c) 2026, t1101Pkg contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/UsbIo.h>

#define MUSB_BASE               0x11200000ULL
#define USB20_PHY_BASE          0x11110800ULL
#define MTK_MUSB_TARGET_BASE    0x0480

#define MUSB_FADDR              0x00
#define MUSB_POWER              0x01
#define MUSB_INTRTX             0x02
#define MUSB_INTRRX             0x04
#define MUSB_INTRTXE            0x06
#define MUSB_INTRRXE            0x08
#define MUSB_INTRUSB            0x0A
#define MUSB_INTRUSBE           0x0B
#define MUSB_FRAME              0x0C
#define MUSB_INDEX              0x0E
#define MUSB_TESTMODE           0x0F

#define MUSB_FIFO_OFFSET(e)     (0x20 + ((e) * 4))

#define MUSB_DEVCTL             0x60
#define MUSB_TXFIFOSZ           0x62
#define MUSB_RXFIFOSZ           0x63
#define MUSB_TXFIFOADD          0x64
#define MUSB_RXFIFOADD          0x66
#define MUSB_HWVERS             0x6C
#define MUSB_EPINFO             0x78
#define MUSB_RAMINFO            0x79

#define MUSB_TXMAXP             0x10
#define MUSB_TXCSR              0x12
#define MUSB_CSR0               0x12
#define MUSB_RXMAXP             0x14
#define MUSB_RXCSR              0x16
#define MUSB_RXCOUNT            0x18
#define MUSB_COUNT0             0x18
#define MUSB_TXTYPE             0x1A
#define MUSB_TYPE0              0x1A
#define MUSB_TXINTERVAL         0x1B
#define MUSB_NAKLIMIT0          0x1B
#define MUSB_RXTYPE             0x1C
#define MUSB_RXINTERVAL         0x1D
#define MUSB_FIFOSIZE           0x1F
#define MUSB_CONFIGDATA         0x1F

#define MUSB_RXTOG              0x80
#define MUSB_RXTOGEN            0x82
#define MUSB_TXTOG              0x84
#define MUSB_TXTOGEN            0x86
#define USB_L1INTS              0xA0
#define USB_L1INTM              0xA4

//
// MT6983 I2C controller used by the MT6375 PMIC on the T1101.  These are
// the v2 registers with the MediaTek slave-address expansion at 0x94.
//
#define MTK_I2C_BASE             0x11017000ULL
#define MTK_I2C_DMA_BASE         0x10217400ULL
#define MTK_I2C_PMU_SLAVE        0x34

#define MTK_I2C_DATA_PORT        0x00
#define MTK_I2C_INTR_MASK        0x08
#define MTK_I2C_INTR_STAT        0x0C
#define MTK_I2C_CONTROL          0x10
#define MTK_I2C_TRANSFER_LEN     0x14
#define MTK_I2C_TRANSAC_LEN      0x18
#define MTK_I2C_DELAY_LEN        0x1C
#define MTK_I2C_TIMING           0x20
#define MTK_I2C_START            0x24
#define MTK_I2C_EXT_CONF         0x28
#define MTK_I2C_LTIMING          0x2C
#define MTK_I2C_HS               0x30
#define MTK_I2C_IO_CONFIG        0x34
#define MTK_I2C_FIFO_ADDR_CLR    0x38
#define MTK_I2C_SDA_TIMING       0x3C
#define MTK_I2C_TRANSFER_LEN_AUX 0x44
#define MTK_I2C_CLOCK_DIV        0x48
#define MTK_I2C_SOFTRESET        0x50
#define MTK_I2C_SLAVE_ADDR1      0x94
#define MTK_I2C_SCL_MIS_COMP     0x90
#define MTK_I2C_FIFO_STAT        0xF4
#define MTK_I2C_FIFO_THRESH      0xF8

#define MTK_I2C_DMA_INT_FLAG     0x00
#define MTK_I2C_DMA_EN           0x08
#define MTK_I2C_DMA_RST          0x0C
#define MTK_I2C_DMA_CON          0x18
#define MTK_I2C_DMA_TX_ADDR      0x1C
#define MTK_I2C_DMA_RX_ADDR      0x20
#define MTK_I2C_DMA_TX_LEN       0x24
#define MTK_I2C_DMA_RX_LEN       0x28

#define MTK_I2C_INTR_COMP        BIT0
#define MTK_I2C_INTR_ACKERR      BIT1
#define MTK_I2C_INTR_ARBLOST     BIT3
#define MTK_I2C_INTR_RS          BIT4
#define MTK_I2C_INTR_HS_ACKERR   BIT2
#define MTK_I2C_INTR_ALL         (BIT0 | BIT1 | BIT2 | BIT3 | BIT4)

#define MTK_I2C_CONTROL_RS       BIT1
#define MTK_I2C_CONTROL_DMA_EN   BIT2
#define MTK_I2C_CONTROL_CLK_EXT  BIT3
#define MTK_I2C_CONTROL_DIR_CHG  BIT4
#define MTK_I2C_CONTROL_ACKERR   BIT5
#define MTK_I2C_CONTROL_DMAACK   BIT8
#define MTK_I2C_CONTROL_ASYNC    BIT9

#define MTK_I2C_SOFT_RST         BIT0
#define MTK_I2C_HANDSHAKE_RST    BIT5
#define MTK_I2C_FIFO_ADDR_CLR_EN BIT0
#define MTK_I2C_IO_OPEN_DRAIN    0x03
#define MTK_I2C_DELAY_LEN_VALUE  0x02

#define MTK_I2C_RS_MUL_TRIG      BIT14
#define MTK_I2C_RS_MUL_CNFG      BIT15

#define MTK_I2C_DMA_CON_TX       0x00
#define MTK_I2C_DMA_CON_RX       BIT0
#define MTK_I2C_DMA_SKIP_CONFIG  BIT4
#define MTK_I2C_DMA_ASYNC_MODE   BIT2
#define MTK_I2C_DMA_DIR_CHANGE   BIT9
#define MTK_I2C_DMA_START_EN     BIT0
#define MTK_I2C_DMA_WARM_RST     BIT0
#define MTK_I2C_DMA_HARD_RST     BIT1
#define MTK_I2C_DMA_HANDSHAKE_RST BIT2

#define MUSB_POWER_ISOUPDATE    0x80
#define MUSB_POWER_HSENAB       0x20
#define MUSB_POWER_HSMODE       0x10
#define MUSB_POWER_RESET        0x08
#define MUSB_POWER_SUSPENDM     0x02

#define MUSB_INTR_CONNECT       0x10
#define MUSB_INTR_DISCONNECT    0x20

#define MUSB_DEVCTL_VBUS        0x18
#define MUSB_DEVCTL_FSDEV       0x40
#define MUSB_DEVCTL_LSDEV       0x20
#define MUSB_DEVCTL_HM          0x04
#define MUSB_DEVCTL_SESSION     0x01

#define MUSB_CSR0_FLUSHFIFO     0x0100
#define MUSB_CSR0_TXPKTRDY      0x0002
#define MUSB_CSR0_RXPKTRDY      0x0001
#define MUSB_CSR0_H_WR_DATATOGGLE 0x0400
#define MUSB_CSR0_H_DATATOGGLE  0x0200
#define MUSB_CSR0_H_NAKTIMEOUT  0x0080
#define MUSB_CSR0_H_STATUSPKT   0x0040
#define MUSB_CSR0_H_REQPKT      0x0020
#define MUSB_CSR0_H_ERROR       0x0010
#define MUSB_CSR0_H_SETUPPKT    0x0008
#define MUSB_CSR0_H_RXSTALL     0x0004
#define MUSB_CSR0_H_WZC_BITS    (MUSB_CSR0_H_NAKTIMEOUT | \
                                 MUSB_CSR0_H_RXSTALL | \
                                 MUSB_CSR0_RXPKTRDY)

#define MUSB_TXCSR_AUTOSET      0x8000
#define MUSB_TXCSR_DMAENAB      0x1000
#define MUSB_TXCSR_FRCDATATOG   0x0800
#define MUSB_TXCSR_MODE         0x2000
#define MUSB_TXCSR_CLRDATATOG   0x0040
#define MUSB_TXCSR_FLUSHFIFO    0x0008
#define MUSB_TXCSR_FIFONOTEMPTY 0x0002
#define MUSB_TXCSR_TXPKTRDY     0x0001
#define MUSB_TXCSR_H_WR_DATATOGGLE 0x0200
#define MUSB_TXCSR_H_DATATOGGLE 0x0100
#define MUSB_TXCSR_H_NAKTIMEOUT 0x0080
#define MUSB_TXCSR_H_RXSTALL    0x0020
#define MUSB_TXCSR_H_ERROR      0x0004
#define MUSB_TXCSR_H_WZC_BITS   (MUSB_TXCSR_H_NAKTIMEOUT | \
                                 MUSB_TXCSR_H_RXSTALL | \
                                 MUSB_TXCSR_H_ERROR | \
                                 MUSB_TXCSR_FIFONOTEMPTY)

#define MUSB_RXCSR_AUTOCLEAR    0x8000
#define MUSB_RXCSR_DISNYET      0x1000
#define MUSB_RXCSR_CLRDATATOG   0x0080
#define MUSB_RXCSR_FLUSHFIFO    0x0010
#define MUSB_RXCSR_DATAERROR    0x0008
#define MUSB_RXCSR_RXPKTRDY     0x0001
#define MUSB_RXCSR_H_AUTOREQ    0x4000
#define MUSB_RXCSR_H_WR_DATATOGGLE 0x0400
#define MUSB_RXCSR_H_DATATOGGLE 0x0200
#define MUSB_RXCSR_H_REQPKT     0x0020
#define MUSB_RXCSR_H_RXSTALL    0x0040
#define MUSB_RXCSR_H_ERROR      0x0004
#define MUSB_RXCSR_H_WZC_BITS   (MUSB_RXCSR_H_RXSTALL | \
                                 MUSB_RXCSR_H_ERROR | \
                                 MUSB_RXCSR_DATAERROR | \
                                 MUSB_RXCSR_RXPKTRDY)

#define MUSB_TYPE_SPEED_SHIFT   6
#define MUSB_TYPE_PROTO_SHIFT   4
#define MUSB_TYPE_BULK          2
#define MUSB_TYPE_INT           3

#define MUSB_CONFIGDATA_DYNFIFO 0x04

#define L1_TX_INT               0x01
#define L1_RX_INT               0x02
#define L1_USBCOM_INT           0x04

#define MUSB_MAX_HW_EP          8
#define MUSB_TIMEOUT_MS         2000

typedef struct {
  UINT8   Address;
  UINT8   HubAddress;
  UINT8   HubPort;
  UINT8   Endpoint;
  UINT8   Speed;
  UINT8   Type;
  UINT16  MaxPacket;
} MUSB_TARGET;

typedef struct {
  UINT8   Address;
  UINT8   Endpoint;
  UINT8   HwEndpoint;
  UINT8   Speed;
  UINT16  MaxPacket;
  UINTN   Interval;
  UINTN   DataLength;
  UINT8   DataToggle;
  UINT8   HubAddress;
  UINT8   HubPort;
  BOOLEAN Active;
  EFI_ASYNC_USB_TRANSFER_CALLBACK Callback;
  VOID    *Context;
  EFI_EVENT Timer;
} MUSB_ASYNC_INT;

typedef struct {
  UINTN                     Base;
  UINTN                     PhyBase;
  BOOLEAN                   Initialized;
  BOOLEAN                   Connected;
  BOOLEAN                   Enabled;
  BOOLEAN                   PortPower;
  BOOLEAN                   VbusEnabled;
  UINT8                     Speed;
  UINT16                    PortChange;
  MUSB_ASYNC_INT            Async[MUSB_MAX_HW_EP];
  EFI_USB2_HC_PROTOCOL      Usb2Hc;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_HANDLE                ControllerHandle;
} MUSB_DEV;

STATIC MUSB_DEV mMusb;

//
// DMA buffers for the MT6375 register accesses.  Keeping these in the
// firmware image guarantees a physical address below 4 GB for the AP DMA.
//
typedef union {
  UINT8   Bytes[16];
  UINT64  Alignment[2];
} MUSB_I2C_BUFFER;

STATIC MUSB_I2C_BUFFER mI2cTxBuf;
STATIC MUSB_I2C_BUFFER mI2cRxBuf;

//
// Vendor hardware device path for the controller handle.
//
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  EFI_GUID                  Guid;
} VENDOR_DEVICE_PATH_NODE;

STATIC VENDOR_DEVICE_PATH_NODE mMusbDevicePath = {
  {
    HARDWARE_DEVICE_PATH,
    HW_VENDOR_DP,
    { (UINT8) sizeof (VENDOR_DEVICE_PATH_NODE), 0 }
  },
  { 0x9a1d2b34, 0x6f7c, 0x4a1e, { 0x8d, 0x55, 0x71, 0x30, 0x62, 0xa9, 0x4c, 0x11 } }
};

STATIC EFI_GUID gMusbDevicePathGuid = {
  0x9a1d2b34, 0x6f7c, 0x4a1e, { 0x8d, 0x55, 0x71, 0x30, 0x62, 0xa9, 0x4c, 0x11 }
};

STATIC EFI_GUID gMusbHcdDriverGuid = {
  0x51d8e6b0, 0x1a2f, 0x4d3c, { 0x9b, 0x77, 0x21, 0x55, 0x6f, 0x88, 0x30, 0x4e }
};

//
// Low-level register helpers
//
STATIC UINT8
MusbRead8 (
  IN UINTN Offset
  )
{
  return MmioRead8 (mMusb.Base + Offset);
}

STATIC VOID
MusbWrite8 (
  IN UINTN Offset,
  IN UINT8 Value
  )
{
  MmioWrite8 (mMusb.Base + Offset, Value);
}

STATIC UINT16
MusbRead16 (
  IN UINTN Offset
  )
{
  return MmioRead16 (mMusb.Base + Offset);
}

STATIC VOID
MusbWrite16 (
  IN UINTN Offset,
  IN UINT16 Value
  )
{
  MmioWrite16 (mMusb.Base + Offset, Value);
}

STATIC UINT32
MusbRead32 (
  IN UINTN Offset
  )
{
  return MmioRead32 (mMusb.Base + Offset);
}

STATIC VOID
MusbWrite32 (
  IN UINTN Offset,
  IN UINT32 Value
  )
{
  MmioWrite32 (mMusb.Base + Offset, Value);
}

STATIC VOID
MusbSelectEp (
  IN UINT8 Endpoint
  )
{
  MusbWrite8 (MUSB_INDEX, Endpoint);
}

//
// MediaTek MUSB uses dedicated per-endpoint toggle registers instead of
// the DATA0/DATA1 bits in TXCSR/RXCSR.  Keep them in sync with the toggle
// tracked by UsbBusDxe.
//
STATIC VOID
MusbSetHwToggle (
  IN UINT8   Endpoint,
  IN BOOLEAN IsIn,
  IN UINT8   Toggle
  )
{
  UINT16 Offset;
  UINT16 Value;

  Offset = IsIn ? MUSB_RXTOG : MUSB_TXTOG;
  Value = MusbRead16 (Offset);
  if ((Toggle & 1) != 0) {
    Value |= (UINT16) (1U << Endpoint);
  } else {
    Value &= (UINT16) ~(1U << Endpoint);
  }
  MusbWrite16 (Offset, Value);
}

STATIC UINT8
MusbGetHwToggle (
  IN UINT8   Endpoint,
  IN BOOLEAN IsIn
  )
{
  UINT16 Offset;

  Offset = IsIn ? MUSB_RXTOG : MUSB_TXTOG;
  return (UINT8) ((MusbRead16 (Offset) >> Endpoint) & 1);
}

STATIC VOID
MusbClearL1Irq (
  IN UINT32 Bits
  )
{
  UINT32 L1;

  L1 = MusbRead32 (USB_L1INTS);
  if ((L1 & Bits) != 0) {
    MusbWrite32 (USB_L1INTS, L1 & Bits);
  }
}

STATIC VOID
MusbClearEp0Irq (
  VOID
  )
{
  MusbClearL1Irq (L1_TX_INT | L1_USBCOM_INT);
  MusbWrite16 (MUSB_INTRTX, 0x0001);
  MusbWrite16 (MUSB_INTRRX, 0x0001);
}

STATIC EFI_STATUS
MusbWaitMs (
  IN UINTN Milliseconds
  )
{
  if (gBS == NULL) {
    return EFI_DEVICE_ERROR;
  }
  gBS->Stall (Milliseconds * 1000);
  return EFI_SUCCESS;
}

//
// Minimal MT6983 I2C PIO/DMA access for the MT6375 PMIC.  LK has already
// programmed the bus timing (1 MHz, clock-div 1), so preserve those values
// and only reset the DMA/controller handshake before each UEFI transfer.
//
STATIC UINT16
MtkI2cRead16 (
  IN UINTN Offset
  )
{
  return MmioRead16 (MTK_I2C_BASE + Offset);
}

STATIC VOID
MtkI2cWrite16 (
  IN UINTN Offset,
  IN UINT16 Value
  )
{
  MmioWrite16 (MTK_I2C_BASE + Offset, Value);
}

STATIC EFI_STATUS
MtkI2cInitHardware (
  VOID
  )
{
  STATIC CONST UINT16 PreservedRegs[] = {
    MTK_I2C_TIMING, MTK_I2C_EXT_CONF, MTK_I2C_LTIMING, MTK_I2C_HS,
    MTK_I2C_IO_CONFIG, MTK_I2C_SDA_TIMING, MTK_I2C_CLOCK_DIV,
    MTK_I2C_SCL_MIS_COMP, MTK_I2C_DELAY_LEN
  };
  UINT16 Values[sizeof (PreservedRegs) / sizeof (PreservedRegs[0])];
  UINTN Index;

  for (Index = 0; Index < sizeof (PreservedRegs) / sizeof (PreservedRegs[0]); Index++) {
    Values[Index] = MtkI2cRead16 (PreservedRegs[Index]);
  }

  MtkI2cWrite16 (MTK_I2C_CONTROL, 0);
  MtkI2cWrite16 (MTK_I2C_START, 0);
  MtkI2cWrite16 (MTK_I2C_INTR_STAT, 0xFFFF);

  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_RST, MTK_I2C_DMA_WARM_RST);
  gBS->Stall (10);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_RST, 0);
  gBS->Stall (10);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_RST,
               MTK_I2C_DMA_HARD_RST | MTK_I2C_DMA_HANDSHAKE_RST);
  MtkI2cWrite16 (MTK_I2C_SOFTRESET,
                 (UINT16) (MTK_I2C_SOFT_RST | MTK_I2C_HANDSHAKE_RST));
  gBS->Stall (10);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_RST, 0);
  MtkI2cWrite16 (MTK_I2C_SOFTRESET, 0);

  for (Index = 0; Index < sizeof (PreservedRegs) / sizeof (PreservedRegs[0]); Index++) {
    MtkI2cWrite16 (PreservedRegs[Index], Values[Index]);
  }
  MtkI2cWrite16 (MTK_I2C_IO_CONFIG, MTK_I2C_IO_OPEN_DRAIN);
  MtkI2cWrite16 (MTK_I2C_DELAY_LEN, MTK_I2C_DELAY_LEN_VALUE);
  MtkI2cWrite16 (MTK_I2C_CONTROL,
                 (UINT16) (MTK_I2C_CONTROL_ACKERR | MTK_I2C_CONTROL_CLK_EXT |
                           MTK_I2C_CONTROL_DMA_EN | MTK_I2C_CONTROL_DMAACK |
                           MTK_I2C_CONTROL_ASYNC));
  MtkI2cWrite16 (MTK_I2C_INTR_MASK, 0);
  MtkI2cWrite16 (MTK_I2C_START, 0);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
MtkI2cWaitComplete (
  VOID
  )
{
  UINTN  Try;
  UINT16 Status;

  for (Try = 0; Try < 2000; Try++) {
    Status = MtkI2cRead16 (MTK_I2C_INTR_STAT);
    if ((Status & (MTK_I2C_INTR_ACKERR | MTK_I2C_INTR_HS_ACKERR |
                   MTK_I2C_INTR_ARBLOST)) != 0) {
      DEBUG ((DEBUG_ERROR, "MusbHostDxe: I2C error stat=%04x\n", Status));
      return EFI_DEVICE_ERROR;
    }
    if ((Status & MTK_I2C_INTR_COMP) != 0) {
      return EFI_SUCCESS;
    }
    gBS->Stall (100);
  }

  DEBUG ((DEBUG_ERROR, "MusbHostDxe: I2C timeout stat=%04x\n", Status));
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
MtkI2cWriteBytes (
  IN UINT8  SlaveAddress,
  IN UINT16 Register,
  IN CONST UINT8 *Buffer,
  IN UINTN  Length
  )
{
  EFI_STATUS Status;
  UINTN Total;

  if (Length == 0 || Length > (sizeof (mI2cTxBuf.Bytes) - 2)) {
    return EFI_INVALID_PARAMETER;
  }

  Total = Length + 2;
  mI2cTxBuf.Bytes[0] = (UINT8) (Register >> 8);
  mI2cTxBuf.Bytes[1] = (UINT8) Register;
  CopyMem (&mI2cTxBuf.Bytes[2], Buffer, Length);
  WriteBackDataCacheRange (mI2cTxBuf.Bytes, Total);

  MtkI2cWrite16 (MTK_I2C_CONTROL,
                 (UINT16) (MTK_I2C_CONTROL_ACKERR | MTK_I2C_CONTROL_CLK_EXT |
                           MTK_I2C_CONTROL_DMA_EN | MTK_I2C_CONTROL_DMAACK |
                           MTK_I2C_CONTROL_ASYNC | MTK_I2C_CONTROL_RS));
  MtkI2cWrite16 (MTK_I2C_SLAVE_ADDR1, (UINT16) (SlaveAddress << 1));
  MtkI2cWrite16 (MTK_I2C_INTR_STAT, MTK_I2C_INTR_ALL);
  MtkI2cWrite16 (MTK_I2C_FIFO_ADDR_CLR, MTK_I2C_FIFO_ADDR_CLR_EN);
  MtkI2cWrite16 (MTK_I2C_INTR_MASK, MTK_I2C_INTR_ALL);
  MtkI2cWrite16 (MTK_I2C_TRANSFER_LEN, (UINT16) Total);
  MtkI2cWrite16 (MTK_I2C_TRANSAC_LEN, 1);

  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_INT_FLAG, 0);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_CON,
               MTK_I2C_DMA_CON_TX | MTK_I2C_DMA_SKIP_CONFIG |
               MTK_I2C_DMA_ASYNC_MODE);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_TX_ADDR,
               (UINT32) (UINTN) mI2cTxBuf.Bytes);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_TX_LEN, (UINT32) Total);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_EN, MTK_I2C_DMA_START_EN);

  MtkI2cWrite16 (MTK_I2C_START,
                 (UINT16) (BIT0 | MTK_I2C_RS_MUL_TRIG));
  Status = MtkI2cWaitComplete ();

  MtkI2cWrite16 (MTK_I2C_START, 0);
  MtkI2cWrite16 (MTK_I2C_INTR_MASK, 0);
  MtkI2cWrite16 (MTK_I2C_INTR_STAT, MTK_I2C_INTR_ALL);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_EN, 0);
  return Status;
}

STATIC EFI_STATUS
MtkI2cReadBytes (
  IN UINT8  SlaveAddress,
  IN UINT16 Register,
  OUT UINT8 *Buffer,
  IN UINTN  Length
  )
{
  EFI_STATUS Status;

  if (Length == 0 || Length > sizeof (mI2cRxBuf.Bytes)) {
    return EFI_INVALID_PARAMETER;
  }

  mI2cTxBuf.Bytes[0] = (UINT8) (Register >> 8);
  mI2cTxBuf.Bytes[1] = (UINT8) Register;
  ZeroMem (mI2cRxBuf.Bytes, Length);
  WriteBackDataCacheRange (mI2cTxBuf.Bytes, 2);
  InvalidateDataCacheRange (mI2cRxBuf.Bytes, Length);

  MtkI2cWrite16 (MTK_I2C_CONTROL,
                 (UINT16) (MTK_I2C_CONTROL_ACKERR | MTK_I2C_CONTROL_CLK_EXT |
                           MTK_I2C_CONTROL_DMA_EN | MTK_I2C_CONTROL_DMAACK |
                           MTK_I2C_CONTROL_ASYNC | MTK_I2C_CONTROL_DIR_CHG |
                           MTK_I2C_CONTROL_RS));
  MtkI2cWrite16 (MTK_I2C_SLAVE_ADDR1, (UINT16) (SlaveAddress << 1));
  MtkI2cWrite16 (MTK_I2C_INTR_STAT, MTK_I2C_INTR_ALL);
  MtkI2cWrite16 (MTK_I2C_FIFO_ADDR_CLR, MTK_I2C_FIFO_ADDR_CLR_EN);
  MtkI2cWrite16 (MTK_I2C_INTR_MASK, MTK_I2C_INTR_ALL);
  MtkI2cWrite16 (MTK_I2C_TRANSFER_LEN, 2);
  MtkI2cWrite16 (MTK_I2C_TRANSFER_LEN_AUX, (UINT16) Length);
  MtkI2cWrite16 (MTK_I2C_TRANSAC_LEN, 2);

  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_INT_FLAG, 0);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_CON,
               MTK_I2C_DMA_SKIP_CONFIG | MTK_I2C_DMA_ASYNC_MODE |
               MTK_I2C_DMA_DIR_CHANGE);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_TX_ADDR,
               (UINT32) (UINTN) mI2cTxBuf.Bytes);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_TX_LEN, 2);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_RX_ADDR,
               (UINT32) (UINTN) mI2cRxBuf.Bytes);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_RX_LEN, (UINT32) Length);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_EN, MTK_I2C_DMA_START_EN);

  MtkI2cWrite16 (MTK_I2C_START,
                 (UINT16) (BIT0 | MTK_I2C_RS_MUL_TRIG | MTK_I2C_RS_MUL_CNFG));
  Status = MtkI2cWaitComplete ();

  InvalidateDataCacheRange (mI2cRxBuf.Bytes, Length);
  MtkI2cWrite16 (MTK_I2C_START, 0);
  MtkI2cWrite16 (MTK_I2C_INTR_MASK, 0);
  MtkI2cWrite16 (MTK_I2C_INTR_STAT, MTK_I2C_INTR_ALL);
  MmioWrite32 (MTK_I2C_DMA_BASE + MTK_I2C_DMA_EN, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CopyMem (Buffer, mI2cRxBuf.Bytes, Length);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
MtkI2cRead8 (
  IN UINT8  SlaveAddress,
  IN UINT16 Register,
  OUT UINT8 *Value
  )
{
  EFI_STATUS Status;

  Status = MtkI2cReadBytes (SlaveAddress, Register, Value, 1);
  return Status;
}

STATIC EFI_STATUS
MtkI2cWrite8 (
  IN UINT8 SlaveAddress,
  IN UINT16 Register,
  IN UINT8 Value
  )
{
  return MtkI2cWriteBytes (SlaveAddress, Register, &Value, 1);
}

//
// MT6375 OTG VBUS enable.  The register programming follows the Linux
// mt6375-charger boost setup: enable human-mode access, switch the charger
// into boost parameters, set 5 V / 2.4 A, then assert OTG_EN.
//
STATIC EFI_STATUS
MtkEnableUsbVbus (
  VOID
  )
{
  typedef struct {
    UINT16 Register;
    UINT8  Mask;
    UINT8  BoostValue;
  } MT6375_BOOST_FIELD;

  STATIC CONST MT6375_BOOST_FIELD BoostFields[] = {
    { 0x121, 0xC0, 0x00 },
    { 0x135, 0x03, 0x03 },
    { 0x140, 0xC0, 0xC0 },
    { 0x129, 0x80, 0x00 }
  };
  EFI_STATUS Status;
  UINT8 Value;
  UINTN Index;

  Status = MtkI2cInitHardware ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MusbHostDxe: I2C init failed: %r\n", Status));
    return Status;
  }

  Status = MtkI2cWrite8 (MTK_I2C_PMU_SLAVE, 0x107, 0x69);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < sizeof (BoostFields) / sizeof (BoostFields[0]); Index++) {
    Status = MtkI2cRead8 (MTK_I2C_PMU_SLAVE, BoostFields[Index].Register, &Value);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Value = (UINT8) ((Value & (UINT8) ~BoostFields[Index].Mask) |
                     BoostFields[Index].BoostValue);
    Status = MtkI2cWrite8 (MTK_I2C_PMU_SLAVE, BoostFields[Index].Register, Value);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = MtkI2cWrite8 (MTK_I2C_PMU_SLAVE, 0x131, 0x1A);  // 5.00 V
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = MtkI2cWrite8 (MTK_I2C_PMU_SLAVE, 0x132, 0x06);  // 2.4 A
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MtkI2cRead8 (MTK_I2C_PMU_SLAVE, 0x120, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Value = (UINT8) ((Value | 0x04) & (UINT8) ~0x08);
  Status = MtkI2cWrite8 (MTK_I2C_PMU_SLAVE, 0x120, Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MusbWaitMs (50);
  MtkI2cRead8 (MTK_I2C_PMU_SLAVE, 0x120, &Value);
  DEBUG ((DEBUG_INFO, "MusbHostDxe: MT6375 OTG VBUS enabled (TOP1=%02x)\n",
          Value));
  return EFI_SUCCESS;
}

STATIC VOID
MusbPhyWrite8 (
  IN UINTN Offset,
  IN UINT8 Value
  )
{
  MmioWrite8 (mMusb.PhyBase + Offset, Value);
}

STATIC VOID
MusbPhySet8 (
  IN UINTN Offset,
  IN UINT8 Mask
  )
{
  MusbPhyWrite8 (Offset, (UINT8) (MmioRead8 (mMusb.PhyBase + Offset) | Mask));
}

STATIC VOID
MusbPhyClear8 (
  IN UINTN Offset,
  IN UINT8 Mask
  )
{
  MusbPhyWrite8 (Offset, (UINT8) (MmioRead8 (mMusb.PhyBase + Offset) & ~Mask));
}

//
// PHY recovery sequence from the MediaTek U-Boot glue driver.  LK normally
// leaves the PHY clocked, but this makes host mode explicit.
//
STATIC VOID
MusbPhyRecover (
  VOID
  )
{
  MusbPhyClear8 (0x1D, 0x10);
  MusbPhyClear8 (0x6B, 0x04);
  MusbPhyClear8 (0x6E, 0x01);
  MusbPhyClear8 (0x6A, 0x04);
  MusbPhyClear8 (0x21, 0x03);
  MusbPhyClear8 (0x68, 0xF4);
  MusbPhyClear8 (0x69, 0x3C);
  MusbPhyClear8 (0x6A, 0xBA);
  MusbPhyClear8 (0x1A, 0x80);
  MusbPhySet8   (0x1A, 0x10);
  MusbPhyClear8 (0x18, 0x08);
  MusbPhySet8   (0x18, 0x06);
  MusbWaitMs (800);
  MusbPhySet8   (0x00, 0x20);
}

STATIC VOID
MusbConfigureFifos (
  VOID
  )
{
  STATIC CONST struct {
    UINT8  Ep;
    UINT16 TxSize;
    UINT16 RxSize;
  } FifoCfg[] = {
    { 0,   64,   64 },
    { 1,  512,  512 },
    { 2,  512,  512 },
    { 3,  512,  512 },
    { 4,  512,  512 },
    { 5,  512,  512 },
    { 6, 1024, 1024 },
    { 7,  512,   64 }
  };
  UINTN Index;
  UINT16 Offset;
  UINT8 Size;

  Offset = 0;
  for (Index = 0; Index < sizeof (FifoCfg) / sizeof (FifoCfg[0]); Index++) {
    MusbSelectEp (FifoCfg[Index].Ep);

    Size = (UINT8) ((HighBitSet32 (FifoCfg[Index].TxSize) - 3) & 0x0F);
    MusbWrite8 (MUSB_TXFIFOSZ, Size);
    MusbWrite16 (MUSB_TXFIFOADD, (UINT16) (Offset >> 3));
    Offset += FifoCfg[Index].TxSize;

    Size = (UINT8) ((HighBitSet32 (FifoCfg[Index].RxSize) - 3) & 0x0F);
    MusbWrite8 (MUSB_RXFIFOSZ, Size);
    MusbWrite16 (MUSB_RXFIFOADD, (UINT16) (Offset >> 3));
    Offset += FifoCfg[Index].RxSize;
  }

  DEBUG ((DEBUG_INFO, "MusbHostDxe: FIFO config ends at 0x%x\n", Offset));
}

STATIC UINT8
MusbReadConfigData (
  VOID
  )
{
  MusbSelectEp (0);
  return MusbRead8 (MUSB_CONFIGDATA);
}

STATIC BOOLEAN
MusbPollConnected (
  VOID
  )
{
  UINT8 DevCtl;

  if ((MusbRead8 (MUSB_INTRUSB) & MUSB_INTR_CONNECT) != 0) {
    MusbWrite8 (MUSB_INTRUSB, MUSB_INTR_CONNECT);
    return TRUE;
  }

  DevCtl = MusbRead8 (MUSB_DEVCTL);
  return (BOOLEAN) (((DevCtl & MUSB_DEVCTL_HM) != 0) &&
                    ((DevCtl & MUSB_DEVCTL_VBUS) != 0));
}

STATIC VOID
MusbUpdateSpeed (
  VOID
  )
{
  if ((MusbRead8 (MUSB_DEVCTL) & MUSB_DEVCTL_LSDEV) != 0) {
    mMusb.Speed = EFI_USB_SPEED_LOW;
  } else if ((MusbRead8 (MUSB_DEVCTL) & MUSB_DEVCTL_FSDEV) != 0) {
    mMusb.Speed = EFI_USB_SPEED_FULL;
  } else if ((MusbRead8 (MUSB_POWER) & MUSB_POWER_HSMODE) != 0) {
    mMusb.Speed = EFI_USB_SPEED_HIGH;
  } else {
    mMusb.Speed = EFI_USB_SPEED_HIGH;
  }
}

STATIC EFI_STATUS
MusbInitHardware (
  VOID
  )
{
  UINT8 ConfigData;
  UINT8 DevCtl;
  UINTN Try;
  EFI_STATUS Status;

  if (mMusb.Initialized) {
    return EFI_SUCCESS;
  }

  if (!mMusb.VbusEnabled) {
    Status = MtkEnableUsbVbus ();
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN,
              "MusbHostDxe: MT6375 VBUS enable failed (%r)\n", Status));
    } else {
      mMusb.VbusEnabled = TRUE;
    }
  }

  MusbPhyRecover ();

  DEBUG ((DEBUG_INFO,
          "MusbHostDxe: HWVERS=%02x EPINFO=%02x RAMINFO=%02x CONFIGDATA=%02x\n",
          MusbRead16 (MUSB_HWVERS) & 0xFF,
          MusbRead8 (MUSB_EPINFO),
          MusbRead8 (MUSB_RAMINFO),
          MusbReadConfigData ()));

  ConfigData = MusbReadConfigData ();
  if ((ConfigData & MUSB_CONFIGDATA_DYNFIFO) != 0) {
    MusbConfigureFifos ();
  }

  MusbWrite16 (MUSB_INTRTXE, 0xFFFF);
  MusbWrite16 (MUSB_INTRRXE, 0xFFFE);
  MusbWrite8 (MUSB_INTRUSBE, 0xF7);
  MusbWrite8 (MUSB_TESTMODE, 0);
  MusbWrite8 (MUSB_POWER, MUSB_POWER_ISOUPDATE | MUSB_POWER_HSENAB);
  MusbWrite16 (MUSB_RXTOGEN, 0xFFFF);
  MusbWrite16 (MUSB_TXTOGEN, 0xFFFF);
  MusbWrite32 (USB_L1INTM, L1_TX_INT | L1_RX_INT | L1_USBCOM_INT);

  MusbClearL1Irq (0xFFFFFFFF);
  MusbWrite16 (MUSB_INTRTX, 0xFFFF);
  MusbWrite16 (MUSB_INTRRX, 0xFFFF);
  MusbWrite8 (MUSB_INTRUSB, 0xFF);

  DevCtl = MusbRead8 (MUSB_DEVCTL);
  DevCtl &= ~MUSB_DEVCTL_SESSION;
  MusbWrite8 (MUSB_DEVCTL, DevCtl);
  MusbWaitMs (10);
  MusbWrite8 (MUSB_DEVCTL, (UINT8) (DevCtl | MUSB_DEVCTL_SESSION));

  mMusb.Connected = FALSE;
  mMusb.PortPower = TRUE;
  mMusb.Enabled = FALSE;
  mMusb.PortChange = 0;

  for (Try = 0; Try < 1000; Try++) {
    if (MusbPollConnected ()) {
      mMusb.Connected = TRUE;
      mMusb.PortChange = USB_PORT_STAT_C_CONNECTION;
      break;
    }
    MusbWaitMs (1);
  }

  MusbUpdateSpeed ();

  DEBUG ((DEBUG_INFO,
          "MusbHostDxe: connect=%a devctl=%02x power=%02x speed=%d vbus=%d\n",
          mMusb.Connected ? "yes" : "no",
          MusbRead8 (MUSB_DEVCTL),
          MusbRead8 (MUSB_POWER),
          mMusb.Speed,
          (MusbRead8 (MUSB_DEVCTL) & MUSB_DEVCTL_VBUS) >> 3));

  mMusb.Initialized = TRUE;
  return mMusb.Connected ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC VOID
MusbProgramTarget (
  IN UINT8 Endpoint,
  IN CONST MUSB_TARGET *Target
  )
{
  UINTN TargetBase;

  TargetBase = mMusb.Base + MTK_MUSB_TARGET_BASE + (8 * Endpoint);

  MmioWrite8 (TargetBase + 0, Target->Address);
  MmioWrite8 (TargetBase + 2, Target->HubAddress);
  MmioWrite8 (TargetBase + 3, Target->HubPort);
  MmioWrite8 (TargetBase + 4, Target->Address);
  MmioWrite8 (TargetBase + 6, Target->HubAddress);
  MmioWrite8 (TargetBase + 7, Target->HubPort);
  MusbWrite8 (MUSB_FADDR, Target->Address);
}

STATIC VOID
MusbFifoWrite (
  IN UINT8 Endpoint,
  IN UINTN Length,
  IN CONST UINT8 *Buffer
  )
{
  UINTN Fifo;
  UINTN Index;

  Fifo = mMusb.Base + MUSB_FIFO_OFFSET (Endpoint);
  for (Index = 0; Index < Length; Index++) {
    MmioWrite8 (Fifo, Buffer[Index]);
  }
}

STATIC VOID
MusbFifoRead (
  IN UINT8 Endpoint,
  IN UINTN Length,
  OUT UINT8 *Buffer
  )
{
  UINTN Fifo;
  UINTN Index;

  Fifo = mMusb.Base + MUSB_FIFO_OFFSET (Endpoint);
  for (Index = 0; Index < Length; Index++) {
    Buffer[Index] = MmioRead8 (Fifo);
  }
}

STATIC EFI_STATUS
MusbCheckEp0Error (
  IN OUT UINT16 *Csr,
  OUT UINT32 *Result
  )
{
  UINT16 LocalCsr;
  UINT16 ErrorBits;

  LocalCsr = MusbRead16 (MUSB_CSR0);
  if (Csr != NULL) {
    *Csr = LocalCsr;
  }
  ErrorBits = LocalCsr & (MUSB_CSR0_H_RXSTALL | MUSB_CSR0_H_ERROR |
                          MUSB_CSR0_H_NAKTIMEOUT);
  if (ErrorBits == 0) {
    return EFI_SUCCESS;
  }

  MusbWrite16 (MUSB_CSR0, ErrorBits);
  if ((LocalCsr & MUSB_CSR0_H_RXSTALL) != 0) {
    *Result |= EFI_USB_ERR_STALL;
  }
  if ((LocalCsr & MUSB_CSR0_H_ERROR) != 0) {
    *Result |= EFI_USB_ERR_CRC;
  }
  if ((LocalCsr & MUSB_CSR0_H_NAKTIMEOUT) != 0) {
    *Result |= EFI_USB_ERR_NAK;
  }
  return EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS
MusbWaitEp0Tx (
  IN UINTN TimeoutMs,
  OUT UINT32 *Result
  )
{
  EFI_STATUS Status;

  while (TimeoutMs > 0) {
    Status = MusbCheckEp0Error (NULL, Result);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MusbHostDxe: ep0 error csr=%04x\n",
              MusbRead16 (MUSB_CSR0)));
      return Status;
    }
    if ((MusbRead16 (MUSB_CSR0) & MUSB_CSR0_TXPKTRDY) == 0) {
      return EFI_SUCCESS;
    }
    MusbWaitMs (1);
    TimeoutMs--;
  }

  *Result |= EFI_USB_ERR_TIMEOUT;
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
MusbWaitEp0Rx (
  IN UINTN TimeoutMs,
  OUT UINT32 *Result
  )
{
  EFI_STATUS Status;
  UINT16 Csr;

  while (TimeoutMs > 0) {
    Status = MusbCheckEp0Error (&Csr, Result);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MusbHostDxe: ep0 RX error csr=%04x\n", Csr));
      return Status;
    }
    if ((Csr & MUSB_CSR0_RXPKTRDY) != 0) {
      return EFI_SUCCESS;
    }
    MusbWaitMs (1);
    TimeoutMs--;
  }

  *Result |= EFI_USB_ERR_TIMEOUT;
  return EFI_TIMEOUT;
}

//
// EFI_USB2_HC_PROTOCOL.ControlTransfer
//
STATIC
EFI_STATUS
EFIAPI
MusbControlTransfer (
  IN     EFI_USB2_HC_PROTOCOL               *This,
  IN     UINT8                              DeviceAddress,
  IN     UINT8                              DeviceSpeed,
  IN     UINTN                              MaximumPacketLength,
  IN     EFI_USB_DEVICE_REQUEST             *Request,
  IN     EFI_USB_DATA_DIRECTION             TransferDirection,
  IN OUT VOID                               *Data OPTIONAL,
  IN OUT UINTN                              *DataLength OPTIONAL,
  IN     UINTN                              TimeOut,
  IN     EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
  OUT    UINT32                             *TransferResult
  )
{
  MUSB_TARGET Target;
  UINT8 *Buffer;
  UINTN Total;
  UINTN Offset;
  UINTN Count;
  UINT16 Csr;
  EFI_STATUS Status;
  EFI_TPL OldTpl;

  if (This == NULL || Request == NULL || TransferResult == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mMusb.Connected) {
    *TransferResult = EFI_USB_ERR_NOTEXECUTE;
    return EFI_DEVICE_ERROR;
  }

  *TransferResult = EFI_USB_NOERROR;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  ZeroMem (&Target, sizeof (Target));
  Target.Address = DeviceAddress;
  Target.Speed = DeviceSpeed;
  Target.MaxPacket = (UINT16) MaximumPacketLength;
  Target.Endpoint = 0;
  if (Translator != NULL) {
    Target.HubAddress = Translator->TranslatorHubAddress;
    Target.HubPort = Translator->TranslatorPortNumber;
  }

  MusbSelectEp (0);
  MusbProgramTarget (0, &Target);
  MusbWrite8 (MUSB_NAKLIMIT0, 0);
  MusbClearEp0Irq ();

  MusbWrite16 (MUSB_CSR0, 0);
  MusbFifoWrite (0, sizeof (EFI_USB_DEVICE_REQUEST), (CONST UINT8 *) Request);
  MusbWrite16 (MUSB_CSR0, MUSB_CSR0_H_SETUPPKT | MUSB_CSR0_TXPKTRDY);

  Status = MusbWaitEp0Tx (TimeOut, TransferResult);
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  MusbClearEp0Irq ();

  Buffer = (UINT8 *) Data;
  Total = (DataLength == NULL) ? 0 : *DataLength;

  if (TransferDirection == EfiUsbDataIn) {
    Offset = 0;
    while (Offset < Total) {
      Csr = (UINT16) ((MusbRead16 (MUSB_CSR0) & MUSB_CSR0_H_WZC_BITS) |
                      MUSB_CSR0_H_REQPKT);
      MusbWrite16 (MUSB_CSR0, Csr);

      Status = MusbWaitEp0Rx (TimeOut, TransferResult);
      if (EFI_ERROR (Status)) {
        goto Done;
      }

      Count = MusbRead8 (MUSB_COUNT0);
      if (Count > (Total - Offset)) {
        Count = Total - Offset;
        *TransferResult |= EFI_USB_ERR_BABBLE;
      }
      MusbFifoRead (0, Count, Buffer + Offset);
      Offset += Count;

      Csr = MusbRead16 (MUSB_CSR0) & MUSB_CSR0_H_WZC_BITS;
      Csr &= (UINT16) ~MUSB_CSR0_RXPKTRDY;
      MusbWrite16 (MUSB_CSR0, Csr);
      MusbClearEp0Irq ();

      if (Count < MaximumPacketLength) {
        break;
      }
    }
    if (DataLength != NULL) {
      *DataLength = Offset;
    }
  } else if (TransferDirection == EfiUsbDataOut) {
    Offset = 0;
    while (Offset < Total) {
      Count = Total - Offset;
      if (Count > MaximumPacketLength) {
        Count = MaximumPacketLength;
      }
      MusbFifoWrite (0, Count, Buffer + Offset);
      Offset += Count;
      MusbWrite16 (MUSB_CSR0,
                   (UINT16) (MUSB_CSR0_H_WZC_BITS | MUSB_CSR0_TXPKTRDY));
      Status = MusbWaitEp0Tx (TimeOut, TransferResult);
      if (EFI_ERROR (Status)) {
        goto Done;
      }
      MusbClearEp0Irq ();
    }
    if (DataLength != NULL) {
      *DataLength = Offset;
    }
  }

  //
  // Status stage.
  //
  MusbClearEp0Irq ();
  if (TransferDirection == EfiUsbDataOut) {
    MusbWrite16 (MUSB_CSR0,
                 (UINT16) (MUSB_CSR0_H_WZC_BITS | MUSB_CSR0_H_STATUSPKT |
                           MUSB_CSR0_H_REQPKT));
    Status = MusbWaitEp0Rx (TimeOut, TransferResult);
  } else {
    MusbWrite16 (MUSB_CSR0,
                 (UINT16) (MUSB_CSR0_H_WZC_BITS | MUSB_CSR0_H_STATUSPKT |
                           MUSB_CSR0_TXPKTRDY));
    Status = MusbWaitEp0Tx (TimeOut, TransferResult);
  }
  MusbClearEp0Irq ();

Done:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC EFI_STATUS
MusbWaitEndpoint (
  IN UINT8 Endpoint,
  IN BOOLEAN IsIn,
  IN UINTN TimeoutMs,
  OUT UINT32 *Result
  )
{
  UINT16 Bit;

  Bit = (UINT16) (1U << Endpoint);
  while (TimeoutMs > 0) {
    UINT16 Irq;

    Irq = IsIn ? MusbRead16 (MUSB_INTRRX) : MusbRead16 (MUSB_INTRTX);
    if ((Irq & Bit) != 0) {
      MusbWrite16 (IsIn ? MUSB_INTRRX : MUSB_INTRTX, Bit);
      MusbClearL1Irq (IsIn ? L1_RX_INT : L1_TX_INT);
      return EFI_SUCCESS;
    }
    MusbWaitMs (1);
    TimeoutMs--;
  }

  *Result |= EFI_USB_ERR_TIMEOUT;
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
MusbBulkOrInterruptIn (
  IN UINT8 Endpoint,
  IN CONST MUSB_TARGET *Target,
  IN OUT UINT8 *Data,
  IN OUT UINTN *DataLength,
  IN OUT UINT8 *DataToggle,
  IN UINTN Interval,
  IN UINTN TimeoutMs,
  OUT UINT32 *Result
  )
{
  UINT16 Csr;
  UINTN Offset;
  UINTN Count;
  EFI_STATUS Status;
  UINTN Retry;

  MusbSelectEp (Endpoint);
  MusbProgramTarget (Endpoint, Target);

  Csr = (UINT16) ((MusbRead16 (MUSB_RXCSR) & MUSB_RXCSR_H_WZC_BITS) &
                  ~MUSB_RXCSR_RXPKTRDY);
  Csr |= MUSB_RXCSR_FLUSHFIFO;
  MusbWrite16 (MUSB_RXCSR, Csr);
  MusbWrite16 (MUSB_RXCSR, Csr);

  MusbWrite16 (MUSB_RXMAXP, Target->MaxPacket);
  MusbWrite8 (MUSB_RXTYPE,
              (UINT8) ((Target->Speed << MUSB_TYPE_SPEED_SHIFT) |
                       (Target->Type << MUSB_TYPE_PROTO_SHIFT) |
                       (Target->Endpoint & 0x0F)));
  MusbWrite8 (MUSB_RXINTERVAL, (UINT8) MIN (Interval, 16));

  Csr = MusbRead16 (MUSB_RXCSR) & MUSB_RXCSR_H_WZC_BITS;
  MusbSetHwToggle (Endpoint, TRUE, *DataToggle);
  if (Target->Type == MUSB_TYPE_INT) {
    Csr |= MUSB_RXCSR_DISNYET;
  }
  Csr |= MUSB_RXCSR_H_REQPKT;
  MusbWrite16 (MUSB_RXCSR, Csr);

  Offset = 0;
  Retry = TimeoutMs;
  while (Retry > 0) {
    Status = MusbWaitEndpoint (Endpoint, TRUE, 100, Result);
    if (EFI_ERROR (Status)) {
      if (Status == EFI_TIMEOUT) {
        Retry--;
        continue;
      }
      return Status;
    }

    Csr = MusbRead16 (MUSB_RXCSR);
    if ((Csr & MUSB_RXCSR_H_RXSTALL) != 0) {
      *Result |= EFI_USB_ERR_STALL;
      MusbWrite16 (MUSB_RXCSR, Csr & MUSB_RXCSR_H_WZC_BITS);
      return EFI_DEVICE_ERROR;
    }
    if ((Csr & MUSB_RXCSR_H_ERROR) != 0) {
      *Result |= EFI_USB_ERR_CRC;
      MusbWrite16 (MUSB_RXCSR, Csr & MUSB_RXCSR_H_WZC_BITS);
      return EFI_DEVICE_ERROR;
    }
    if ((Csr & MUSB_RXCSR_DATAERROR) != 0) {
      Csr = (Csr & MUSB_RXCSR_H_WZC_BITS) & ~MUSB_RXCSR_DATAERROR;
      MusbWrite16 (MUSB_RXCSR, Csr);
      Retry--;
      continue;
    }
    if ((Csr & MUSB_RXCSR_RXPKTRDY) == 0) {
      Retry--;
      continue;
    }

    Count = MusbRead16 (MUSB_RXCOUNT);
    if (Count > (*DataLength - Offset)) {
      Count = *DataLength - Offset;
      *Result |= EFI_USB_ERR_BABBLE;
    }
    MusbFifoRead (Endpoint, Count, Data + Offset);
    Offset += Count;

    Csr = MusbRead16 (MUSB_RXCSR) & MUSB_RXCSR_H_WZC_BITS;
    Csr &= (UINT16) ~MUSB_RXCSR_RXPKTRDY;
    if ((Offset < *DataLength) && (Count == Target->MaxPacket)) {
      Csr |= MUSB_RXCSR_H_REQPKT;
    }
    MusbWrite16 (MUSB_RXCSR, Csr);

    if ((Offset >= *DataLength) || (Count < Target->MaxPacket)) {
      *DataLength = Offset;
      *DataToggle = MusbGetHwToggle (Endpoint, TRUE);
      return EFI_SUCCESS;
    }
  }

  *Result |= EFI_USB_ERR_TIMEOUT;
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
MusbBulkOut (
  IN UINT8 Endpoint,
  IN CONST MUSB_TARGET *Target,
  IN CONST UINT8 *Data,
  IN OUT UINTN *DataLength,
  IN OUT UINT8 *DataToggle,
  IN UINTN TimeoutMs,
  OUT UINT32 *Result
  )
{
  UINT16 Csr;
  UINTN Offset;
  UINTN Count;
  EFI_STATUS Status;
  UINTN Retry;

  MusbSelectEp (Endpoint);
  MusbProgramTarget (Endpoint, Target);

  Csr = MusbRead16 (MUSB_TXCSR);
  while ((Csr & MUSB_TXCSR_FIFONOTEMPTY) != 0) {
    MusbWrite16 (MUSB_TXCSR, Csr | MUSB_TXCSR_FLUSHFIFO);
    Csr = MusbRead16 (MUSB_TXCSR);
  }

  Csr &= ~(MUSB_TXCSR_H_NAKTIMEOUT | MUSB_TXCSR_H_RXSTALL |
           MUSB_TXCSR_H_ERROR | MUSB_TXCSR_TXPKTRDY |
           MUSB_TXCSR_DMAENAB | MUSB_TXCSR_AUTOSET);
  Csr |= MUSB_TXCSR_MODE;
  MusbSetHwToggle (Endpoint, FALSE, *DataToggle);
  MusbWrite16 (MUSB_TXCSR, Csr);
  MusbWrite16 (MUSB_TXMAXP, Target->MaxPacket);
  MusbWrite8 (MUSB_TXTYPE,
              (UINT8) ((Target->Speed << MUSB_TYPE_SPEED_SHIFT) |
                       (MUSB_TYPE_BULK << MUSB_TYPE_PROTO_SHIFT) |
                       (Target->Endpoint & 0x0F)));
  MusbWrite8 (MUSB_TXINTERVAL, 0);

  Offset = 0;
  Retry = TimeoutMs;
  while (Offset < *DataLength) {
    Count = *DataLength - Offset;
    if (Count > Target->MaxPacket) {
      Count = Target->MaxPacket;
    }
    MusbFifoWrite (Endpoint, Count, Data + Offset);
    Offset += Count;

    Csr = MusbRead16 (MUSB_TXCSR) & MUSB_TXCSR_H_WZC_BITS;
    Csr |= MUSB_TXCSR_TXPKTRDY;
    MusbWrite16 (MUSB_TXCSR, Csr);

    Status = MusbWaitEndpoint (Endpoint, FALSE, 100, Result);
    if (EFI_ERROR (Status)) {
      if (Status == EFI_TIMEOUT) {
        Retry--;
        continue;
      }
      return Status;
    }

    Csr = MusbRead16 (MUSB_TXCSR);
    if ((Csr & MUSB_TXCSR_H_RXSTALL) != 0) {
      *Result |= EFI_USB_ERR_STALL;
      return EFI_DEVICE_ERROR;
    }
    if ((Csr & MUSB_TXCSR_H_ERROR) != 0) {
      *Result |= EFI_USB_ERR_CRC;
      return EFI_DEVICE_ERROR;
    }
    if ((Csr & MUSB_TXCSR_H_NAKTIMEOUT) != 0) {
      Csr = (Csr & MUSB_TXCSR_H_WZC_BITS) & ~MUSB_TXCSR_H_NAKTIMEOUT;
      MusbWrite16 (MUSB_TXCSR, Csr);
      Retry--;
      continue;
    }
  }

  *DataLength = Offset;
  *DataToggle = MusbGetHwToggle (Endpoint, FALSE);
  return EFI_SUCCESS;
}

//
// EFI_USB2_HC_PROTOCOL.BulkTransfer
//
STATIC
EFI_STATUS
EFIAPI
MusbBulkTransfer (
  IN     EFI_USB2_HC_PROTOCOL               *This,
  IN     UINT8                              DeviceAddress,
  IN     UINT8                              EndPointAddress,
  IN     UINT8                              DeviceSpeed,
  IN     UINTN                              MaximumPacketLength,
  IN     UINT8                              DataBuffersNumber,
  IN OUT VOID                               *Data[EFI_USB_MAX_BULK_BUFFER_NUM],
  IN OUT UINTN                              *DataLength,
  IN OUT UINT8                              *DataToggle,
  IN     UINTN                              TimeOut,
  IN     EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
  OUT    UINT32                             *TransferResult
  )
{
  MUSB_TARGET Target;
  EFI_STATUS Status;
  UINTN Length;
  EFI_TPL OldTpl;
  BOOLEAN IsIn;

  if (This == NULL || Data == NULL || DataLength == NULL ||
      DataToggle == NULL || TransferResult == NULL ||
      DataBuffersNumber == 0 || DataBuffersNumber > EFI_USB_MAX_BULK_BUFFER_NUM) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mMusb.Connected) {
    *TransferResult = EFI_USB_ERR_NOTEXECUTE;
    return EFI_DEVICE_ERROR;
  }

  *TransferResult = EFI_USB_NOERROR;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  ZeroMem (&Target, sizeof (Target));
  Target.Address = DeviceAddress;
  Target.Speed = DeviceSpeed;
  Target.MaxPacket = (UINT16) MaximumPacketLength;
  Target.Endpoint = (UINT8) (EndPointAddress & 0x0F);
  Target.Type = MUSB_TYPE_BULK;
  if (Translator != NULL) {
    Target.HubAddress = Translator->TranslatorHubAddress;
    Target.HubPort = Translator->TranslatorPortNumber;
  }

  //
  // UsbBusDxe always passes one bulk buffer.  Supporting a split transfer
  // would require a per-buffer length, which this USB2 HC API does not
  // provide, so reject the rare multi-buffer caller instead of silently
  // transferring the wrong amount of data.
  //
  if (DataBuffersNumber != 1) {
    gBS->RestoreTPL (OldTpl);
    return EFI_UNSUPPORTED;
  }

  IsIn = (EndPointAddress & 0x80) != 0;
  Length = *DataLength;
  if (IsIn) {
    Status = MusbBulkOrInterruptIn (1, &Target, Data[0],
                                    &Length, DataToggle, 0,
                                    TimeOut, TransferResult);
  } else {
    Status = MusbBulkOut (2, &Target, Data[0],
                          &Length, DataToggle, TimeOut, TransferResult);
  }
  if (EFI_ERROR (Status)) {
    gBS->RestoreTPL (OldTpl);
    return Status;
  }

  *DataLength = Length;
  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

//
// EFI_USB2_HC_PROTOCOL.SyncInterruptTransfer
//
STATIC
EFI_STATUS
EFIAPI
MusbSyncInterruptTransfer (
  IN     EFI_USB2_HC_PROTOCOL               *This,
  IN     UINT8                              DeviceAddress,
  IN     UINT8                              EndPointAddress,
  IN     UINT8                              DeviceSpeed,
  IN     UINTN                              MaximumPacketLength,
  IN OUT VOID                               *Data,
  IN OUT UINTN                              *DataLength,
  IN OUT UINT8                              *DataToggle,
  IN     UINTN                              TimeOut,
  IN     EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
  OUT    UINT32                             *TransferResult
  )
{
  MUSB_TARGET Target;
  EFI_STATUS Status;
  EFI_TPL OldTpl;

  if (This == NULL || Data == NULL || DataLength == NULL ||
      DataToggle == NULL || TransferResult == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mMusb.Connected || (EndPointAddress & 0x80) == 0) {
    *TransferResult = EFI_USB_ERR_NOTEXECUTE;
    return EFI_DEVICE_ERROR;
  }

  *TransferResult = EFI_USB_NOERROR;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  ZeroMem (&Target, sizeof (Target));
  Target.Address = DeviceAddress;
  Target.Speed = DeviceSpeed;
  Target.MaxPacket = (UINT16) MaximumPacketLength;
  Target.Endpoint = (UINT8) (EndPointAddress & 0x0F);
  Target.Type = MUSB_TYPE_INT;
  if (Translator != NULL) {
    Target.HubAddress = Translator->TranslatorHubAddress;
    Target.HubPort = Translator->TranslatorPortNumber;
  }

  Status = MusbBulkOrInterruptIn (3, &Target, Data, DataLength,
                                  DataToggle, 1, TimeOut, TransferResult);
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC VOID EFIAPI MusbAsyncTimer (IN EFI_EVENT Event, IN VOID *Context);

//
// EFI_USB2_HC_PROTOCOL.AsyncInterruptTransfer
//
STATIC
EFI_STATUS
EFIAPI
MusbAsyncInterruptTransfer (
  IN     EFI_USB2_HC_PROTOCOL                                *This,
  IN     UINT8                                               DeviceAddress,
  IN     UINT8                                               EndPointAddress,
  IN     UINT8                                               DeviceSpeed,
  IN     UINTN                                               MaxiumPacketLength,
  IN     BOOLEAN                                             IsNewTransfer,
  IN OUT UINT8                                               *DataToggle,
  IN     UINTN                                               PollingInterval OPTIONAL,
  IN     UINTN                                               DataLength OPTIONAL,
  IN     EFI_USB2_HC_TRANSACTION_TRANSLATOR                  *Translator OPTIONAL,
  IN     EFI_ASYNC_USB_TRANSFER_CALLBACK                     CallBackFunction OPTIONAL,
  IN     VOID                                                *Context OPTIONAL
  )
{
  MUSB_ASYNC_INT *Async;
  UINTN Index;
  EFI_STATUS Status;

  if (This == NULL || DataToggle == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mMusb.Connected || (EndPointAddress & 0x80) == 0) {
    return EFI_DEVICE_ERROR;
  }

  if (!IsNewTransfer) {
    for (Index = 4; Index < MUSB_MAX_HW_EP; Index++) {
      Async = &mMusb.Async[Index];
      if (Async->Active && Async->Endpoint == (EndPointAddress & 0x0F) &&
          Async->DataLength == DataLength) {
        Async->Active = FALSE;
        if (Async->Timer != NULL) {
          gBS->SetTimer (Async->Timer, TimerCancel, 0);
          gBS->CloseEvent (Async->Timer);
          Async->Timer = NULL;
        }
        *DataToggle = Async->DataToggle;
        return EFI_SUCCESS;
      }
    }
    return EFI_NOT_FOUND;
  }

  if (CallBackFunction == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 4; Index < MUSB_MAX_HW_EP; Index++) {
    Async = &mMusb.Async[Index];
    if (!Async->Active) {
      break;
    }
  }
  if (Index >= MUSB_MAX_HW_EP) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Async, sizeof (*Async));
  Async->Address = DeviceAddress;
  Async->Endpoint = (UINT8) (EndPointAddress & 0x0F);
  Async->HwEndpoint = (UINT8) Index;
  Async->Speed = DeviceSpeed;
  Async->MaxPacket = (UINT16) MaxiumPacketLength;
  Async->Interval = (PollingInterval == 0) ? 1 : PollingInterval;
  Async->DataLength = DataLength;
  Async->DataToggle = *DataToggle;
  Async->HubAddress = (Translator == NULL) ? 0 : Translator->TranslatorHubAddress;
  Async->HubPort = (Translator == NULL) ? 0 : Translator->TranslatorPortNumber;
  Async->Callback = CallBackFunction;
  Async->Context = Context;
  Async->Active = TRUE;
  Async->Timer = NULL;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL,
                             TPL_NOTIFY, MusbAsyncTimer, Async, &Async->Timer);
  if (EFI_ERROR (Status)) {
    Async->Active = FALSE;
    return Status;
  }

  //
  // EDK2 timer periods are in 100 ns units.
  //
  Status = gBS->SetTimer (Async->Timer, TimerPeriodic,
                          Async->Interval * 10000);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Async->Timer);
    Async->Timer = NULL;
    Async->Active = FALSE;
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MusbHostDxe: async interrupt ep=%d interval=%d\n",
          Async->Endpoint, Async->Interval));
  return EFI_SUCCESS;
}

STATIC VOID
EFIAPI
MusbAsyncTimer (
  IN EFI_EVENT Event,
  IN VOID *Context
  )
{
  MUSB_ASYNC_INT *Async;
  MUSB_TARGET Target;
  EFI_STATUS Status;
  UINT32 Result;
  UINT8 Buffer[64];
  UINTN Length;

  Async = (MUSB_ASYNC_INT *) Context;
  if (Async == NULL || !Async->Active) {
    return;
  }

  ZeroMem (&Target, sizeof (Target));
  Target.Address = Async->Address;
  Target.Speed = Async->Speed;
  Target.MaxPacket = Async->MaxPacket;
  Target.Endpoint = Async->Endpoint;
  Target.Type = MUSB_TYPE_INT;
  Target.HubAddress = Async->HubAddress;
  Target.HubPort = Async->HubPort;

  Length = MIN (Async->DataLength, sizeof (Buffer));
  ZeroMem (Buffer, sizeof (Buffer));
  Result = EFI_USB_NOERROR;
  Status = MusbBulkOrInterruptIn (Async->HwEndpoint, &Target, Buffer, &Length,
                                  &Async->DataToggle, Async->Interval,
                                  50, &Result);
  if (EFI_ERROR (Status)) {
    return;
  }

  Async->Callback (Buffer, Length, Async->Context, EFI_USB_NOERROR);
}

//
// Root hub helpers
//
STATIC
EFI_STATUS
EFIAPI
MusbGetRootHubPortStatus (
  IN  EFI_USB2_HC_PROTOCOL *This,
  IN  UINT8                PortNumber,
  OUT EFI_USB_PORT_STATUS  *PortStatus
  )
{
  if (This == NULL || PortStatus == NULL || PortNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }

  PortStatus->PortStatus = 0;
  if (mMusb.Connected) {
    PortStatus->PortStatus = USB_PORT_STAT_CONNECTION | USB_PORT_STAT_POWER;
    if (mMusb.Enabled) {
      PortStatus->PortStatus |= USB_PORT_STAT_ENABLE;
    }
    if (mMusb.Speed == EFI_USB_SPEED_LOW) {
      PortStatus->PortStatus |= USB_PORT_STAT_LOW_SPEED;
    } else if (mMusb.Speed == EFI_USB_SPEED_HIGH) {
      PortStatus->PortStatus |= USB_PORT_STAT_HIGH_SPEED;
    }
  }

  PortStatus->PortChangeStatus = mMusb.PortChange;
  mMusb.PortChange = 0;

  DEBUG ((DEBUG_VERBOSE,
          "MusbHostDxe: root port status=%04x change=%04x\n",
          PortStatus->PortStatus, PortStatus->PortChangeStatus));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MusbSetRootHubPortFeature (
  IN EFI_USB2_HC_PROTOCOL *This,
  IN UINT8                PortNumber,
  IN EFI_USB_PORT_FEATURE PortFeature
  )
{
  UINT8 Power;
  EFI_STATUS Status;

  if (This == NULL || PortNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }

  switch (PortFeature) {
  case EfiUsbPortEnable:
    mMusb.Enabled = TRUE;
    return EFI_SUCCESS;
  case EfiUsbPortPower:
    mMusb.PortPower = TRUE;
    return EFI_SUCCESS;
  case EfiUsbPortReset:
    if (!mMusb.Connected) {
      return EFI_DEVICE_ERROR;
    }
    DEBUG ((DEBUG_INFO, "MusbHostDxe: reset root port\n"));
    Power = MusbRead8 (MUSB_POWER);
    MusbWrite8 (MUSB_POWER, (UINT8) (Power | MUSB_POWER_RESET));
    MusbWaitMs (50);
    MusbWrite8 (MUSB_POWER, (UINT8) (Power & ~MUSB_POWER_RESET));
    MusbWaitMs (10);

    mMusb.Initialized = FALSE;
    Status = MusbInitHardware ();
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "MusbHostDxe: no device after reset (%r)\n",
              Status));
      return Status;
    }
    mMusb.Enabled = TRUE;
    mMusb.PortChange |= USB_PORT_STAT_C_RESET;
    return EFI_SUCCESS;
  default:
    return EFI_UNSUPPORTED;
  }
}

STATIC
EFI_STATUS
EFIAPI
MusbClearRootHubPortFeature (
  IN EFI_USB2_HC_PROTOCOL *This,
  IN UINT8                PortNumber,
  IN EFI_USB_PORT_FEATURE PortFeature
  )
{
  if (This == NULL || PortNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }

  switch (PortFeature) {
  case EfiUsbPortEnable:
    mMusb.Enabled = FALSE;
    return EFI_SUCCESS;
  case EfiUsbPortPower:
    mMusb.PortPower = FALSE;
    return EFI_SUCCESS;
  case EfiUsbPortReset:
    mMusb.PortChange |= USB_PORT_STAT_C_RESET;
    return EFI_SUCCESS;
  case EfiUsbPortConnectChange:
    mMusb.PortChange &= (UINT16) ~USB_PORT_STAT_C_CONNECTION;
    return EFI_SUCCESS;
  case EfiUsbPortEnableChange:
    mMusb.PortChange &= (UINT16) ~USB_PORT_STAT_C_ENABLE;
    return EFI_SUCCESS;
  case EfiUsbPortOverCurrentChange:
    mMusb.PortChange &= (UINT16) ~USB_PORT_STAT_C_OVERCURRENT;
    return EFI_SUCCESS;
  case EfiUsbPortResetChange:
    mMusb.PortChange &= (UINT16) ~USB_PORT_STAT_C_RESET;
    return EFI_SUCCESS;
  default:
    return EFI_UNSUPPORTED;
  }
}

STATIC
EFI_STATUS
EFIAPI
MusbGetCapability (
  IN EFI_USB2_HC_PROTOCOL *This,
  OUT UINT8               *MaxSpeed,
  OUT UINT8               *PortNumber,
  OUT UINT8               *Is64BitCapable
  )
{
  if (This == NULL || MaxSpeed == NULL || PortNumber == NULL ||
      Is64BitCapable == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *MaxSpeed = EFI_USB_SPEED_HIGH;
  *PortNumber = 1;
  *Is64BitCapable = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MusbReset (
  IN EFI_USB2_HC_PROTOCOL *This,
  IN UINT16                Attributes
  )
{
  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if ((Attributes & EFI_USB_HC_RESET_HOST_CONTROLLER) == 0) {
    return EFI_UNSUPPORTED;
  }

  mMusb.Initialized = FALSE;
  return MusbInitHardware ();
}

STATIC
EFI_STATUS
EFIAPI
MusbGetState (
  IN EFI_USB2_HC_PROTOCOL *This,
  OUT EFI_USB_HC_STATE    *State
  )
{
  if (This == NULL || State == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *State = EfiUsbHcStateOperational;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MusbSetState (
  IN EFI_USB2_HC_PROTOCOL *This,
  IN EFI_USB_HC_STATE     State
  )
{
  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  return (State == EfiUsbHcStateOperational) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
MusbIsochronousTransfer (
  IN     EFI_USB2_HC_PROTOCOL               *This,
  IN     UINT8                              DeviceAddress,
  IN     UINT8                              EndPointAddress,
  IN     UINT8                              DeviceSpeed,
  IN     UINTN                              MaximumPacketLength,
  IN     UINT8                              DataBuffersNumber,
  IN OUT VOID                               *Data[EFI_USB_MAX_ISO_BUFFER_NUM],
  IN     UINTN                              DataLength,
  IN     EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
  OUT    UINT32                             *TransferResult
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
MusbAsyncIsochronousTransfer (
  IN     EFI_USB2_HC_PROTOCOL               *This,
  IN     UINT8                              DeviceAddress,
  IN     UINT8                              EndPointAddress,
  IN     UINT8                              DeviceSpeed,
  IN     UINTN                              MaximumPacketLength,
  IN     UINT8                              DataBuffersNumber,
  IN OUT VOID                               *Data[EFI_USB_MAX_ISO_BUFFER_NUM],
  IN     UINTN                              DataLength,
  IN     EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
  IN     EFI_ASYNC_USB_TRANSFER_CALLBACK    IsochronousCallBack,
  IN     VOID                               *Context
  )
{
  return EFI_UNSUPPORTED;
}

STATIC EFI_USB2_HC_PROTOCOL mMusbUsb2Hc = {
  MusbGetCapability,
  MusbReset,
  MusbGetState,
  MusbSetState,
  MusbControlTransfer,
  MusbBulkTransfer,
  MusbAsyncInterruptTransfer,
  MusbSyncInterruptTransfer,
  MusbIsochronousTransfer,
  MusbAsyncIsochronousTransfer,
  MusbGetRootHubPortStatus,
  MusbSetRootHubPortFeature,
  MusbClearRootHubPortFeature,
  1,
  0
};

/**
  DXE driver entry point.
**/
EFI_STATUS
EFIAPI
MusbHostDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;

  mMusb.Base = MUSB_BASE;
  mMusb.PhyBase = USB20_PHY_BASE;
  CopyMem (&mMusbDevicePath.Guid, &gMusbDevicePathGuid, sizeof (EFI_GUID));
  mMusb.DevicePath = (EFI_DEVICE_PATH_PROTOCOL *) &mMusbDevicePath;

  Status = MusbInitHardware ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN,
            "MusbHostDxe: no device detected on USB2 OTG port (%r)\n",
            Status));
  }

  CopyMem (&mMusb.Usb2Hc, &mMusbUsb2Hc, sizeof (mMusbUsb2Hc));
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mMusb.ControllerHandle,
                  &gEfiDevicePathProtocolGuid, (VOID *) &mMusbDevicePath,
                  &gEfiUsb2HcProtocolGuid, (VOID *) &mMusb.Usb2Hc,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MusbHostDxe: failed to install USB2 HC: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MusbHostDxe: installed EFI_USB2_HC_PROTOCOL\n"));
  return EFI_SUCCESS;
}
