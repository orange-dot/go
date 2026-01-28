#!/usr/bin/env python3
"""
mksd.py - EK-KOR2 SD Card Preparation Tool

Creates a dual-partition SD card layout for Raspberry Pi 3B+ with EK-KOR2:
  - Partition 1: FAT32 (4MB) - Boot files (bootcode.bin, start.elf, kernel8.img, config.txt)
  - Partition 2: EKKFS (remaining) - Custom filesystem for config, logs, state

Usage:
    Linux/macOS:
        sudo python3 mksd.py /dev/sdX
        sudo python3 mksd.py /dev/sdX --boot-files /path/to/boot/files

    Windows (requires elevated privileges):
        python mksd.py \\.\PhysicalDrive1

WARNING: This tool will DESTROY ALL DATA on the target device!

Copyright (c) 2026 Elektrokombinacija
License: MIT
"""

import argparse
import os
import sys
import struct
import time

# EKKFS Constants
EKKFS_MAGIC = 0x454B4653  # "EKFS"
EKKFS_VERSION = 1
EKKFS_BLOCK_SIZE = 512
EKKFS_DEFAULT_INODES = 256
EKKFS_INODES_PER_BLOCK = 8

# MBR Constants
MBR_SIGNATURE = 0xAA55
PART_TYPE_FAT32_LBA = 0x0C
PART_TYPE_EKKFS = 0xEE  # Custom type for EKKFS


def create_mbr(boot_partition_sectors, ekkfs_start_lba, ekkfs_sectors):
    """Create MBR with two partitions: FAT32 boot and EKKFS data."""
    mbr = bytearray(512)

    # Boot code (jump to partition table)
    mbr[0:3] = b'\xEB\x58\x90'  # JMP SHORT 0x5A; NOP

    # Partition 1: FAT32 boot (starts at sector 2048 for alignment)
    part1_start = 2048
    part1_offset = 446
    mbr[part1_offset + 0] = 0x80  # Bootable
    mbr[part1_offset + 1:4] = b'\x00\x00\x00'  # CHS start (ignored)
    mbr[part1_offset + 4] = PART_TYPE_FAT32_LBA
    mbr[part1_offset + 5:8] = b'\x00\x00\x00'  # CHS end (ignored)
    struct.pack_into('<I', mbr, part1_offset + 8, part1_start)  # LBA start
    struct.pack_into('<I', mbr, part1_offset + 12, boot_partition_sectors)  # Sector count

    # Partition 2: EKKFS
    part2_offset = 446 + 16
    mbr[part2_offset + 0] = 0x00  # Not bootable
    mbr[part2_offset + 1:4] = b'\x00\x00\x00'  # CHS start
    mbr[part2_offset + 4] = PART_TYPE_EKKFS
    mbr[part2_offset + 5:8] = b'\x00\x00\x00'  # CHS end
    struct.pack_into('<I', mbr, part2_offset + 8, ekkfs_start_lba)  # LBA start
    struct.pack_into('<I', mbr, part2_offset + 12, ekkfs_sectors)  # Sector count

    # MBR signature
    struct.pack_into('<H', mbr, 510, MBR_SIGNATURE)

    return bytes(mbr)


def crc32(data):
    """Calculate CRC32 using the same polynomial as EKKFS."""
    crc = 0xFFFFFFFF
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            if c & 1:
                c = 0xEDB88320 ^ (c >> 1)
            else:
                c = c >> 1
        table.append(c)

    for byte in data:
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8)

    return crc ^ 0xFFFFFFFF


def create_ekkfs_superblock(total_blocks, inode_count=EKKFS_DEFAULT_INODES):
    """Create EKKFS superblock."""
    # Calculate layout
    inode_blocks = (inode_count + EKKFS_INODES_PER_BLOCK - 1) // EKKFS_INODES_PER_BLOCK
    data_blocks_estimate = total_blocks - 1 - inode_blocks - 2  # -superblock -inodes -bitmap -journal
    bitmap_blocks = (data_blocks_estimate + (8 * EKKFS_BLOCK_SIZE) - 1) // (8 * EKKFS_BLOCK_SIZE)
    if bitmap_blocks < 1:
        bitmap_blocks = 1

    inode_start = 1
    bitmap_start = 1 + inode_blocks
    journal_start = bitmap_start + bitmap_blocks
    data_start = journal_start + 1
    free_blocks = total_blocks - data_start

    # Build superblock (512 bytes)
    sb = bytearray(512)

    struct.pack_into('<I', sb, 0, EKKFS_MAGIC)         # magic
    struct.pack_into('<I', sb, 4, EKKFS_VERSION)       # version
    struct.pack_into('<I', sb, 8, EKKFS_BLOCK_SIZE)    # block_size
    struct.pack_into('<I', sb, 12, total_blocks)       # total_blocks
    struct.pack_into('<I', sb, 16, inode_count)        # inode_count
    struct.pack_into('<I', sb, 20, inode_start)        # inode_start
    struct.pack_into('<I', sb, 24, bitmap_start)       # bitmap_start
    struct.pack_into('<I', sb, 28, journal_start)      # journal_start
    struct.pack_into('<I', sb, 32, data_start)         # data_start
    struct.pack_into('<I', sb, 36, free_blocks)        # free_blocks
    struct.pack_into('<Q', sb, 40, int(time.time() * 1000000))  # mount_time
    struct.pack_into('<I', sb, 48, 0)                  # mount_count

    # Calculate CRC (of first 52 bytes, before crc32 field)
    crc = crc32(sb[:52])
    struct.pack_into('<I', sb, 52, crc)                # crc32

    return bytes(sb), inode_blocks, bitmap_blocks


