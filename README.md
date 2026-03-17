# FunnyOS

A minimal x86-64 operating system written in NASM assembly and C.  
Boots via the **Limine** bootloader directly into a black-background shell — no GUI, no desktop, just a terminal.

```
  ███████╗██╗   ██╗███╗   ██╗███╗   ██╗██╗   ██╗ ██████╗ ███████╗
  ██╔════╝██║   ██║████╗  ██║████╗  ██║╚██╗ ██╔╝██╔═══██╗██╔════╝
  █████╗  ██║   ██║██╔██╗ ██║██╔██╗ ██║ ╚████╔╝ ██║   ██║███████╗
  ██╔══╝  ██║   ██║██║╚██╗██║██║╚██╗██║  ╚██╔╝  ██║   ██║╚════██║
  ██║     ╚██████╔╝██║ ╚████║██║ ╚████║   ██║   ╚██████╔╝███████║
  ╚═╝      ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═══╝   ╚═╝    ╚═════╝ ╚══════╝
```

---

## Features

| Feature | Description |
|---|---|
| **Architecture** | x86-64 long mode |
| **Bootloader** | Limine (v8, BIOS + UEFI) |
| **Display** | Linear framebuffer with embedded 8×16 bitmap font |
| **Keyboard** | PS/2 keyboard via IRQ1 (scan code set 1) |
| **Memory** | Physical memory manager with free-list allocator |
| **CPU** | GDT (5 descriptors), IDT (exceptions + IRQ 0-15), PIC remapped |
| **Shell** | Interactive shell with history |
| **RAM FS** | Simple in-memory named file store |
| **Compiler** | Built-in tiny C compiler → x86-64 machine code |

### Shell Commands

```
help              Show all commands
clear             Clear screen
echo [text]       Print text
ls                List RAM filesystem files
cat [file]        Print a file
write [file]      Write text to a file (end with a line containing just '.')
rm [file]         Delete a file
meminfo           Show memory statistics
about             Show OS info / ASCII art
cc [file.c]       Compile a C source file to x86-64 machine code
run [file.bin]    Execute compiled binary
reboot            Reboot the machine
halt              Halt the system
```

### Built-in C Compiler (tcc)

The `cc` command compiles a restricted C subset directly in the kernel:

```c
// example.c  –  type this with 'write example.c'
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int i;
    for (i = 0; i < 10; i++) {
        printf("%d ", fib(i));
    }
    printf("\n");
    return 0;
}
```

Then:
```
funnyos> write example.c
funnyos> cc example.c
funnyos> run example.bin
```

**Supported language subset:**
- Types: `int`, `char`, `char*`
- Operators: `+ - * / %  == != < > <= >=  && ||  !  = (assign)`
- Control flow: `if/else`, `while`, `for`, `return`
- Functions: definitions + calls (up to 6 int parameters)
- Built-ins: `printf()`, `putchar()`, `getchar()`
- String literals

---

## Project Structure

```
FunnyOS/
├── Makefile                  Build system
├── limine.conf               Limine bootloader configuration
├── .gitignore
├── README.md
└── kernel/
    ├── linker.ld             Linker script (higher-half kernel)
    └── src/
        ├── kernel.c          Kernel entry – init sequence
        ├── limine.h          Limine protocol header
        ├── boot/
        │   └── entry.asm     _start: sets up stack, calls kmain()
        ├── cpu/
        │   ├── cpu.h         GDT / IDT / register types
        │   ├── cpu.c         GDT + IDT init, ISR/IRQ handlers
        │   └── cpu_asm.asm   gdt_flush, idt_flush, ISR/IRQ stubs
        ├── mm/
        │   ├── pmm.h
        │   └── pmm.c         Bump + free-list allocator
        ├── lib/
        │   ├── string.h/c    kstrlen, kstrcpy, kmemcpy, kitoa …
        │   └── printf.h/c    kprintf, kputs, kputchar
        ├── terminal/
        │   ├── terminal.h/c  Framebuffer text renderer + scrolling
        │   └── font.h        Embedded 8×16 bitmap font (ASCII 0-127)
        ├── drivers/
        │   ├── keyboard.h/c  PS/2 keyboard driver, IRQ1, readline
        ├── shell/
        │   ├── shell.h/c     Interactive shell + RAM filesystem
        └── compiler/
            ├── compiler.h
            └── compiler.c    Tiny C compiler (lexer+parser+x86-64 codegen)
```

