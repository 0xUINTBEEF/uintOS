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

## Directory Structure
```
bootloader/ - Boot code and initialization
hal/        - Hardware Abstraction Layer
kernel/     - Core OS functionality
memory/     - Memory management
filesystem/ - VFS and filesystem drivers
network/    - Network protocol stack
drivers/    - Device drivers
```

## Key Commands
- `help` - List commands
- `vm create/start/list` - Virtual machine management
- `meminfo`, `taskinfo` - System information
- `gui start/shutdown` - GUI subsystem control
- `power [state]` - Power management

## Building & Running
1. Install x86 cross-compiler
2. Run `make`
3. Launch with `qemu-system-x86_64 -kernel kernel/kernel`

## License
MIT License

---
*Last Updated: May 11, 2025*
