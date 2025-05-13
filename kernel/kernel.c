#include "io.h"
#include "irq.h"
#include "gdt.h"
#include "asm.h"
#include "task.h"
#include "task1.h"
#include "task2.h"
#include "lapic.h"
#include "keyboard.h"
#include "shell.h"
#include "vga.h"
#include "panic.h"  // Include the panic header
#include "module.h" // Include the module system header
#include "syscall.h" // Include the syscall header
#include "preempt.h" // Include preemptive scheduling header
#include "exception_handlers.h" // Include exception handlers
#include "irq_asm.h" // Include assembly IRQ handling
#include "../filesystem/fat12.h"
#include "../memory/paging.h"
#include "../memory/heap.h"
#include "../hal/include/hal.h"
#include "../kernel/graphics/graphics.h"
#include "../kernel/logging/log.h"
#include "../kernel/virtualization/vmx.h"
#include "../kernel/virtualization/vm_memory.h"
#include "../drivers/pci/pci.h" // Include PCI driver framework
#include "../drivers/network/rtl8139.h" // Include RTL8139 network driver
#include "../drivers/audio/ac97.h" // Include AC97 audio driver

// Define system version constants
#define SYSTEM_VERSION "1.0.0"
#define SYSTEM_BUILD_DATE "May 11, 2025"

// Paging structures
#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIRECTORY_ENTRIES 1024

// Boot information structure (aligned with bootloader's structure)
typedef struct {
    uint32_t mem_map_addr;      // Address of memory map
    uint32_t mem_map_entries;   // Number of memory map entries
    uint32_t kernel_phys;       // Physical address where kernel is loaded
    uint32_t kernel_size;       // Size of kernel in bytes
    uint8_t  boot_device;       // Boot device identifier
    uint32_t vbe_mode_info;     // VBE mode info structure address
    uint32_t acpi_rsdp;         // ACPI RSDP table address
    uint32_t cmdline;           // Kernel command line address
    uint32_t modules_count;     // Count of loaded modules
    uint32_t modules_addr;      // Address of module info structures
    uint32_t hardware_info;     // Address of hardware information structure
} boot_info_t;

// Hardware information structure
typedef struct {
    char     cpu_vendor[12];    // CPU vendor string
    uint32_t cpu_features;      // CPU features flags
    uint8_t  has_apic;          // 1 if APIC present
    uint8_t  has_sse;           // 1 if SSE present
    uint8_t  has_sse2;          // 1 if SSE2 present
    uint8_t  has_sse3;          // 1 if SSE3 present
    uint8_t  has_fpu;           // 1 if FPU present
    uint8_t  has_vmx;           // 1 if VMX present (for virtualization)
    uint8_t  has_aes;           // 1 if AES instructions present
    uint8_t  has_xsave;         // 1 if XSAVE present
    uint8_t  reserved[4];       // Reserved for future use
} hardware_info_t;

// Memory map entry structure
typedef struct {
    uint64_t base_addr;         // Base address of memory region
    uint64_t length;            // Length of memory region in bytes
    uint32_t type;              // Type of memory region
    uint32_t acpi_extended;     // ACPI 3.0 extended attributes
} memory_map_entry_t;

// Global variables
unsigned int page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
unsigned int page_table[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
int hal_initialized = 0;
boot_info_t *boot_info = NULL;  // Pointer to boot information

// Forward declarations
void gdb_stub(void);
void vga_demo(void);
void initialize_system(void);
void display_welcome_message(void);
void parse_boot_info(boot_info_t *info);

struct tss initial_task_state = {
  .esp0 = 0x10000,
  .ss0_r = DATA_SELECTOR,
  .ss1_r = DATA_SELECTOR,
  .esp1 = 0x10000,
  .ss2_r = DATA_SELECTOR,
  .eip = 0x0,
  .esp2 = 0x10000,
  .esp = 0x10000,
  .eflags = 0x87,
  .es_r = VIDEO_SELECTOR,
  .cs_r = CODE_SELECTOR,
  .ds_r = DATA_SELECTOR,
  .ss_r = DATA_SELECTOR,
  .fs_r = DATA_SELECTOR,
  .gs_r = DATA_SELECTOR,
};

// Detect a QEMU environment to apply special configurations
int detect_qemu() {
    // Check for QEMU-specific CPUID signature
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" 
               : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) 
               : "a" (0x40000000));
    
    if (ebx == 0x4D455551 && ecx == 0x554D4551 && edx == 0x554D4551) { // "QEMU"
        return 1;
    }
    
    // Check for known QEMU I/O ports
    // Try writing to and reading from a QEMU debug port
    outb(0xe9, 'Q');  // QEMU debug port
    
    // Check for QEMU memory signature at known locations
    char *qemu_signature = (char*)0xFFFD0000;
    if (qemu_signature[0] == 'Q' && qemu_signature[1] == 'E' && 
        qemu_signature[2] == 'M' && qemu_signature[3] == 'U') {
        return 1;
    }
    
    return 0;  // Not QEMU
}