---

## Prerequisites

---

### Windows (MSYS2 / MinGW64)  ← **Start here if you're on Windows**

Everything runs inside **MSYS2** — a Unix-like environment for Windows that ships with `pacman`, `bash`, `make`, and lets you build the cross-compiler natively.

#### Step 1 — Install MSYS2

Download and run the installer from **https://www.msys2.org**  
After installation, open the **"MSYS2 MinGW64"** shell from the Start menu.

#### Step 2 — Update and install base packages

Since you already have MinGW64 GCC on your PATH, you can skip the gcc package. You still need the GMP/MPFR/MPC libraries to build the cross-compiler from source.

```bash
# Update package database and all installed packages
pacman -Syu

# Close the terminal when prompted, reopen MinGW64, then run again
pacman -Syu

# Install all required build prerequisites
# (skip mingw-w64-x86_64-gcc if you already have it)
pacman -S --needed \
    base-devel \
    mingw-w64-x86_64-make \
    nasm \
    xorriso \
    git \
    wget \
    mingw-w64-x86_64-gmp \
    mingw-w64-x86_64-mpfr \
    mingw-w64-x86_64-mpc \
    mingw-w64-x86_64-isl
```

#### Step 3 — Install QEMU

Download the QEMU Windows installer from **https://www.qemu.org/download/#windows**  
Install it, then add QEMU to your MSYS2 PATH:

```bash
# Add QEMU to PATH (adjust version/path if different)
echo 'export PATH="/c/Program Files/qemu:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verify
qemu-system-x86_64 --version
```

#### Step 4 — Build the x86_64-elf cross-compiler

This is the only step that takes a while (~15–30 min).  
Run everything inside the **MSYS2 MinGW64** shell.

```bash
# Set up environment variables
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

# Create build directory
mkdir -p "$HOME/cross-build"
cd "$HOME/cross-build"
```

**Build Binutils:**
```bash
wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
tar xf binutils-2.42.tar.gz
mkdir binutils-build && cd binutils-build

../binutils-2.42/configure \
    --target=$TARGET \
    --prefix=$PREFIX \
    --with-sysroot \
    --disable-nls \
    --disable-werror

make -j$(nproc)
make install
cd ..
```

**Build GCC:**
```bash
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz
tar xf gcc-13.2.0.tar.gz
mkdir gcc-build && cd gcc-build

../gcc-13.2.0/configure \
    --target=$TARGET \
    --prefix=$PREFIX \
    --disable-nls \
    --disable-fixincludes \
    --enable-languages=c \
    --without-headers

make -j$(nproc) all-gcc all-target-libgcc all-libiberty
make install-gcc install-target-libgcc
cd ..
```

**Make the cross-compiler permanent:**
```bash
echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verify everything is working
x86_64-elf-gcc --version
x86_64-elf-ld --version
nasm --version
xorriso --version
```

#### Step 5 — Build and run FunnyOS

```bash
# Clone the repo (or cd into your workspace folder)
cd /c/Users/funny/FunnyOS

# Build everything (fetches Limine, compiles, links, creates ISO)
make

# Run in QEMU
make run
```

#### Quick-reference: all Windows commands in order