def create_fat32_boot_sector(volume_size_sectors, sectors_per_cluster=8):
    """Create a minimal FAT32 boot sector."""
    bs = bytearray(512)

    # Jump instruction
    bs[0:3] = b'\xEB\x58\x90'

    # OEM name
    bs[3:11] = b'EKKBOOT '

    # BPB (BIOS Parameter Block)
    bytes_per_sector = 512
    reserved_sectors = 32
    num_fats = 2
    root_entries = 0  # FAT32 uses 0
    total_sectors_16 = 0  # Use 32-bit field
    media_type = 0xF8  # Fixed disk
    sectors_per_fat_16 = 0  # Use 32-bit field
    sectors_per_track = 63
    num_heads = 255
    hidden_sectors = 2048  # Aligned start
    total_sectors_32 = volume_size_sectors

    # Calculate sectors per FAT
    data_sectors = total_sectors_32 - reserved_sectors
    clusters = data_sectors // sectors_per_cluster
    sectors_per_fat = ((clusters * 4) + 511) // 512 + 1

    struct.pack_into('<H', bs, 11, bytes_per_sector)
    bs[13] = sectors_per_cluster
    struct.pack_into('<H', bs, 14, reserved_sectors)
    bs[16] = num_fats
    struct.pack_into('<H', bs, 17, root_entries)
    struct.pack_into('<H', bs, 19, total_sectors_16)
    bs[21] = media_type
    struct.pack_into('<H', bs, 22, sectors_per_fat_16)
    struct.pack_into('<H', bs, 24, sectors_per_track)
    struct.pack_into('<H', bs, 26, num_heads)
    struct.pack_into('<I', bs, 28, hidden_sectors)
    struct.pack_into('<I', bs, 32, total_sectors_32)

    # FAT32 specific
    struct.pack_into('<I', bs, 36, sectors_per_fat)  # sectors per FAT
    struct.pack_into('<H', bs, 40, 0)  # flags
    struct.pack_into('<H', bs, 42, 0)  # version
    struct.pack_into('<I', bs, 44, 2)  # root cluster
    struct.pack_into('<H', bs, 48, 1)  # FSInfo sector
    struct.pack_into('<H', bs, 50, 6)  # backup boot sector

    # Extended BPB
    bs[64] = 0x80  # drive number
    bs[66] = 0x29  # extended boot signature
    struct.pack_into('<I', bs, 67, 0x12345678)  # volume serial
    bs[71:82] = b'EK-KOR BOOT'  # volume label
    bs[82:90] = b'FAT32   '  # filesystem type

    # Boot signature
    struct.pack_into('<H', bs, 510, 0xAA55)

    return bytes(bs)