/**
 * Parse boot information passed by bootloader
 */
void parse_boot_info(boot_info_t *info) {
    if (!info) {
        log_warning("No boot info provided by bootloader");
        return;
    }
    
    log_info("Boot info: kernel at 0x%X, size %d bytes", 
             info->kernel_phys, info->kernel_size);
    
    // Log boot device information
    log_info("Booted from device 0x%02X", info->boot_device);
    
    // Process memory map if available
    if (info->mem_map_addr && info->mem_map_entries) {
        memory_map_entry_t *mmap = (memory_map_entry_t*)(info->mem_map_addr);
        log_info("Memory map: %d entries at 0x%X", info->mem_map_entries, info->mem_map_addr);
        
        uint64_t total_memory = 0;
        for (uint32_t i = 0; i < info->mem_map_entries; i++) {
            if (mmap[i].type == 1) { // Available memory
                log_debug("Memory region %d: 0x%llX - 0x%llX (%llu KB, type %d)", 
                         i, mmap[i].base_addr, 
                         mmap[i].base_addr + mmap[i].length - 1,
                         mmap[i].length / 1024, mmap[i].type);
                total_memory += mmap[i].length;
            }
        }
        
        log_info("Total usable memory: %llu KB (%llu MB)", 
                total_memory / 1024, total_memory / (1024 * 1024));
    }
    
    // Process hardware info if available
    if (info->hardware_info) {
        hardware_info_t *hw_info = (hardware_info_t*)(info->hardware_info);
        
        // Copy vendor string and ensure null termination
        char vendor[13] = {0};
        memcpy(vendor, hw_info->cpu_vendor, 12);
        vendor[12] = '\0';
        
        log_info("CPU: %s", vendor);
        log_info("CPU features: VMX=%d SSE=%d SSE2=%d SSE3=%d FPU=%d APIC=%d",
                hw_info->has_vmx, hw_info->has_sse, hw_info->has_sse2,
                hw_info->has_sse3, hw_info->has_fpu, hw_info->has_apic);
    }
    
    // Process ACPI info if available
    if (info->acpi_rsdp) {
        log_info("ACPI RSDP found at 0x%X", info->acpi_rsdp);
    }
    
    // Process VBE info if available
    if (info->vbe_mode_info) {
        log_info("VBE mode info available at 0x%X", info->vbe_mode_info);
    }
}

/**
 * Initialize the paging system
 */
void initialize_paging() {
    log_info("Initializing paging subsystem...");
    
    // Map the first 4MB of memory
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        page_table[i] = (i * PAGE_SIZE) | 3; // Present and writable
    }

    // Point the first entry of the page directory to the page table
    page_directory[0] = ((unsigned int)page_table) | 3; // Present and writable

    // Set all other entries in the page directory to not present
    for (int i = 1; i < PAGE_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    // Load the page directory into CR3
    asm volatile("mov %0, %%cr3" : : "r"(page_directory));

    // Enable paging by setting the PG bit in CR0
    unsigned int cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Set the PG bit
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    
    log_info("Paging initialized successfully");
}

/**
 * Provides a breakpoint for debugging with GDB
 */
void gdb_stub() {
    asm volatile("int3"); // Trigger a breakpoint interrupt for GDB
}

/**
 * Display VGA demo showing graphical capabilities
 */
