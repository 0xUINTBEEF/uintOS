## Overview
uintOS is an educational operating system demonstrating OS concepts, kernel development, and hardware interaction.

## Key Features
- **Virtualization**: Hardware-accelerated VM support via Intel VT-x with EPT
- **HAL**: Cross-architecture hardware abstraction layer
- **Memory Management**: Paging, heap allocation, and protection
- **Multitasking**: Preemptive scheduling with priority-based task management
- **File Systems**: VFS with FAT12, ext2, and ISO 9660 support
- **Graphics**: VGA text mode with basic windowing and GUI framework
- **Security**: Capability-based access control and resource isolation
- **Power Management**: ACPI-compatible power states and thermal monitoring
- **Drivers**: Comprehensive driver support for various device types including:
  - Display: Intel GPU with 2D acceleration
  - Input: PS/2 mouse interface
  - Storage: NVMe high-speed SSD support
  - Network: RTL8139 Ethernet and RTL8821CE WiFi chipsets
  - USB: Mass storage devices
  - Audio: AC97 audio chipset

## Directory Structure
```
bootloader/ - Boot code and initialization
hal/        - Hardware Abstraction Layer
kernel/     - Core OS functionality
memory/     - Memory management
filesystem/ - VFS and filesystem drivers
network/    - Network protocol stack
drivers/    - Device drivers
  ├── audio/    - Audio device drivers
  ├── display/  - Display and GPU drivers
  ├── input/    - Keyboard, mouse, and input devices
  ├── network/  - Ethernet and wireless drivers
  ├── pci/      - PCI bus interface
  ├── storage/  - Disk and storage controllers
  ├── usb/      - USB controllers and devices
  └── windows/  - Windows driver compatibility
```

## Key Commands
- `help` - List commands
- `vm create/start/list` - Virtual machine management
- `meminfo`, `taskinfo` - System information
- `gui start/shutdown` - GUI subsystem control
- `power [state]` - Power management
- `driver list/load/unload` - Driver management commands

## Building & Running
1. Install x86 cross-compiler
2. Run `make`
3. Launch with `qemu-system-x86_64 -kernel kernel/kernel`

## Virtualization Architecture

uintOS features native hardware virtualization using Intel VT-x (VMX) extensions. Our implementation:

- Uses CPU hardware virtualization features directly (no QEMU dependency)
- Supports Extended Page Tables (EPT) for memory virtualization
- Handles VM lifecycle management (create, start, pause, resume, stop)
- Implements virtual device emulation

Note that while QEMU is used for development/testing purposes (to boot uintOS itself), the virtualization system within uintOS is completely independent and doesn't rely on QEMU.

## License
MIT License

---
*Last Updated: May 11, 2025*
