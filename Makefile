# ============================================================
#  FunnyOS  –  Makefile
#  Requires: clang, lld, nasm, xorriso, qemu-system-x86_64
#  On MSYS2 MinGW64:
#    pacman -S mingw-w64-x86_64-clang mingw-w64-x86_64-lld nasm xorriso
#  On Debian/Ubuntu:
#    sudo apt install clang lld nasm xorriso qemu-system-x86_64
# ============================================================

# ── Toolchain ────────────────────────────────────────────────────────────────
CC      := clang
AS      := nasm
LD      := ld.lld
OBJCOPY := llvm-objcopy

# ── Flags ────────────────────────────────────────────────────────────────────
CFLAGS  := --target=x86_64-elf -std=c11 -ffreestanding -fno-stack-protector \
           -fno-stack-check -fno-omit-frame-pointer -fno-PIC              \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel     \
           -O2 -Wall -Wextra                                              \
           -Ikernel/src

ASFLAGS := -f elf64

LDFLAGS := -nostdlib -static -m elf_x86_64 \
           -T kernel/linker.ld --no-dynamic-linker -z max-page-size=0x1000

# ── Source files ─────────────────────────────────────────────────────────────
C_SRCS := \
    kernel/src/kernel.c               \
    kernel/src/cpu/cpu.c              \
    kernel/src/mm/pmm.c               \
    kernel/src/lib/string.c           \
    kernel/src/lib/printf.c           \
    kernel/src/terminal/terminal.c    \
    kernel/src/drivers/keyboard.c     \
    kernel/src/drivers/disk.c         \
    kernel/src/drivers/mouse.c        \
    kernel/src/drivers/pit.c          \
    kernel/src/drivers/rtc.c          \
    kernel/src/gui/gfx.c              \
    kernel/src/gui/wm.c               \
    kernel/src/fs/fs.c                \
    kernel/src/shell/shell.c          \
    kernel/src/compiler/compiler.c

ASM_SRCS := \
    kernel/src/boot/entry.asm         \
    kernel/src/cpu/cpu_asm.asm

# ── Object files ─────────────────────────────────────────────────────────────
C_OBJS   := $(C_SRCS:.c=.o)
ASM_OBJS := $(ASM_SRCS:.asm=.o)
OBJS     := $(ASM_OBJS) $(C_OBJS)

# ── Limine ───────────────────────────────────────────────────────────────────
LIMINE_DIR       := limine
LIMINE_REPO      := https://github.com/limine-bootloader/limine.git
LIMINE_BRANCH    := v8.x-binary

# ── Output ───────────────────────────────────────────────────────────────────
KERNEL  := dist/kernel.elf
ISO     := dist/funnyos.iso
DISK    := dist/disk.img
ISO_DIR := iso_root

.PHONY: all run debug clean clean-disk limine fetch-limine disk

# ── Create blank disk image (4 MiB, preserved across rebuilds) ──────────────────
disk: | dist
	@if [ ! -f "$(DISK)" ]; then \
	    echo "  DISK creating $(DISK) (4 MiB)..."; \
	    dd if=/dev/zero of=$(DISK) bs=512 count=8192 2>/dev/null; \
	    echo "  DISK done."; \
	fi

# ── Default target ────────────────────────────────────────────────────────────────────
all: $(ISO) disk

# ── Fetch Limine bootloader (binary release) ──────────────────────────────────
fetch-limine:
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
	    echo "[*] Cloning Limine $(LIMINE_BRANCH)..."; \
	    git clone --depth=1 --branch $(LIMINE_BRANCH) $(LIMINE_REPO) $(LIMINE_DIR); \
	    cd $(LIMINE_DIR) && make; \
	else \
	    echo "[*] Limine already present."; \
	fi

# ── Compile C sources ─────────────────────────────────────────────────────────
%.o: %.c
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Assemble ASM sources ──────────────────────────────────────────────────────
%.o: %.asm
	@echo "  AS  $<"
	@$(AS) $(ASFLAGS) $< -o $@

# ── Link kernel ───────────────────────────────────────────────────────────────
$(KERNEL): $(OBJS) kernel/linker.ld | dist
	@echo "  LD  $@"
	@$(LD) $(LDFLAGS) $(OBJS) -o $@

# ── Build ISO image ───────────────────────────────────────────────────────────
$(ISO): $(KERNEL) fetch-limine limine.conf | dist
	@echo "  ISO building..."
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/limine
	@mkdir -p $(ISO_DIR)/EFI/BOOT

	@cp $(KERNEL)       $(ISO_DIR)/boot/kernel.elf
	@cp limine.conf     $(ISO_DIR)/boot/limine/limine.conf
	@cp $(LIMINE_DIR)/limine-bios.sys      $(ISO_DIR)/boot/limine/
	@cp $(LIMINE_DIR)/limine-bios-cd.bin   $(ISO_DIR)/boot/limine/
	@cp $(LIMINE_DIR)/limine-uefi-cd.bin   $(ISO_DIR)/boot/limine/
	@cp $(LIMINE_DIR)/BOOTX64.EFI          $(ISO_DIR)/EFI/BOOT/
	@cp $(LIMINE_DIR)/BOOTIA32.EFI         $(ISO_DIR)/EFI/BOOT/ 2>/dev/null || true

	@xorriso -as mkisofs                              \
	    -b boot/limine/limine-bios-cd.bin             \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    --efi-boot boot/limine/limine-uefi-cd.bin     \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(ISO_DIR) -o $(ISO) 2>/dev/null

	@$(LIMINE_DIR)/limine bios-install $(ISO)
	@echo "  OK  $(ISO)"

# ── Run in QEMU ───────────────────────────────────────────────────────────────
run: $(ISO) disk
	@echo "[QEMU] Booting FunnyOS..."
	qemu-system-x86_64                                            \
	    -cdrom $(ISO)                                             \
	    -drive file=$(DISK),format=raw,if=ide,index=0             \
	    -m 256M                                                   \
	    -cpu qemu64                                               \
	    -drive if=pflash,format=raw,readonly=on,file=/usr/share/ovmf/OVMF.fd 2>/dev/null || \
	qemu-system-x86_64                                            \
	    -cdrom $(ISO)                                             \
	    -drive file=$(DISK),format=raw,if=ide,index=0             \
	    -m 256M                                                   \
	    -cpu qemu64

# ── Debug with GDB ────────────────────────────────────────────────────────────
debug: $(ISO) disk
	qemu-system-x86_64                            \
	    -cdrom $(ISO)                             \
	    -drive file=$(DISK),format=raw,if=ide,index=0 \
	    -m 256M                                   \
	    -cpu qemu64                               \
	    -s -S &
	gdb -ex "target remote :1234" \
	    -ex "symbol-file $(KERNEL)"

# ── Directories ───────────────────────────────────────────────────────────────
dist:
	@mkdir -p dist

# ── Clean ─────────────────────────────────────────────────────────────────────────
clean:
	@rm -rf dist $(ISO_DIR)
	@find kernel -name "*.o" -delete
	@echo "  Cleaned.  (disk.img preserved – use 'make clean-disk' to wipe files)"

clean-disk:
	@rm -f $(DISK)
	@echo "  Deleted disk image."