def format_sd_card(device, boot_files_dir=None, total_size_mb=None):
    """Format an SD card with EK-KOR2 dual-partition layout."""

    print(f"EK-KOR2 SD Card Preparation Tool")
    print(f"=" * 50)
    print(f"Target device: {device}")

    # Check if device exists
    if not os.path.exists(device):
        print(f"ERROR: Device {device} not found")
        return 1

    # Get device size
    try:
        if sys.platform == 'win32':
            # Windows: use size from parameter or default
            if total_size_mb:
                device_size = total_size_mb * 1024 * 1024
            else:
                print("WARNING: Cannot auto-detect size on Windows, using 32MB")
                device_size = 32 * 1024 * 1024
        else:
            # Linux/macOS
            with open(device, 'rb') as f:
                f.seek(0, 2)
                device_size = f.tell()
    except PermissionError:
        print("ERROR: Permission denied. Run with sudo/administrator privileges.")
        return 1
    except Exception as e:
        print(f"ERROR: Cannot read device: {e}")
        return 1

    total_sectors = device_size // 512
    print(f"Device size: {device_size / (1024*1024):.1f} MB ({total_sectors} sectors)")

    if total_sectors < 16384:  # Minimum 8MB
        print("ERROR: Device too small (minimum 8MB)")
        return 1

    # Partition layout
    boot_size_mb = 4
    boot_sectors = (boot_size_mb * 1024 * 1024) // 512
    boot_start = 2048  # Standard alignment

    ekkfs_start = boot_start + boot_sectors
    ekkfs_sectors = total_sectors - ekkfs_start - 100  # Leave some slack

    print(f"\nPartition layout:")
    print(f"  P1 (FAT32 boot): LBA {boot_start}, {boot_sectors} sectors ({boot_size_mb} MB)")
    print(f"  P2 (EKKFS):      LBA {ekkfs_start}, {ekkfs_sectors} sectors ({ekkfs_sectors * 512 / (1024*1024):.1f} MB)")

    # Confirm
    print(f"\nWARNING: This will DESTROY ALL DATA on {device}!")
    response = input("Continue? [y/N]: ")
    if response.lower() != 'y':
        print("Aborted.")
        return 1

    print("\nFormatting...")

    try:
        with open(device, 'r+b') as f:
            # Write MBR
            print("  Writing MBR...")
            mbr = create_mbr(boot_sectors, ekkfs_start, ekkfs_sectors)
            f.seek(0)
            f.write(mbr)

            # Write FAT32 boot sector
            print("  Writing FAT32 boot sector...")
            fat_bs = create_fat32_boot_sector(boot_sectors)
            f.seek(boot_start * 512)
            f.write(fat_bs)

            # Write FSInfo sector
            print("  Writing FAT32 FSInfo...")
            fsinfo = bytearray(512)
            struct.pack_into('<I', fsinfo, 0, 0x41615252)  # Signature
            struct.pack_into('<I', fsinfo, 484, 0x61417272)  # Signature
            struct.pack_into('<I', fsinfo, 488, 0xFFFFFFFF)  # Free clusters (unknown)
            struct.pack_into('<I', fsinfo, 492, 0xFFFFFFFF)  # Next free cluster
            struct.pack_into('<H', fsinfo, 510, 0xAA55)
            f.seek((boot_start + 1) * 512)
            f.write(fsinfo)

            # Write EKKFS superblock
            print("  Writing EKKFS superblock...")
            sb, inode_blocks, bitmap_blocks = create_ekkfs_superblock(ekkfs_sectors)
            f.seek(ekkfs_start * 512)
            f.write(sb)

            # Write empty inode blocks
            print(f"  Writing {inode_blocks} empty inode blocks...")
            empty_block = bytes(512)
            for i in range(inode_blocks):
                f.seek((ekkfs_start + 1 + i) * 512)
                f.write(empty_block)

            # Write empty bitmap
            print(f"  Writing {bitmap_blocks} bitmap blocks...")
            bitmap_start = 1 + inode_blocks
            for i in range(bitmap_blocks):
                f.seek((ekkfs_start + bitmap_start + i) * 512)
                f.write(empty_block)

            # Write empty journal
            print("  Writing journal block...")
            journal_start = bitmap_start + bitmap_blocks
            f.seek((ekkfs_start + journal_start) * 512)
            f.write(empty_block)

            f.flush()
            os.fsync(f.fileno())

    except PermissionError:
        print("ERROR: Permission denied. Run with sudo/administrator privileges.")
        return 1
    except Exception as e:
        print(f"ERROR: Write failed: {e}")
        return 1

    print("\nSD card prepared successfully!")
    print("\nNext steps:")
    print("  1. Mount the FAT32 partition (P1)")
    print("  2. Copy boot files: bootcode.bin, start.elf, kernel8.img, config.txt")
    print("  3. Eject and insert into Raspberry Pi 3")
    print("  4. EKKFS will auto-mount from P2")

    if boot_files_dir and os.path.isdir(boot_files_dir):
        print(f"\nWould copy boot files from: {boot_files_dir}")
        # Note: Actually copying files requires mounting the FAT32 partition
        # which is OS-specific and beyond the scope of this script

    return 0


def main():
    parser = argparse.ArgumentParser(
        description='EK-KOR2 SD Card Preparation Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Linux:    sudo python3 mksd.py /dev/sdb
  macOS:    sudo python3 mksd.py /dev/disk2
  Windows:  python mksd.py \\\\.\\PhysicalDrive1

WARNING: This tool will DESTROY ALL DATA on the target device!
        """
    )

    parser.add_argument('device', help='Target device (e.g., /dev/sdb, /dev/disk2)')
    parser.add_argument('--boot-files', '-b', metavar='DIR',
                        help='Directory containing boot files')
    parser.add_argument('--size', '-s', type=int, metavar='MB',
                        help='Total size in MB (auto-detected if not specified)')

    args = parser.parse_args()

    return format_sd_card(args.device, args.boot_files, args.size)


if __name__ == '__main__':
    sys.exit(main())