```bash
# ── One-time setup (paste into MSYS2 MinGW64) ────────────────────────────

pacman -Syu
# skip mingw-w64-x86_64-gcc if you already have MinGW64 GCC on PATH
pacman -S --needed base-devel mingw-w64-x86_64-make nasm xorriso git wget \
    mingw-w64-x86_64-gmp mingw-w64-x86_64-mpfr \
    mingw-w64-x86_64-mpc mingw-w64-x86_64-isl

export PREFIX="$HOME/opt/cross"; export TARGET=x86_64-elf
mkdir -p ~/cross-build && cd ~/cross-build

# Binutils
wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
tar xf binutils-2.42.tar.gz && mkdir binutils-build && cd binutils-build
../binutils-2.42/configure --target=$TARGET --prefix=$PREFIX \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && make install && cd ..

# GCC
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz
tar xf gcc-13.2.0.tar.gz && mkdir gcc-build && cd gcc-build
../gcc-13.2.0/configure --target=$TARGET --prefix=$PREFIX \
    --disable-nls --disable-fixincludes --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc all-libiberty
make install-gcc install-target-libgcc && cd ..

echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# ── Every time you build FunnyOS ──────────────────────────────────────────

cd /c/Users/funny/FunnyOS
make        # build ISO
make run    # boot in QEMU
make clean  # clean all build artifacts
```

> **Tip:** Always use the **MSYS2 MinGW64** shell, not PowerShell or CMD, for all build commands.

---

### Linux / macOS

#### 1. Cross-compiler: `x86_64-elf-gcc` + `x86_64-elf-ld`

A cross-compiler targeting `x86_64-elf` (bare metal, no OS) is required.

**Option A – Pre-built packages (Linux)**

```bash
# Arch Linux (AUR)
yay -S x86_64-elf-gcc x86_64-elf-binutils

# Nix (nixpkgs)
nix-env -iA nixpkgs.pkgsCross.x86_64-embedded.buildPackages.gcc
```

**Option B – Build from source (any Linux/macOS/WSL)**

```bash
# Install dependencies first
sudo apt install build-essential bison flex libgmp-dev \
                 libmpc-dev libmpfr-dev texinfo wget

export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

# Binutils
mkdir /tmp/binutils-build && cd /tmp/binutils-build
wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
tar xf binutils-2.42.tar.gz
mkdir build && cd build
../binutils-2.42/configure --target=$TARGET --prefix=$PREFIX \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
make install

# GCC (C + C++ frontends)
cd /tmp
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz
tar xf gcc-13.2.0.tar.gz
mkdir gcc-build && cd gcc-build
../gcc-13.2.0/configure --target=$TARGET --prefix=$PREFIX \
    --disable-nls --disable-fixincludes --enable-languages=c,c++ \
    --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

Add to your shell profile:
```bash
export PATH="$HOME/opt/cross/bin:$PATH"
```

#### 2. NASM

```bash
# Debian / Ubuntu / WSL
sudo apt install nasm

# Fedora
sudo dnf install nasm

# Arch
sudo pacman -S nasm

# macOS
brew install nasm
```

#### 3. xorriso  (ISO creation)

```bash
sudo apt install xorriso      # Debian/Ubuntu/WSL
sudo dnf install xorriso      # Fedora
sudo pacman -S libisoburn     # Arch
brew install xorriso          # macOS
```

#### 4. QEMU  (emulator / testing)

```bash
sudo apt install qemu-system-x86    # Debian/Ubuntu/WSL
sudo dnf install qemu-system-x86    # Fedora
sudo pacman -S qemu-full            # Arch
brew install qemu                   # macOS
```

#### 5. Git  (for fetching Limine)

```bash
sudo apt install git   # or equivalent
```

#### Summary table

| Tool | Ubuntu/Debian | Arch | macOS (brew) |
|---|---|---|---|
| NASM | `apt install nasm` | `pacman -S nasm` | `brew install nasm` |
| xorriso | `apt install xorriso` | `pacman -S libisoburn` | `brew install xorriso` |
| QEMU | `apt install qemu-system-x86` | `pacman -S qemu-full` | `brew install qemu` |
| Git | `apt install git` | `pacman -S git` | `brew install git` |
| GCC cross | build from source (see above) | `yay -S x86_64-elf-gcc` | build from source |

#### Build and run

```bash
cd FunnyOS
make        # build ISO
make run    # boot in QEMU
make clean  # clean artifacts
```

---

## Building

### Windows (MSYS2 MinGW64 shell)

```bash
# Open "MSYS2 MinGW64" from the Start menu

