## Overview
uintOS is an educational operating system demonstrating OS concepts, kernel development, and hardware interaction.

## Key Components
- **HAL**: Hardware Abstraction Layer supporting multiple architectures
- **Memory Management**: Paging, heap allocation, and memory protection
- **Multitasking**: Preemptive scheduler with task/thread management
- **File System**: Unified VFS layer with FAT12, ext2, and ISO 9660 support
- **UI**: Shell interface with VGA text mode graphics
- **Logging**: Multi-level logging system with configurable outputs
- **IRQ**: Priority-based interrupt handling for both PIC and APIC

## Project Structure
```
bootloader/ - Boot code and initialization
hal/        - Hardware Abstraction Layer
kernel/     - Core OS functionality
memory/     - Memory management systems
filesystem/ - VFS implementation and filesystem drivers
network/    - Network stack and protocols
drivers/    - Device drivers
```

## Common Commands
- `help` - List available commands
- `ls [path]`, `cat [file]`, `cd [path]` - File navigation
- `meminfo`, `taskinfo` - System information
- `mount` - Show mounted filesystems
- `log [level]` - Manage system logs
- `reboot` - Restart system
- `log [code]` - Test Panic Codes

## Building the OS
1. Install a cross-compiler for x86
2. Run `make` from the project root
3. Launch with `qemu-system-x86_64 -kernel kernel/kernel`

## License
MIT License

---
*Last Updated: May 5, 2025*
