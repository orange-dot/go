# EK-KOR v2 HAL for Raspberry Pi 3B+ (BCM2837B0)

> **⚠️ Experimental:** This HAL was developed because we had RPi3 hardware available for testing. It is NOT the target production platform. Production deployment targets STM32G474 (L1) and AURIX TC375 (L2).

Bare-metal AArch64 HAL for running EK-KOR v2 on the Raspberry Pi 3B+.

## Features

- **4 EK-KOR modules** running on 4 Cortex-A53 cores
- **Shared memory messaging** between cores (no OS needed)
- **ARM Generic Timer** for microsecond-precision timestamps
- **GIC-400** interrupt controller support
- **Mini-UART** debug output (115200 baud on GPIO 14/15)

## Building

### Prerequisites

Install the AArch64 cross-compiler:

```bash
# Ubuntu/Debian
sudo apt install gcc-aarch64-linux-gnu

# Arch Linux
sudo pacman -S aarch64-linux-gnu-gcc

# macOS (Homebrew)
brew install aarch64-elf-gcc
```

### Build with CMake

```bash
cd ek-kor2/c
mkdir build-rpi3 && cd build-rpi3
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64-rpi3.cmake -DEKK_PLATFORM=rpi3
make
```

This produces:
- `kernel8.elf` - ELF file for debugging
- `kernel8.img` - Binary image for SD card

## Running on QEMU

```bash
qemu-system-aarch64 \
    -M raspi3b \
    -kernel kernel8.img \
    -serial stdio \
    -display none
```

Expected output:
```
===========================================
  EK-KOR v2 HAL for Raspberry Pi 3B+
  BCM2837B0 (Cortex-A53 x 4 @ 1.4 GHz)
===========================================
Timer: 19200000 Hz
GIC distributor initialized
Message queues initialized
Core 0 initialization complete
Core 0: HAL initialized (Module ID 1)

Starting secondary cores...
Core 1: HAL initialized (Module ID 2)
Core 2: HAL initialized (Module ID 3)
Core 3: HAL initialized (Module ID 4)
Waiting for all cores...
4 cores ready

EK-KOR v2 ready on Raspberry Pi 3B+
4 cores = 4 EK-KOR modules (IDs 1-4)
Communication: Shared memory message queues
```

## Running on Real Hardware

### SD Card Setup

1. Format SD card as FAT32
2. Download RPi firmware from [raspberrypi/firmware](https://github.com/raspberrypi/firmware/tree/master/boot):
   - `bootcode.bin`
   - `start.elf`
   - `fixup.dat`
3. Create `config.txt`:
   ```
   arm_64bit=1
   enable_uart=1
   kernel=kernel8.img
   ```
4. Copy `kernel8.img` from build directory

### Serial Connection

Connect USB-to-TTL serial adapter (3.3V!):

| RPi3 GPIO | Serial Adapter |
|-----------|----------------|
| GPIO 14 (Pin 8) | RX |
| GPIO 15 (Pin 10) | TX |
| GND (Pin 6) | GND |

Open terminal at **115200 baud, 8N1**.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Shared Memory                                 │
│  ┌─────────────┐  ┌─────────────────────────────────────────┐  │
│  │ Field Region│  │ Per-Core Message Queues                 │  │
│  │   (64KB)    │  │ [Core 0][Core 1][Core 2][Core 3]        │  │
│  └─────────────┘  └─────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
         │                        │
    ┌────┴────┬────────┬──────────┴─────────┐
    ▼         ▼        ▼                    ▼
┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐
│Core 0 │ │Core 1 │ │Core 2 │ │Core 3 │
│ ID=1  │ │ ID=2  │ │ ID=3  │ │ ID=4  │
└───────┘ └───────┘ └───────┘ └───────┘
```

## Files

| File | Description |
|------|-------------|
| `boot.S` | AArch64 boot stub (EL2→EL1, SMP spin table) |
| `linker.ld` | Memory layout (stacks, field region, queues) |
| `ekk_hal_rpi3.c` | Main HAL implementation |
| `uart.c` | Mini-UART debug driver |
| `timer.c` | ARM Generic Timer driver |
| `gic.c` | GIC-400 interrupt controller |
| `smp.c` | Multi-core spin-up |
| `msg_queue.c` | Per-core lock-free message queues |
| `rpi3_hw.h` | BCM2837B0 register definitions |

## Module ID Mapping

| Core | Module ID | HAL Function |
|------|-----------|--------------|
| 0 | 1 | Primary, runs `kernel_main()` |
| 1 | 2 | Secondary |
| 2 | 3 | Secondary |
| 3 | 4 | Secondary |

## Memory Map

| Address | Size | Description |
|---------|------|-------------|
| 0x00080000 | ~512KB | Code + data (kernel8.img) |
| 0x00100000 | 64KB | Per-core stacks (16KB each) |
| 0x00110000 | 64KB | EK-KOR field region |
| 0x00120000 | 256KB | Per-core message queues |
| 0x00160000 | 4MB | Heap |
| 0x3F000000 | - | Peripheral base (MMIO) |

## License

MIT License - Copyright (c) 2026 Elektrokombinacija