void vga_demo() {
    // Save current color
    uint8_t old_color = vga_current_color;
    
    log_debug("Launching VGA demo...");
    
    // Clear screen with blue background
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_clear_screen();
    
    // Draw a title
    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE));
    vga_write_string_at("uintOS VGA Demo", 30, 1);
    
    // Draw some boxes with different colors
    vga_draw_box(5, 3, 25, 10, vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_draw_window(30, 3, 50, 10, "Info", vga_entry_color(VGA_COLOR_CYAN, VGA_COLOR_BLUE), 
                   vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_CYAN));
    vga_draw_window(55, 3, 75, 10, "Help", vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLUE), 
                   vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_GREEN));
    
    // Add some text inside boxes
    vga_write_string_at("File Explorer", 10, 5);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_write_string_at("Documents", 8, 7);
    vga_write_string_at("Pictures", 8, 8);
    vga_write_string_at("Settings", 8, 9);
    
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_write_string_at("System Info:", 33, 5);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_write_string_at("CPU: 1x86", 33, 6);
    vga_write_string_at("RAM: 16 MB", 33, 7);
    vga_write_string_at("OS: uintOS " SYSTEM_VERSION, 33, 8);
    vga_write_string_at("Date: " SYSTEM_BUILD_DATE, 33, 9);
    
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_write_string_at("Keyboard Shortcuts:", 58, 5);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_write_string_at("F1 - Help", 58, 6);
    vga_write_string_at("F2 - Menu", 58, 7);
    vga_write_string_at("F3 - Search", 58, 8);
    vga_write_string_at("ESC - Exit", 58, 9);
    
    // Draw a progress bar
    vga_draw_horizontal_line(20, 12, 40, vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE));
    vga_draw_horizontal_line(20, 12, 28, vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLUE));
    vga_write_string_at("System Loading... 70%", 28, 14);
    
    // Add a footer
    vga_set_color(vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY));
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga_buffer[24 * VGA_WIDTH + i] = vga_entry(' ', vga_current_color);
    }
    vga_write_string_at("Press any key to continue to shell...", 22, 24);
    
    // Wait for a keypress
    while (!is_key_available()) {
        // Simple animation for the progress bar
        static int progress = 0;
        static int direction = 1;
        
        // Erase old progress indicator
        vga_draw_horizontal_line(20, 12, 40, vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLUE));
        
        // Update progress
        progress += direction;
        if (progress >= 40) {
            direction = -1;
        } else if (progress <= 0) {
            direction = 1;
        }
        
        // Draw new progress
        vga_draw_horizontal_line(20, 12, progress, vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLUE));
        
        // Small delay
        for (volatile int i = 0; i < 500000; i++);
    }
    
    // Clear keypress
    keyboard_read_key();
    
    // Restore original color
    vga_set_color(old_color);
    vga_clear_screen();
    
    log_debug("VGA demo completed");
}

/**
 * Display welcome message with version information
 */