# 1. Make sure cross-compiler is on PATH (if not already in .bashrc)
export PATH="$HOME/opt/cross/bin:$PATH"

# 2. Navigate to the project
cd /c/Users/funny/FunnyOS

# 3. Build everything (clones Limine, compiles, links, creates ISO)
make

# 4. Boot in QEMU
make run

# 5. Clean
make clean
```

### Linux / macOS

```bash
# 1. Clone (or navigate to) FunnyOS
cd FunnyOS

# 2. Make sure your cross-compiler is on PATH
export PATH="$HOME/opt/cross/bin:$PATH"

# 3. Build everything (fetches Limine automatically)
make

# 4. Boot in QEMU
make run
```

The `make` command will:
1. Clone the Limine binary release into `./limine/`
2. Compile all `.c` files with `x86_64-elf-gcc`
3. Assemble all `.asm` files with `nasm`
4. Link into `dist/kernel.elf`
5. Build a bootable ISO at `dist/funnyos.iso` using `xorriso`
6. Install the Limine BIOS bootloader into the ISO MBR

---

## Running on Real Hardware

```bash
# Write the ISO to a USB drive (replace /dev/sdX with your drive)
sudo dd if=dist/funnyos.iso of=/dev/sdX bs=1M status=progress
sync
```

> **Warning:** Make sure you use the correct drive letter. `dd` will overwrite everything on the target drive.

---

## Development Workflow

### Debug with GDB + QEMU

```bash
make debug
# QEMU starts paused; GDB connects automatically on port 1234
(gdb) break kmain
(gdb) continue
```

### Adding shell commands

Open [kernel/src/shell/shell.c](kernel/src/shell/shell.c) and:
1. Write a handler `static void cmd_mycommand(int argc, char **argv) { ... }`
2. Add an entry to the `commands[]` table at the bottom

### Adding a kernel driver

1. Create `kernel/src/drivers/mydriver.h` and `mydriver.c`
2. Add the `.c` file to `C_SRCS` in the [Makefile](Makefile)
3. Call your `mydriver_init()` from [kernel/src/kernel.c](kernel/src/kernel.c)

---

## Architecture Notes

### Memory Layout

| Region | Address |
|---|---|
| Kernel (higher half) | `0xFFFF_FFFF_8000_0000` + |
| Limine HHDM mapping | `0xFFFF_8000_0000_0000` + (direct map of physical RAM) |
| Kernel heap | Largest usable physical region, mapped via HHDM |

### Interrupt Vectors

| Range | Usage |
|---|---|
| 0–31 | CPU exceptions (divide-by-zero, GPF, page fault, …) |
| 32–47 | Hardware IRQs 0–15 (remapped via 8259 PIC) |
| IRQ 0 (vec 32) | PIT timer |
| IRQ 1 (vec 33) | PS/2 keyboard |

### Calling Convention

The kernel uses the standard **System V AMD64 ABI**:
- First 6 integer args: `rdi, rsi, rdx, rcx, r8, r9`
- Return value: `rax`
- Caller-saved: `rax, rcx, rdx, rsi, rdi, r8-r11`
- Callee-saved: `rbx, rbp, r12-r15`

---

## Roadmap / Ideas

- [ ] ATA/IDE driver for real disk access  
- [ ] FAT32 filesystem  
- [ ] Basic paging / virtual memory  
- [ ] Multitasking (round-robin scheduler)  
- [ ] Network stack (RTL8139 driver)  
- [ ] Expand the C compiler (pointers, arrays, structs)  
- [ ] ELF loader to run programs from disk  

---

## License

This project is released into the public domain.  Do whatever you want with it.
