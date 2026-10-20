# Makefile - NinoOS / NinoKernel
CC      := gcc
AS      := nasm
LD      := ld

TARGET  := ninoos.elf
VERSION := 0.7
NIC     ?= rtl8139          # network card for "make run": rtl8139 or e1000 (VirtualBox uses e1000)

CFLAGS  := -m32 -march=i686 -mno-sse -mno-mmx -mno-80387 \
           -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fcf-protection=none \
           -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra
ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

ASM_OBJS := boot.o isr.o gdt_flush.o
C_OBJS   := vga.o gdt.o idt.o timer.o rtc.o heap.o util.o string.o fs.o \
            keyboard.o mouse.o gfx.o ata.o persist.o widgets.o paint.o pci.o rtl8139.o e1000.o nic.o net.o tls.o url.o http.o css.o html.o cookies.o download.o browser.o editor.o sysreport.o power.o panic.o \
            term.o capybara.o viewer.o fastfetch.o gui.o kernel.o
OBJS     := $(ASM_OBJS) $(C_OBJS)

# ---- Optional HTTPS: needs the BearSSL sources in ./bearssl and a generated ./trust_anchors.h
# (see README-HTTPS.txt). Without them NinoOS still builds; https:// pages just show an explanation.
BEAR_DIR := bearssl
ifneq ($(and $(wildcard $(BEAR_DIR)/src/inner.h),$(wildcard trust_anchors.h)),)
HTTPS    := 1
BEAR_SRC := $(shell find $(BEAR_DIR)/src -name '*.c')
BEAR_OBJS := $(BEAR_SRC:.c=.o)
# No operating system under us: turn off every BearSSL feature that needs one
BEAR_DEFS := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
             -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0
CFLAGS   += -DNINO_HTTPS -I$(BEAR_DIR)/inc
OBJS     += $(BEAR_OBJS)
endif

.PHONY: all run check iso clean cleandisk https-status dist

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(HTTPS),1)
# BearSSL objects: its own flags, no warnings
$(BEAR_OBJS): %.o: %.c
	$(CC) -m32 -march=i686 -mno-sse -mno-mmx -mno-80387 -std=gnu11 -ffreestanding -fno-pic -fno-pie \
	      -fno-stack-protector -fno-asynchronous-unwind-tables -fcf-protection=none -O2 -w \
	      $(BEAR_DEFS) -I$(BEAR_DIR)/inc -I$(BEAR_DIR)/src -c $< -o $@
endif

https-status:
ifeq ($(HTTPS),1)
	@echo "HTTPS: ON (BearSSL found, trust_anchors.h found)"
else
	@echo "HTTPS: OFF - see README-HTTPS.txt"
endif

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# Bootable ISO via GRUB (needs grub, xorriso, mtools). GRUB sets the 1024x768x32 video mode.
iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/$(TARGET)
	printf 'set timeout=0\nset default=0\ninsmod all_video\nmenuentry "NinoOS" {\n\tmultiboot /boot/$(TARGET)\n}\n' > isodir/boot/grub/grub.cfg
	grub-mkrescue -o ninoos.iso isodir

# 64 MiB raw disk image for the file system (created once, kept across 'make clean')
disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64 status=none

# Build the ISO and boot it in QEMU.
#  -rtc localtime       : the taskbar clock shows your local time
#  -drive ... disk.img  : the persistent disk (primary IDE master)
#  $(NIC) + user net    : network card; QEMU acts as router, DHCP and DNS server (no root needed)
#  -cpu max             : gives NinoOS the CPU's hardware random number generator (RDRAND) for TLS
#  grab-on-hover        : the mouse is captured as soon as it enters the window, and Ctrl+Alt+G releases it
run: iso disk.img
	qemu-system-i386 -cdrom ninoos.iso -drive file=disk.img,format=raw,if=ide,index=0,media=disk \
		-device $(NIC),netdev=n0 -netdev user,id=n0 \
		-cpu max -m 256M -rtc base=localtime -display gtk,grab-on-hover=on

# Everything a friend needs to try NinoOS in VirtualBox: the ISO, a setup script and instructions.
# Creates NinoOS-$(VERSION).zip (needs the 'zip' program) or NinoOS-$(VERSION).tar.gz
dist: iso
	rm -rf dist && mkdir -p dist/NinoOS
	cp ninoos.iso dist/NinoOS/
	cp README.md create-vm.sh create-vm.bat THIRD-PARTY.txt dist/NinoOS/
	rm -f NinoOS-$(VERSION).zip NinoOS-$(VERSION).tar.gz
	cd dist && (zip -q -r ../NinoOS-$(VERSION).zip NinoOS || tar czf ../NinoOS-$(VERSION).tar.gz NinoOS)
	@echo "Ready: NinoOS-$(VERSION).zip (or .tar.gz). Send this file to your friends."

# Verify that the Multiboot header is valid
check: $(TARGET)
	grub-file --is-x86-multiboot $(TARGET) && echo "Multiboot OK"

clean:
	rm -rf *.o $(TARGET) isodir ninoos.iso
	-@find $(BEAR_DIR) -name '*.o' -delete 2>/dev/null; true

# Forget all saved files (wipes disk.img)
cleandisk:
	rm -f disk.img