void display_welcome_message() {
    vga_set_color(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
    vga_write_string("uintOS (" SYSTEM_BUILD_DATE ") - Version " SYSTEM_VERSION "\n");
    vga_write_string("-------------------------------------------\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_write_string("Memory, filesystem, task and VGA subsystems initialized\n");
    vga_write_string("Preemptive multitasking enabled\n");
    vga_write_string("Type 'help' for a list of available commands\n\n");
    
    log_info("System initialization completed successfully");
    
    // Enable preemptive multitasking now that initialization is complete
    enable_preemption();
    log_info("Preemptive multitasking enabled");
}

/**
 * Initialize all system components
 */
void initialize_system() {
    // Initialize logging first for early diagnostics
    log_init(LOG_LEVEL_INFO, LOG_DEST_SCREEN | LOG_DEST_MEMORY, LOG_FORMAT_LEVEL | LOG_FORMAT_SOURCE | LOG_FORMAT_TIMESTAMP);
    log_info("KERNEL", "uintOS " SYSTEM_VERSION " starting up...");
    
    // Initialize memory management
    initialize_paging();
    heap_init();
    
    // Initialize Security Subsystem (after memory management, before peripherals)
    log_info("KERNEL", "Initializing security subsystem...");
    int security_status = security_init();
    if (security_status != 0) {
        log_error("KERNEL", "Security subsystem initialization failed (status: %d)", security_status);
    } else {
        log_info("KERNEL", "Security subsystem initialized successfully");
    }
    
    // Initialize Hardware Abstraction Layer
    log_info("KERNEL", "Initializing Hardware Abstraction Layer...");
    int hal_status = hal_initialize();
    if (hal_status != 0) {
        // HAL initialization failed, fall back to direct hardware access
        log_error("KERNEL", "HAL initialization failed (status: %d), falling back to direct hardware access", hal_status);
        vga_init();
        vga_set_color(vga_entry_color(VGA_COLOR_RED, VGA_COLOR_BLACK));
        vga_write_string("WARNING: HAL initialization failed, falling back to direct hardware access\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        // HAL initialized successfully
        hal_initialized = 1;
        log_info("KERNEL", "HAL initialized successfully");
        
        // Verify individual HAL components are working correctly
        log_debug("KERNEL", "Verifying HAL components");
        
        // Check HAL timer subsystem
        hal_timer_info_t timer_info;
        if (hal_timer_get_info(0, &timer_info) != HAL_TIMER_SUCCESS) {
            log_warning("KERNEL", "HAL timer subsystem not available or not properly initialized");
        } else {
            log_debug("KERNEL", "HAL timer subsystem initialized successfully");
        }
        
        // Check HAL memory subsystem
        hal_memory_map_t memory_map;
        if (hal_memory_get_map(&memory_map) != 0) {
            log_warning("KERNEL", "HAL memory subsystem not available or not properly initialized");
        } else {
            log_debug("KERNEL", "HAL memory subsystem initialized successfully");
        }
        
        vga_init();
        vga_set_color(vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
        vga_write_string("HAL initialized successfully\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    // Initialize hardware and kernel subsystems
    log_info("KERNEL", "Initializing GDT and TSS...");
    initialize_gdt(&initial_task_state);
      log_info("KERNEL", "Initializing interrupt system...");
    uintos_initialize_interrupts();
    
    // Initialize CPU exception handlers
    log_info("KERNEL", "Initializing exception handlers...");
    exception_init();
    irq_asm_install();
    log_info("KERNEL", "Exception handlers initialized");
    
    // Initialize system call interface
    log_info("KERNEL", "Initializing system call interface...");
    syscall_init();
    log_info("KERNEL", "System call interface initialized");
      // Initialize device management system
    log_info("KERNEL", "Initializing device manager...");
    int device_manager_result = device_manager_init();
    if (device_manager_result != 0) {
        log_error("KERNEL", "Device manager initialization failed: %d", device_manager_result);
    } else {
        log_info("KERNEL", "Device manager initialized successfully");
    }
    
    // Initialize kernel subsystems (including ASLR)
    log_info("KERNEL", "Initializing kernel subsystems...");
    kernel_init_subsystems();
    log_info("KERNEL", "Kernel subsystems initialized");

    // Initialize module management system
    log_info("KERNEL", "Initializing module system...");
    int module_system_result = module_system_init();
    if (module_system_result != 0) {
        log_error("KERNEL", "Module system initialization failed: %d", module_system_result);
    } else {
        log_info("KERNEL", "Module system initialized successfully");
    }
    
    // Configure and calibrate the system timer for preemptive scheduling
    log_info("KERNEL", "Configuring system timer for preemptive scheduling...");
    if (hal_initialized) {
        // Configure the timer using HAL
        hal_timer_info_t timer_info;
        hal_timer_get_info(0, &timer_info); // Get info for the first timer (LAPIC)
        
        hal_timer_calibrate(); // Calibrate the timer frequency
        
        // Configure timer for periodic interrupts at 100Hz (10ms intervals)
        hal_timer_config_t timer_config = {
            .mode = HAL_TIMER_PERIODIC,
            .frequency = 100, // 100Hz = 10ms period
            .vector = 32,     // IRQ0 = Vector 32 (timer)
            .callback = NULL  // We'll handle it via the IRQ handler
        };
        
        int timer_status = hal_timer_configure(0, &timer_config);
        if (timer_status != 0) {
            log_error("KERNEL", "Failed to configure timer for preemptive scheduling");
        } else {
            hal_timer_start(0);
            log_info("KERNEL", "Preemptive scheduling timer configured successfully (100Hz)");
        }
    } else {
        // Direct hardware access for timer configuration
        log_info("KERNEL", "Configuring PIT timer for preemptive scheduling...");
        
        // PIT runs at 1.193182 MHz, divisor = 11932 gives ~100Hz
        uint16_t divisor = 11932; 
        
        // Set PIT mode 3 (square wave)
        outb(0x43, 0x36);
        
        // Set divisor (low byte, then high byte)
        outb(0x40, divisor & 0xFF);
        outb(0x40, (divisor >> 8) & 0xFF);
        
        // Unmask IRQ0 (timer) in PIC
        outb(0x21, inb(0x21) & 0xFE);
        
        log_info("KERNEL", "PIT timer configured for preemptive scheduling (100Hz)");
    }
      // Initialize task management
    log_info("KERNEL", "Initializing multitasking...");
    initialize_multitasking();
      // Initialize preemptive multitasking
    log_info("KERNEL", "Initializing preemptive multitasking...");
    if (init_preemptive_multitasking(100) == 0) { // 100 Hz timer frequency
        log_info("KERNEL", "Preemptive multitasking initialized successfully");
        
        // Enable preemption by default
        enable_preemption();
        log_info("KERNEL", "Preemptive multitasking enabled by default");
    } else {
        log_error("KERNEL", "Failed to initialize preemptive multitasking");
    }
    
    // Initialize threading system
    log_info("KERNEL", "Initializing threading system...");
    thread_init();
    log_info("KERNEL", "Threading system initialized");
    
    // Initialize inter-process communication (IPC)
    log_info("KERNEL", "Initializing IPC subsystem...");
    ipc_init();
    log_info("KERNEL", "IPC subsystem initialized");
    
    // Initialize keyboard driver with improved handling
    log_info("KERNEL", "Initializing keyboard driver...");
    keyboard_init();
    keyboard_flush(); // Clear any pending keys

    // Initialize the Virtual Filesystem System
    log_info("KERNEL", "Initializing Virtual Filesystem System...");
    vfs_init();
    
    // Register all filesystem types with the VFS
    log_info("KERNEL", "Registering filesystem types...");
    register_fat12_with_vfs(); // Register FAT12
    register_ext2_with_vfs();  // Register ext2
    register_iso9660_with_vfs(); // Register ISO9660
    register_exfat_with_vfs(); // Register exFAT
    
    // Initialize individual filesystems
    log_info("KERNEL", "Initializing filesystem drivers...");
    fat12_init();
    ext2_init("ext2_disk");
    iso9660_init("cdrom");
    exfat_init("exfat_disk");
    
    // Mount filesystems
    log_info("KERNEL", "Mounting filesystems...");
    vfs_mount("fat12", "fat12_disk", "/fat", 0);
    vfs_mount("ext2", "ext2_disk", "/ext2", 0);
    vfs_mount("iso9660", "cdrom", "/cdrom", VFS_MOUNT_READONLY);
    vfs_mount("exfat", "exfat_disk", "/exfat", 0);
    
    log_info("KERNEL", "All filesystems registered and mounted");
    
    // Initialize PCI subsystem
    log_info("KERNEL", "Initializing PCI subsystem...");
    int pci_result = pci_init();
    if (pci_result != 0) {
        log_error("KERNEL", "PCI subsystem initialization failed: %d", pci_result);
    } else {
        log_info("KERNEL", "PCI subsystem initialized successfully");
        
        // Initialize RTL8139 network driver
        log_info("KERNEL", "Initializing RTL8139 network driver...");
        int rtl8139_result = rtl8139_init();
        if (rtl8139_result != 0) {
            log_error("KERNEL", "RTL8139 network driver initialization failed: %d", rtl8139_result);
        } else {
            log_info("KERNEL", "RTL8139 network driver initialized successfully");
        }
        
        // Initialize AC97 audio driver
        log_info("KERNEL", "Initializing AC97 audio driver...");
        int ac97_result = ac97_init();
        if (ac97_result != 0) {
            log_error("KERNEL", "AC97 audio driver initialization failed: %d", ac97_result);
        } else {
            log_info("KERNEL", "AC97 audio driver initialized successfully");
        }
    }
    
    // Create system tasks with the new named task API
    log_info("KERNEL", "Creating system tasks...");
    create_named_task(idle_task, "System Idle");
    create_named_task(counter_task, "Background Counter");
    
    // Initialize shell interface with the enhanced filesystem commands
    log_info("KERNEL", "Initializing shell...");
    shell_init();
    
    log_info("KERNEL", "System initialization complete");
}

/**
 * Main kernel entry point
 * 
 * This function supports both QEMU and real hardware booting.
 * When booted from real hardware, the bootloader passes a pointer
 * to the boot information structure in the EBX register.
 */
void kernel_main(uint32_t magic, boot_info_t *info) {
    // Store boot info for later use
    boot_info = info;
    
    // Initialize all system components
    initialize_system();
    
    // Parse boot information if available
    if (info) {
        parse_boot_info(info);
    }
    
    // Check if we're running under QEMU
    int is_qemu = detect_qemu();
    if (is_qemu) {
        log_info("Running under QEMU emulation");
    } else {
        log_info("Running on real hardware");
    }
    
    // Show VGA demo screen on first boot
    if (is_qemu) {
        vga_demo();
    }
    
    // Display welcome message
    display_welcome_message();
    
    // Start the shell - this will run in a loop
    log_info("Starting interactive shell");
    shell_run();
    
    // We should never reach here due to the shell's infinite loop
    log_error("Shell terminated unexpectedly!");
    
    // If the shell somehow exits, halt the system
    for (;;) {
        asm("hlt");
    }
}
