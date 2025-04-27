# uintOS

## Overview
uintOS is a simple operating system designed for educational purposes. It demonstrates basic kernel development, task management, and low-level hardware interaction.

## Features
- **Basic Kernel**: Core kernel with task management and interrupt handling.
- **Memory Management**: 
  - Paging support with virtual memory mapping
  - Dynamic memory allocation (heap management with malloc/free)
- **Input/Output**: 
  - Basic I/O operations
  - Keyboard driver with scan code translation
- **Multitasking**: Implemented task creation, switching, and a round-robin scheduler.
- **User Interface**: Interactive command-line shell with multiple built-in commands.
- **Debugging Support**: Integrated GDB stub for debugging.
- **File System Support**: Basic FAT12 file system support.
- **Hardware Integration**: LAPIC timer support for scheduling.

## New Features (April 2025)
- **Interactive Shell**: Added a command-line interface with support for multiple commands.
- **Keyboard Driver**: Implemented keyboard input handling with scan code translation.
- **Dynamic Memory Allocation**: Added heap memory management with malloc, free, realloc, and calloc.

## Project Structure
- `bootloader/`: Contains the bootloader code.
- `kernel/`: Contains the kernel source code and headers.
- `memory/`: Memory management subsystem (paging, heap).
- `filesystem/`: File system implementation.
- `test/`: Contains unit tests and testing framework.

## Shell Commands
- `help`: Display available commands
- `clear`: Clear the screen
- `echo`: Display a message
- `meminfo`: Display memory usage statistics
- `taskinfo`: Show running tasks
- `reboot`: Reboot the system

## Build Instructions
1. Install a cross-compiler for your target architecture.
2. Run `make` in the root directory to build the project.
3. Use QEMU to emulate the OS: `qemu-system-x86_64 -kernel kernel/kernel`.

## Memory Management
The heap management system provides the following features:
- Dynamic memory allocation with standard C functions
- Memory block splitting and merging for efficiency
- Memory usage statistics
- Protection against buffer overflows and double-free errors

## Contribution Guidelines
- Follow consistent naming conventions.
- Write clear and concise comments.
- Ensure all new features are tested.

## License
This project is licensed under the MIT License.
