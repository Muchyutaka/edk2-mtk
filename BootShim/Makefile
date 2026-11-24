ARCH              = arm64
TARGET            = aarch64-linux-gnu
CROSS_COMPILE     = $(TARGET)-
CC                = $(CROSS_COMPILE)gcc
OBJCOPY           = $(CROSS_COMPILE)objcopy

all: BootShim.elf BootShim.bin 

BootShim.bin: BootShim.elf
	$(OBJCOPY) -O binary $< $@

CFLAGS = -DUEFI_BASE=$(UEFI_BASE) -DUEFI_SIZE=$(UEFI_SIZE) $(EXTRA_CFLAGS)

BootShim.elf: BootShim.S
	$(CC) -c $< -o $@ $(CFLAGS)

BootShim.S:

clean:
	rm -f BootShim.elf BootShim.bin