# uintOS

## Overview
uintOS is a simple operating system designed for educational purposes. It demonstrates basic kernel development, task management, and low-level hardware interaction.

## Features
- Basic kernel with task management.
- Interrupt handling and I/O operations.
- Assembly and C-based implementation.
- **Memory Management**: Added basic paging support.
- **Multitasking**: Implemented task creation, switching, and a round-robin scheduler.
- **User Interaction**: Added a basic shell with commands like `help` and `tasks`.
- **Debugging Support**: Integrated a GDB stub for debugging.

## Project Structure
- `bootloader/`: Contains the bootloader code.
- `kernel/`: Contains the kernel source code and headers.
- `test/`: Contains unit tests and testing framework.

## Build Instructions
1. Install a cross-compiler for your target architecture.
2. Run `make` in the root directory to build the project.
3. Use QEMU to emulate the OS: `qemu-system-x86_64 -kernel kernel/kernel`.

## Contribution Guidelines
- Follow consistent naming conventions.
- Write clear and concise comments.
- Ensure all new features are tested.

## License
This project is licensed under the MIT License.
