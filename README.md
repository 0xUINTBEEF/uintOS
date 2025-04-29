# uintOS

## Overview
uintOS is an educational operating system that demonstrates OS concepts, kernel development, and hardware interaction.

## Core Features
- **HAL**: Hardware Abstraction Layer for multiple architectures
- **Memory**: Paging, heap management, memory safety
- **Input/Output**: Keyboard driver and VGA text mode support
- **Multitasking**: Preemptive scheduling with task management
- **UI**: Interactive shell and text-based interface
- **File System**: FAT12, ext2, and ISO 9660 support
- **Debugging**: Built-in tools and error reporting

## Latest Enhancements
- **HAL Improvements**: Better hardware abstraction and portability
- **Enhanced IRQ System**: Priority-based handlers, diagnostics, debugging tools
- **VGA Graphics**: Text mode UI elements with color support
- **Memory Safety**: Corruption detection and improved allocation
- **File System**: Added ext2 and ISO 9660 support
- **UI Enhancements**: Color-coded output and improved commands

## IRQ System
- Multiple priority-based handlers per IRQ vector
- Detailed error diagnostics for CPU exceptions
- IRQ statistics and debugging tools
- Support for both PIC and APIC interrupt controllers

## Project Structure
- `bootloader/`: Bootloader code
- `hal/`: Hardware Abstraction Layer
- `kernel/`: Core OS code and drivers
- `memory/`: Memory management
- `filesystem/`: File system implementation
- `test/`: Testing framework

## Commands
- `help`: Show commands
- `clear`: Clear screen
- `ls`: List directory
- `cat`: Show file
- `meminfo`: Memory info
- `taskinfo`: Process info
- `reboot`: Restart system

## Build
1. Install cross-compiler
2. Run `make` to build
3. Run with `qemu-system-x86_64 -kernel kernel/kernel`

## License
MIT License

---
*Last Updated: April 29, 2025*
