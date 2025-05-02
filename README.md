# uintOS

## Overview
uintOS is an educational operating system that demonstrates OS concepts, kernel development, and hardware interaction.

## Core Features
- **HAL**: Hardware Abstraction Layer for multiple architectures
- **Memory**: Paging, heap management, memory safety
- **Input/Output**: Keyboard driver and VGA text mode support
- **Multitasking**: Preemptive scheduling with task management
- **UI**: Interactive shell and text-based interface
- **File System**: FAT12, ext2, and ISO 9660 support with a unified VFS layer
- **Debugging**: Built-in tools and error reporting
- **Logging**: Centralized logging system with severity levels and multiple outputs

## Latest Enhancements
- **HAL Improvements**: Better hardware abstraction and portability
- **Enhanced IRQ System**: Priority-based handlers, diagnostics, debugging tools
- **Logging System**: Comprehensive logging with multiple severity levels
- **VGA Graphics**: Text mode UI elements with color support
- **Memory Safety**: Corruption detection and improved allocation
- **File System**: Fully implemented FAT12, ext2, and ISO 9660 with simulated disk images
- **Virtual File System**: Unified filesystem access through VFS abstraction layer
- **UI Enhancements**: Color-coded output and improved commands

## IRQ System
- Multiple priority-based handlers per IRQ vector
- Detailed error diagnostics for CPU exceptions
- IRQ statistics and debugging tools
- Support for both PIC and APIC interrupt controllers

## Logging System
- Nine severity levels from TRACE to EMERGENCY
- Configurable output destinations (screen, memory buffer, serial)
- Customizable formatting options
- Integration with IRQ and error handling
- Log buffer for storing and reviewing system messages
- Interactive shell commands for managing logs

## File System
### Virtual File System (VFS)
- Abstract layer providing unified access to different filesystem types
- Consistent API for file operations (open, read, write, seek)
- Directory operations (list, create, remove)
- Mount point management for combining multiple filesystems

### Supported Filesystems
- **FAT12**: Basic file access with 8.3 filename support
- **ext2**: More advanced Unix-like filesystem with permissions
- **ISO9660**: CD-ROM filesystem with Joliet extensions and El Torito boot support

### Features
- File operations: open, close, read, write, seek
- Directory operations: list, create, delete
- Mount/unmount functionality
- Path normalization and resolution
- Error handling and reporting
- Simulated disk images for educational purposes

## Project Structure
- `bootloader/`: Bootloader code
- `hal/`: Hardware Abstraction Layer
- `kernel/`: Core OS code and drivers
  - `logging/`: Logging subsystem
  - `graphics/`: VGA text mode graphics
- `memory/`: Memory management
- `filesystem/`: File system implementation
  - `vfs/`: Virtual File System layer
  - `fat12/`: FAT12 filesystem implementation
  - `ext2/`: ext2 filesystem implementation
  - `iso9660/`: ISO9660 CD-ROM filesystem implementation
- `test/`: Testing framework

## Commands
- `help`: Show commands
- `clear`: Clear screen
- `ls [path]`: List directory contents
- `cat [file]`: Display file contents
- `cd [path]`: Change directory
- `mkdir [dir]`: Create directory
- `rm [file]`: Remove file
- `meminfo`: Display memory information
- `taskinfo`: Display process information
- `mount`: Show mounted filesystems
- `reboot`: Restart system
- `log [level]`: Manage system logs

## Build
1. Install cross-compiler
2. Run `make` to build
3. Run with `qemu-system-x86_64 -kernel kernel/kernel`

## License
MIT License

---
*Last Updated: May 3, 2025*
