# uintOS

## Overview
uintOS is a sophisticated educational operating system that demonstrates key OS concepts, kernel development techniques, and low-level hardware interaction. It features a robust architecture with memory management, multitasking, filesystem support, and a graphical text-based interface.

## Features
- **Enhanced Kernel**: Core kernel with comprehensive task management, interrupt handling, and error recovery
- **Memory Management**: 
  - Paging support with virtual memory mapping
  - Advanced heap implementation with corruption detection and memory safety features
  - Standard memory allocation functions (malloc, free, realloc, calloc)
  - Memory block tracking with footers for overflow detection
- **Input/Output**: 
  - Hardware-abstracted I/O operations
  - Enhanced keyboard driver with robust scan code translation and key buffering
  - Full VGA text mode support with colors and drawing capabilities
- **Multitasking**: 
  - Preemptive multitasking with task scheduling
  - Named task support with task state management
  - Comprehensive task status information
- **User Interface**: 
  - Interactive shell with colored output and command history
  - VGA-powered text UI with windows, boxes, and other drawing primitives
  - Multiple built-in commands for system management
- **File System**: 
  - FAT12 file system with directory listing and file operations
  - File existence checking and size retrieval
  - File content reading and error handling
- **Debugging Support**: 
  - Integrated GDB stub for debugging
  - Enhanced error reporting and memory validation
- **Hardware Integration**: 
  - LAPIC timer support for scheduling
  - Safe reboot mechanisms

## New Features (April 2025)
- **VGA Graphics Support**: Added comprehensive VGA text mode support with:
  - 16 foreground and 16 background colors
  - Text-based UI elements (windows, boxes, borders)
  - Positioned text rendering and cursor control
  - Graphical demo with animated elements
- **Enhanced Memory Management**:
  - Memory corruption detection with header/footer validation
  - Protection against double-free bugs and buffer overruns
  - Detailed memory usage statistics and testing utilities
- **Improved Task Management**:
  - Named task support for better identification
  - Task state tracking and reporting
  - Task information command with detailed status view
- **Enhanced File System**:
  - Directory listing with file attributes
  - File content reading with error handling
  - File existence and size checking
- **Improved Shell**:
  - Color-coded prompt and output
  - New commands for system management and diagnostics
  - Enhanced error handling and user feedback

## Project Structure
- `bootloader/`: Contains the bootloader code
- `kernel/`: Contains the kernel source code and headers
  - `vga.c/h`: VGA driver implementation
  - `task.c/h`: Task management system
  - `keyboard.c/h`: Enhanced keyboard driver
  - `shell.c/h`: Interactive shell implementation
- `memory/`: Memory management subsystem
  - `paging.c/h`: Virtual memory implementation
  - `heap.c/h`: Advanced heap memory management
- `filesystem/`: File system implementation
  - `fat12.c/h`: FAT12 file system driver
- `test/`: Contains unit tests and testing framework

## Shell Commands
- `help`: Display available commands
- `clear`: Clear the screen
- `echo`: Display a message
- `ls`: List directory contents
- `cat`: Display file contents
- `meminfo`: Display basic memory information
- `memstat`: Show detailed memory statistics
- `memtest`: Run memory allocation tests
- `taskinfo`: Display task information with states
- `reboot`: Reboot the system
- `vgademo`: Launch the VGA demonstration

## VGA Features
The VGA subsystem provides:
- Text mode support (80x25 characters)
- 16 foreground and 16 background colors with blinking text support
- Hardware cursor manipulation with size and visibility control
- Special character handling (newlines, tabs, backspace)
- Advanced scrolling with configurable regions and smooth scrolling
- Full-screen and region-based screen clearing
- Triple buffer support for flicker-free animations
- Drawing primitives:
  - Boxes with single/double-line borders and custom styling
  - Windows with titles, shadows, and interaction elements
  - Horizontal and vertical lines with various styles (solid, dotted, dashed)
  - Positioned text rendering with character-by-character animation
  - Custom character set support with user-defined glyphs
  - Color fading and transition effects
  - Basic shape drawing (rectangles, circles)
- Text UI components:
  - Progress bars with customizable styling
  - Simple menus with selection highlighting
  - Dialog boxes with multiple options
  - Status bars with update capabilities
- Screen capture and restore functionality
- Virtual terminal support with multiple screens

## Memory Management
The enhanced heap management system provides:
- Dynamic memory allocation with standard C functions (malloc, free, realloc, calloc)
- Memory block validation with magic numbers
- Header and footer system for detecting buffer overruns
- Memory block splitting and merging for efficiency
- Detailed memory usage statistics
- Protection against double-free errors and memory corruption

## Task Management
The task management system features:
- Preemptive multitasking with round-robin scheduling
- Named tasks for better identification
- Task state tracking (UNUSED, READY, RUNNING)
- Task information retrieval API
- Task switching safety mechanisms

## Build Instructions
1. Install a cross-compiler for your target architecture.
2. Run `make` in the root directory to build the project.
3. Use QEMU to emulate the OS: `qemu-system-x86_64 -kernel kernel/kernel`.

## Debugging
- GDB can be attached to debug the kernel: `gdb -ex "target remote localhost:1234" -ex "symbol-file kernel/kernel"`
- Memory statistics can be viewed with the `memstat` command
- Task information can be viewed with the `taskinfo` command

## Contribution Guidelines
- Follow consistent naming conventions
- Write clear and concise comments
- Ensure all new features are tested
- Use memory-safe practices and validate inputs

## License
This project is licensed under the MIT License.

---

*Last Updated: April 28, 2025*
