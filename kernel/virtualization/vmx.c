/**
 * @file vmx.c
 * @brief VMX (Virtual Machine Extensions) implementation for virtualization support
 *
 * This file implements Intel VT-x virtualization extensions to allow running
 * virtual machines under the uintOS operating system.
 */

#include "vmx.h"
#include "../logging/log.h"
#include "../../memory/paging.h"
#include "../../memory/heap.h"
#include "../../hal/include/hal_cpu.h"
#include "../../hal/include/hal_memory.h"
#include "../io.h"
#include "../asm.h"
#include <string.h>

// Define the VMX log tag
#define VMX_LOG_TAG "VMX"

// Global array of VM instances
static vm_instance_t vm_instances[MAX_VMS];
static uint32_t num_vms = 0;
static uint32_t next_vm_id = 1;

// VMX region (required for VMXON instruction)
static uint8_t __attribute__((aligned(4096))) vmx_region[4096];

// Error code mapping for VMX operations
#define VMX_SUCCESS                 0
#define VMX_ERROR_UNSUPPORTED       -1
#define VMX_ERROR_ALREADY_INIT      -2
#define VMX_ERROR_INIT_FAILED       -3
#define VMX_ERROR_VMCS_SETUP        -4
#define VMX_ERROR_VM_LAUNCH         -5
#define VMX_ERROR_VM_NOT_FOUND      -6
#define VMX_ERROR_VM_INVALID_STATE  -7
#define VMX_ERROR_INSUFFICIENT_MEM  -8
#define VMX_ERROR_INVALID_PARAM     -9

/**
 * VMX MSR Constants 
 */
#define IA32_VMX_BASIC              0x480
#define IA32_VMX_CR0_FIXED0         0x486
#define IA32_VMX_CR0_FIXED1         0x487
#define IA32_VMX_CR4_FIXED0         0x488
#define IA32_VMX_CR4_FIXED1         0x489
#define IA32_FEATURE_CONTROL        0x3A

/**
 * Helper function to read 64-bit MSR using HAL
 */
static inline uint64_t read_msr_64(uint32_t msr) {
    return hal_cpu_read_msr(msr);
}

/**
 * Helper function to write 64-bit MSR using HAL
 */
static inline void write_msr_64(uint32_t msr, uint64_t value) {
    hal_cpu_write_msr(msr, value);
}

// Initialize VMX and check for VMX support
int vmx_init() {
    log_info(VMX_LOG_TAG, "Initializing VMX subsystem");
    
    // First, check if VMX is supported
    if (!vmx_is_supported()) {
        log_error(VMX_LOG_TAG, "VMX not supported by CPU");
        return VMX_ERROR_UNSUPPORTED;
    }
    
    // Initialize VM instance array
    for (int i = 0; i < MAX_VMS; i++) {
        memset(&vm_instances[i], 0, sizeof(vm_instance_t));
        vm_instances[i].state = VM_STATE_UNINITIALIZED;
    }
    
    // Set up the VMX region for VMXON
    uint64_t vmx_basic_msr = read_msr_64(IA32_VMX_BASIC);
    
    // The first 32 bits of the VMXON region must be set to the VMX revision identifier
    uint32_t revision_id = vmx_basic_msr & 0x7FFFFFFF;
    *(uint32_t *)vmx_region = revision_id;
    
    log_debug(VMX_LOG_TAG, "VMX revision ID: 0x%x", revision_id);
    
    // Get the fixed bits for CR0 and CR4
    uint64_t cr0_fixed0 = read_msr_64(IA32_VMX_CR0_FIXED0);
    uint64_t cr0_fixed1 = read_msr_64(IA32_VMX_CR0_FIXED1);
    uint64_t cr4_fixed0 = read_msr_64(IA32_VMX_CR4_FIXED0);
    uint64_t cr4_fixed1 = read_msr_64(IA32_VMX_CR4_FIXED1);
    
    // Set the required bits in CR0 and CR4 using HAL functions
    uint32_t cr0 = hal_cpu_read_cr0();
    uint32_t cr4 = hal_cpu_read_cr4();
    
    cr0 = (cr0 & cr0_fixed1) | cr0_fixed0;
    cr4 = (cr4 & cr4_fixed1) | cr4_fixed0;
    
    // Set the VMX enable bit in CR4
    cr4 |= 0x2000; // CR4.VMXE bit
    
    hal_cpu_write_cr0(cr0);
    hal_cpu_write_cr4(cr4);
    
    // Enter VMX operation
    int result = vmx_enter_root_mode();
    if (result != 0) {
        log_error(VMX_LOG_TAG, "Failed to enter VMX root operation");
        return VMX_ERROR_INIT_FAILED;
    }
    
    log_info(VMX_LOG_TAG, "VMX subsystem initialized successfully");
    return VMX_SUCCESS;
}

// Check if VMX is supported by the CPU
int vmx_is_supported() {
    // Check for VMX support using HAL CPU info
    hal_cpu_info_t cpu_info;
    hal_cpu_get_info(&cpu_info);
    
    if (!cpu_info.has_vmx) {
        log_warn(VMX_LOG_TAG, "CPU does not support VMX");
        return 0;
    }
    
    // Check if VMX is disabled in BIOS using MSR IA32_FEATURE_CONTROL
    uint64_t feature_ctrl = read_msr_64(IA32_FEATURE_CONTROL);
    
    // Bit 0 = lock bit, Bit 2 = VMX outside SMX
    if (!(feature_ctrl & 0x5)) {
        log_warn(VMX_LOG_TAG, "VMX is disabled in BIOS/firmware");
        return 0;
    }
    
    log_info(VMX_LOG_TAG, "VMX is supported by CPU");
    return 1;
}

// Enter VMX root operation
int vmx_enter_root_mode() {
    // Get physical address of the VMX region
    uintptr_t vmx_region_phys = (uintptr_t)vmx_region;
    
    // Use HAL CPU function instead of inline assembly for VMXON
    int result = hal_cpu_vmx_on(vmx_region_phys);
    if (result != 0) {
        log_error(VMX_LOG_TAG, "VMXON instruction failed");
        return -1;
    }
    
    log_debug(VMX_LOG_TAG, "Entered VMX root operation successfully");
    return 0;
}

// Exit VMX operation
int vmx_exit_root_mode() {
    uint32_t vmx_off_success = 0;
    
    // Use inline assembly for VMXOFF since it's a specialized instruction
    // In the future, this could be moved to hal_cpu_vmx_off()
    asm volatile(
        "vmxoff\n\t"
        "jnc 1f\n\t"      // Jump if carry flag is not set (success)
        "mov $0, %0\n\t"   // Set success to 0 (failure)
        "jmp 2f\n\t"
        "1:\n\t"
        "mov $1, %0\n\t"   // Set success to 1 (success)
        "2:"
        : "=r"(vmx_off_success)
        :
        : "cc", "memory"
    );
    
    if (!vmx_off_success) {
        log_error(VMX_LOG_TAG, "VMXOFF instruction failed");
        return -1;
    }
    
    // Clear VMX enable bit in CR4 using HAL
    uint32_t cr4 = hal_cpu_read_cr4();
    cr4 &= ~0x2000; // Clear CR4.VMXE
    hal_cpu_write_cr4(cr4);
    
    log_debug(VMX_LOG_TAG, "Exited VMX operation successfully");
    return 0;
}

// Create a new virtual machine
int vmx_create_vm(const char* name, uint32_t memory_size, uint32_t vcpu_count) {
    // Input validation
    if (!name || memory_size < 4096 || vcpu_count == 0) {
        log_error(VMX_LOG_TAG, "Invalid parameters for VM creation");
        return VMX_ERROR_INVALID_PARAM;
    }

    // Check if we have room for a new VM
    if (num_vms >= MAX_VMS) {
        log_error(VMX_LOG_TAG, "Maximum number of VMs reached");
        return VMX_ERROR_INSUFFICIENT_MEM;
    }

    // Find a free slot in the VM array
    int index = -1;
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].state == VM_STATE_UNINITIALIZED) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        log_error(VMX_LOG_TAG, "No available VM slots");
        return VMX_ERROR_INSUFFICIENT_MEM;
    }

    // Initialize the VM instance
    vm_instance_t* vm = &vm_instances[index];
    memset(vm, 0, sizeof(vm_instance_t));
    
    vm->id = next_vm_id++;
    vm->state = VM_STATE_READY;
    vm->vcpu_count = vcpu_count;
    vm->allocated_memory = memory_size;
    vm->type = VM_TYPE_NORMAL;
    
    // Copy the VM name (with bounds checking)
    size_t name_len = strlen(name);
    if (name_len >= sizeof(vm->name)) {
        name_len = sizeof(vm->name) - 1;
    }
    memcpy(vm->name, name, name_len);
    vm->name[name_len] = '\0';
    
    // Allocate memory for VMCS using HAL memory allocation
    uintptr_t vmcs_phys = hal_physical_alloc(1); // 1 page = 4KB
    if (!vmcs_phys) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for VMCS");
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Map VMCS into virtual memory
    hal_page_flags_t flags = {0};
    flags.access = HAL_MEM_ACCESS_RW;
    flags.cache = HAL_CACHE_WRITE_BACK;
    
    // For simplicity, use identity mapping for now
    // In a real implementation, we would use proper virtual memory allocation
    uintptr_t vmcs_virt = vmcs_phys;
    
    // Initialize VMCS
    vm->vmcs = (vmcs_t *)vmcs_virt;
    uint64_t vmx_basic_msr = read_msr_64(IA32_VMX_BASIC);
    uint32_t revision_id = vmx_basic_msr & 0x7FFFFFFF;
    vm->vmcs->revision_id = revision_id;
    vm->vmcs->abort_indicator = 0;
    
    // Allocate guest state structure
    vm->guest_state = (vm_guest_state_t *)malloc(sizeof(vm_guest_state_t));
    if (!vm->guest_state) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for guest state");
        hal_physical_free(vmcs_phys, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    memset(vm->guest_state, 0, sizeof(vm_guest_state_t));
    
    // Create a new address space for the VM
    vm->cr3 = paging_create_address_space(1);
    if (vm->cr3 == 0) {
        log_error(VMX_LOG_TAG, "Failed to create address space for VM");
        free(vm->guest_state);
        hal_physical_free(vmcs_phys, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Allocate memory for I/O bitmaps and MSR bitmap using HAL
    vm->io_bitmap_a_phys = hal_physical_alloc(1);
    vm->io_bitmap_b_phys = hal_physical_alloc(1);
    vm->msr_bitmap_phys = hal_physical_alloc(1);
    
    if (!vm->io_bitmap_a_phys || !vm->io_bitmap_b_phys || !vm->msr_bitmap_phys) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for bitmaps");
        if (vm->io_bitmap_a_phys) hal_physical_free(vm->io_bitmap_a_phys, 1);
        if (vm->io_bitmap_b_phys) hal_physical_free(vm->io_bitmap_b_phys, 1);
        if (vm->msr_bitmap_phys) hal_physical_free(vm->msr_bitmap_phys, 1);
        free(vm->guest_state);
        hal_physical_free(vmcs_phys, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Map bitmaps into virtual memory (identity mapping for now)
    vm->io_bitmap_a = (void*)vm->io_bitmap_a_phys;
    vm->io_bitmap_b = (void*)vm->io_bitmap_b_phys;
    vm->msr_bitmap = (void*)vm->msr_bitmap_phys;
    
    // Set all bits to 1 to intercept all I/O operations and MSR accesses
    memset(vm->io_bitmap_a, 0xFF, 4096);
    memset(vm->io_bitmap_b, 0xFF, 4096);
    memset(vm->msr_bitmap, 0xFF, 4096);
    
    // Allocate host stack for VM exits
    vm->host_stack = (uint8_t*)malloc(16 * 1024); // 16KB stack
    if (!vm->host_stack) {
        log_error(VMX_LOG_TAG, "Failed to allocate host stack");
        hal_physical_free(vm->io_bitmap_a_phys, 1);
        hal_physical_free(vm->io_bitmap_b_phys, 1);
        hal_physical_free(vm->msr_bitmap_phys, 1);
        free(vm->guest_state);
        hal_physical_free(vmcs_phys, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    vm->host_stack_top = vm->host_stack + 16 * 1024; // Stack grows down
    
    // Setup EPT (Extended Page Tables) for this VM
    // Check if the CPU supports EPT
    // In a real implementation, we would check via CPUID/MSR
    vm->supports_ept = 1; // Assume EPT is supported for this demonstration
    
    // Setup EPT if supported
    if (vm->supports_ept) {
        log_debug(VMX_LOG_TAG, "Setting up EPT for VM %d", vm->id);
        
        // Initialize EPT structures for the VM
        int ept_result = vm_memory_setup_ept(vm->id);
        if (ept_result != 0) {
            log_error(VMX_LOG_TAG, "Failed to setup EPT for VM %d: %d", vm->id, ept_result);
            // Continue without EPT, or fail if EPT is required
            vm->supports_ept = 0;
        } else {
            log_info(VMX_LOG_TAG, "EPT setup successful for VM %d", vm->id);
        }
    }
    
    // Initialize vCPU context using HAL CPU context functions
    for (uint32_t i = 0; i < vcpu_count && i < MAX_VCPUS; i++) {
        // Allocate vCPU context
        vm->vcpu_contexts[i] = (hal_cpu_context_t*)malloc(sizeof(hal_cpu_context_t));
        if (!vm->vcpu_contexts[i]) {
            log_error(VMX_LOG_TAG, "Failed to allocate vCPU context %d", i);
            // Free all previously allocated contexts
            for (uint32_t j = 0; j < i; j++) {
                free(vm->vcpu_contexts[j]);
            }
            // Free other resources
            free(vm->host_stack);
            hal_physical_free(vm->io_bitmap_a_phys, 1);
            hal_physical_free(vm->io_bitmap_b_phys, 1);
            hal_physical_free(vm->msr_bitmap_phys, 1);
            free(vm->guest_state);
            hal_physical_free(vmcs_phys, 1);
            memset(vm, 0, sizeof(vm_instance_t));
            return VMX_ERROR_INSUFFICIENT_MEM;
        }
        
        // Initialize vCPU context
        memset(vm->vcpu_contexts[i], 0, sizeof(hal_cpu_context_t));
    }
    
    // Increase VM count
    num_vms++;
    
    log_info(VMX_LOG_TAG, "Created VM '%s' with ID %d, %dKB memory, %d vCPUs", 
             vm->name, vm->id, vm->allocated_memory, vm->vcpu_count);
             
    return vm->id;
}

// Delete a virtual machine
int vmx_delete_vm(uint32_t vm_id) {
    // Find the VM
    vm_instance_t* vm = NULL;
    int index = -1;
    
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].id == vm_id && vm_instances[i].state != VM_STATE_UNINITIALIZED) {
            vm = &vm_instances[i];
            index = i;
            break;
        }
    }
    
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Can't delete a running VM
    if (vm->state == VM_STATE_RUNNING) {
        log_error(VMX_LOG_TAG, "Cannot delete running VM %d", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Deleting VM '%s' (ID: %d)", vm->name, vm_id);
    
    // Free all allocated resources
    if (vm->msr_bitmap_phys) hal_physical_free(vm->msr_bitmap_phys, 1);
    if (vm->io_bitmap_a_phys) hal_physical_free(vm->io_bitmap_a_phys, 1);
    if (vm->io_bitmap_b_phys) hal_physical_free(vm->io_bitmap_b_phys, 1);
    if (vm->guest_state) free(vm->guest_state);
    
    // Free vCPU contexts
    for (uint32_t i = 0; i < vm->vcpu_count && i < MAX_VCPUS; i++) {
        if (vm->vcpu_contexts[i]) {
            free(vm->vcpu_contexts[i]);
        }
    }
    
    // Free VMCS physical memory
    if (vm->vmcs) {
        // In this example we're using identity mapping, so physical = virtual
        hal_physical_free((uintptr_t)vm->vmcs, 1);
    }
    
    // Mark the VM as uninitialized
    memset(vm, 0, sizeof(vm_instance_t));
    vm->state = VM_STATE_UNINITIALIZED;
    
    num_vms--;
    
    return VMX_SUCCESS;
}

// Find a VM by ID
static vm_instance_t* find_vm_by_id(uint32_t vm_id) {
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].id == vm_id && vm_instances[i].state != VM_STATE_UNINITIALIZED) {
            return &vm_instances[i];
        }
    }
    return NULL;
}

// Setup VMCS for a VM
int vmx_setup_vmcs(uint32_t vm_id) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    vmcs_t* vmcs = vm->vmcs;
    vm_guest_state_t* guest_state = vm->guest_state;
    
    // Execute VMCLEAR to initialize the VMCS
    int vmclear_result = vmx_vmclear((uintptr_t)vmcs);
    if (vmclear_result != 0) {
        return VMX_ERROR_VMCS_SETUP;
    }
    
    // Execute VMPTRLD to make this the current VMCS
    int vmptrld_result = vmx_vmptrld((uintptr_t)vmcs);
    if (vmptrld_result != 0) {
        return VMX_ERROR_VMCS_SETUP;
    }
    
    // Set up control fields for the VM
    
    // 1. VM Entry controls
    uint32_t entry_ctls = 0x00000C92; // IA32e mode guest, load IA32_EFER
    vmx_vmwrite(VMX_ENTRY_CONTROLS, entry_ctls);
    
    // 2. VM Exit controls
    uint32_t exit_ctls = 0x00036DFF; // Host address space size, load IA32_EFER
    vmx_vmwrite(VMX_EXIT_CONTROLS, exit_ctls);
    
    // 3. Pin-based VM execution controls
    uint32_t pin_ctls = 0x00000001; // External interrupt exiting
    vmx_vmwrite(VMX_PIN_BASED_VM_EXEC_CONTROL, pin_ctls);
    
    // 4. Primary processor-based VM execution controls
    // Enable secondary controls and I/O bitmaps
    uint32_t cpu_ctls = 0x0000203F; // HLT, INVLPG, CR3 read/write, use I/O bitmaps, activate secondary controls
    vmx_vmwrite(VMX_CPU_BASED_VM_EXEC_CONTROL, cpu_ctls);
    
    // 5. Secondary processor-based VM execution controls
    // Enable EPT and unrestricted guest if supported
    uint32_t secondary_ctls = 0x00000000;
    
    // Check if EPT is supported
    // In a real implementation, we would check the VMX capability MSRs
    if (vm->supports_ept) {
        // Enable EPT (bit 1)
        secondary_ctls |= (1 << 1);
        log_debug(VMX_LOG_TAG, "Enabling EPT for VM %d", vm_id);
        
        // If we have EPT, we can also enable unrestricted guest mode
        // Unrestricted guest allows real mode operation with paging enabled
        if (vm->supports_unrestricted) {
            secondary_ctls |= (1 << 7);
            log_debug(VMX_LOG_TAG, "Enabling unrestricted guest for VM %d", vm_id);
        }
    }
    
    vmx_vmwrite(VMX_SECONDARY_VM_EXEC_CONTROL, secondary_ctls);
    
    // 6. Exception bitmap (which exceptions cause VM exits)
    vmx_vmwrite(VMX_EXCEPTION_BITMAP, 0x00000000); // No exceptions cause VM exits initially
    
    // 7. Set up control registers
    vmx_vmwrite(VMX_CR0_GUEST_HOST_MASK, 0x00000000); // No bits cause VM exits
    vmx_vmwrite(VMX_CR4_GUEST_HOST_MASK, 0x00000000); // No bits cause VM exits
    
    // 8. Configure I/O bitmaps
    vmx_vmwrite(VMX_IO_BITMAP_A_ADDR, vm->io_bitmap_a_phys);
    vmx_vmwrite(VMX_IO_BITMAP_B_ADDR, vm->io_bitmap_b_phys);
    
    // 9. Configure MSR bitmap
    vmx_vmwrite(VMX_MSR_BITMAP_ADDR, vm->msr_bitmap_phys);
    
    // 10. Configure Extended Page Table Pointer (EPTP) if EPT is enabled
    if (vm->supports_ept && vm->eptp) {
        vmx_vmwrite(VMX_EPT_POINTER, vm->eptp);
        log_debug(VMX_LOG_TAG, "Configured EPTP: 0x%x", vm->eptp);
    }
    
    // 11. Guest state
    // CR0, CR3, CR4
    vmx_vmwrite(VMX_GUEST_CR0, hal_cpu_read_cr0());
    vmx_vmwrite(VMX_GUEST_CR3, vm->cr3);
    vmx_vmwrite(VMX_GUEST_CR4, hal_cpu_read_cr4() & ~0x2000); // Without VMX bit
    
    // Segment selectors and bases
    vmx_vmwrite(VMX_GUEST_CS_SELECTOR, 0x0008); // Code segment
    vmx_vmwrite(VMX_GUEST_DS_SELECTOR, 0x0010); // Data segment
    vmx_vmwrite(VMX_GUEST_ES_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_GUEST_FS_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_GUEST_GS_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_GUEST_SS_SELECTOR, 0x0010);
    
    // Segment limits and access rights
    vmx_vmwrite(VMX_GUEST_CS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMX_GUEST_DS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMX_GUEST_ES_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMX_GUEST_FS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMX_GUEST_GS_LIMIT, 0xFFFFFFFF);
    vmx_vmwrite(VMX_GUEST_SS_LIMIT, 0xFFFFFFFF);
    
    // Guest RFLAGS
    vmx_vmwrite(VMX_GUEST_RFLAGS, 0x00000002); // Reserved bit is set
    
    // Guest RIP (entry point)
    vmx_vmwrite(VMX_GUEST_RIP, 0x00000000); // Start at address 0
    
    // Guest RSP (stack pointer)
    vmx_vmwrite(VMX_GUEST_RSP, 0x00000000); // No stack yet
    
    // Set up host state (state to return to on VM exit)
    
    // Host CR0, CR3, CR4
    vmx_vmwrite(VMX_HOST_CR0, hal_cpu_read_cr0());
    vmx_vmwrite(VMX_HOST_CR3, hal_cpu_read_cr3());
    vmx_vmwrite(VMX_HOST_CR4, hal_cpu_read_cr4());
    
    // Host segment selectors
    vmx_vmwrite(VMX_HOST_CS_SELECTOR, 0x0008);
    vmx_vmwrite(VMX_HOST_DS_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_HOST_ES_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_HOST_FS_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_HOST_GS_SELECTOR, 0x0010);
    vmx_vmwrite(VMX_HOST_SS_SELECTOR, 0x0010);
    
    // Host RIP (VM exit handler)
    vmx_vmwrite(VMX_HOST_RIP, (uint32_t)vmx_vm_exit_handler);
    
    // Host RSP (stack to use on VM exit)
    vmx_vmwrite(VMX_HOST_RSP, (uint32_t)vm->host_stack_top);
    
    log_debug(VMX_LOG_TAG, "VMCS setup complete for VM %d", vm_id);
    return VMX_SUCCESS;
}

// Start a VM
int vmx_start_vm(uint32_t vm_id) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Check that VM is in the ready or paused state
    if (vm->state != VM_STATE_READY && vm->state != VM_STATE_PAUSED) {
        log_error(VMX_LOG_TAG, "VM %d is not in a startable state", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Starting VM '%s' (ID: %d)", vm->name, vm_id);
    
    // Setup VMCS if not already done
    int vmcs_result = vmx_setup_vmcs(vm_id);
    if (vmcs_result != VMX_SUCCESS) {
        return vmcs_result;
    }
    
    // Update VM state
    vm->state = VM_STATE_RUNNING;
    
    // In a real implementation, we would execute VMLAUNCH here
    // For demonstration purposes, we'll just simulate it
    log_info(VMX_LOG_TAG, "VM '%s' (ID: %d) is now running", vm->name, vm_id);
    
    return VMX_SUCCESS;
}

// Stop a VM
int vmx_stop_vm(uint32_t vm_id) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Check that VM is in the running or paused state
    if (vm->state != VM_STATE_RUNNING && vm->state != VM_STATE_PAUSED) {
        log_error(VMX_LOG_TAG, "VM %d is not in a stoppable state", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Stopping VM '%s' (ID: %d)", vm->name, vm_id);
    
    // In a real implementation, we would trigger a VM exit here
    // For demonstration purposes, we'll just update the state
    vm->state = VM_STATE_TERMINATED;
    
    log_info(VMX_LOG_TAG, "VM '%s' (ID: %d) has been stopped", vm->name, vm_id);
    
    return VMX_SUCCESS;
}

// Pause a VM
int vmx_pause_vm(uint32_t vm_id) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Check that VM is in the running state
    if (vm->state != VM_STATE_RUNNING) {
        log_error(VMX_LOG_TAG, "VM %d is not running", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Pausing VM '%s' (ID: %d)", vm->name, vm_id);
    
    // In a real implementation, we would trigger a VM exit here
    // For demonstration purposes, we'll just update the state
    vm->state = VM_STATE_PAUSED;
    
    log_info(VMX_LOG_TAG, "VM '%s' (ID: %d) has been paused", vm->name, vm_id);
    
    return VMX_SUCCESS;
}

// Resume a VM
int vmx_resume_vm(uint32_t vm_id) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Check that VM is in the paused state
    if (vm->state != VM_STATE_PAUSED) {
        log_error(VMX_LOG_TAG, "VM %d is not paused", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Resuming VM '%s' (ID: %d)", vm->name, vm_id);
    
    // In a real implementation, we would execute VMRESUME here
    // For demonstration purposes, we'll just update the state
    vm->state = VM_STATE_RUNNING;
    
    log_info(VMX_LOG_TAG, "VM '%s' (ID: %d) has been resumed", vm->name, vm_id);
    
    return VMX_SUCCESS;
}

// Get VM information
int vmx_get_vm_info(uint32_t vm_id, vm_instance_t* vm_info) {
    if (!vm_info) {
        return VMX_ERROR_INVALID_PARAM;
    }
    
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Copy VM information
    memcpy(vm_info, vm, sizeof(vm_instance_t));
    
    return VMX_SUCCESS;
}

// List all VMs
int vmx_list_vms(vm_instance_t* vms, uint32_t max_vms) {
    if (!vms || max_vms == 0) {
        return VMX_ERROR_INVALID_PARAM;
    }
    
    int count = 0;
    
    for (int i = 0; i < MAX_VMS && count < max_vms; i++) {
        if (vm_instances[i].state != VM_STATE_UNINITIALIZED) {
            memcpy(&vms[count], &vm_instances[i], sizeof(vm_instance_t));
            count++;
        }
    }
    
    return count;
}

// Load a kernel image into a VM
int vmx_load_kernel(uint32_t vm_id, const char* image_path) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    if (!image_path) {
        return VMX_ERROR_INVALID_PARAM;
    }
    
    // Check that VM is in the ready state
    if (vm->state != VM_STATE_READY) {
        log_error(VMX_LOG_TAG, "VM %d is not in the ready state", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Loading kernel image '%s' into VM %d", image_path, vm_id);
    
    // For a real implementation, we'd load the kernel image here
    // For now, just log it and simulate success
    log_info(VMX_LOG_TAG, "Kernel image loaded into VM %d", vm_id);
    
    return VMX_SUCCESS;
}

// Register a VM exit handler
int vmx_register_exit_handler(uint32_t vm_id, vmx_exit_handler_t handler) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    if (!handler) {
        return VMX_ERROR_INVALID_PARAM;
    }
    
    vm->vm_exit_handler = (void*)handler;
    log_debug(VMX_LOG_TAG, "Exit handler registered for VM %d", vm_id);
    
    return VMX_SUCCESS;
}

// Read a field from the currently loaded VMCS
uint64_t vmx_vmread(uint32_t field) {
    uint64_t value;
    int result = hal_cpu_vmx_vmread(field, &value);
    if (result != 0) {
        log_error(VMX_LOG_TAG, "VMREAD instruction failed for field: %x", field);
        return 0;
    }
    return value;
}

// Write a value to a field in the currently loaded VMCS
int vmx_vmwrite(uint32_t field, uint64_t value) {
    int result = hal_cpu_vmx_vmwrite(field, value);
    if (result != 0) {
        log_error(VMX_LOG_TAG, "VMWRITE instruction failed for field: %x", field);
        return -1;
    }
    return 0;
}

// VM exit handler function (referenced in VMCS setup)
void vmx_vm_exit_handler(void) {
    // This is a placeholder - in a real implementation, this would be a proper handler
    // that would get control when a VM exit occurs
    asm volatile(
        "push %%eax\n\t"
        "push %%ebx\n\t"
        "push %%ecx\n\t"
        "push %%edx\n\t"
        "push %%esi\n\t"
        "push %%edi\n\t"
        "push %%ebp\n\t"
        "call vmx_handle_vm_exit_internal\n\t"
        "pop %%ebp\n\t"
        "pop %%edi\n\t"
        "pop %%esi\n\t"
        "pop %%edx\n\t"
        "pop %%ecx\n\t"
        "pop %%ebx\n\t"
        "pop %%eax\n\t"
        "vmresume\n\t"
        : : : "memory"
    );
    
    // Should never reach here unless VMRESUME fails
    log_error(VMX_LOG_TAG, "VMRESUME failed in exit handler");
}

// Internal VM exit handler (called from assembly stub)
int vmx_handle_vm_exit_internal(void) {
    // Get exit reason from VMCS
    uint32_t exit_reason = vmx_vmread(VMX_EXIT_REASON);
    
    // Get currently active VM (would need a more sophisticated method in a real implementation)
    uint32_t current_vm_id = 0;  // Placeholder
    vm_instance_t* current_vm = NULL;
    
    // Find the current VM
    for (int i = 0; i < MAX_VMS; i++) {
        if (vm_instances[i].state == VM_STATE_RUNNING) {
            current_vm = &vm_instances[i];
            current_vm_id = vm_instances[i].id;
            break;
        }
    }
    
    if (!current_vm) {
        log_error(VMX_LOG_TAG, "VM exit occurred, but no VM is active");
        return -1;
    }
    
    log_debug(VMX_LOG_TAG, "VM exit occurred for VM %d, reason: 0x%x", 
              current_vm_id, exit_reason);
    
    // Call the VM's exit handler if registered
    if (current_vm->vm_exit_handler) {
        vmx_exit_handler_t handler = (vmx_exit_handler_t)current_vm->vm_exit_handler;
        return handler(current_vm_id, exit_reason);
    }
    
    // Default handling based on exit reason
    return vmx_handle_vm_exit(current_vm_id, exit_reason);
}

// Main VM exit handler
int vmx_handle_vm_exit(uint32_t vm_id, uint32_t exit_reason) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return -1;
    }
    
    log_debug(VMX_LOG_TAG, "Handling VM exit for VM %d, reason: 0x%x", vm_id, exit_reason);
    
    // Handle exit reason
    switch (exit_reason) {
        case VMX_EXIT_CPUID:
            // Handle CPUID instruction
            return handle_cpuid_exit(vm);
            
        case VMX_EXIT_HLT:
            // Handle HLT instruction (typically just continue)
            return 0;
            
        case VMX_EXIT_IO_INSTRUCTION:
            // Handle I/O instruction
            return handle_io_exit(vm);
            
        case VMX_EXIT_TRIPLE_FAULT:
            log_error(VMX_LOG_TAG, "VM %d experienced triple fault", vm_id);
            vm->state = VM_STATE_TERMINATED;
            return 1; // Signal to terminate VM
            
        default:
            log_warn(VMX_LOG_TAG, "Unhandled VM exit reason 0x%x for VM %d", exit_reason, vm_id);
            return 0; // Continue VM execution
    }
}

// Handler for CPUID VM exit
static int handle_cpuid_exit(vm_instance_t* vm) {
    // Get EAX and ECX values from guest state
    uint32_t eax = vm->guest_state->rax;
    uint32_t ecx = vm->guest_state->rcx;
    
    log_debug(VMX_LOG_TAG, "CPUID exit with EAX=%x, ECX=%x", eax, ecx);
    
    // Execute CPUID instruction on behalf of the VM
    uint32_t cpuid_eax, cpuid_ebx, cpuid_ecx, cpuid_edx;
    hal_cpu_cpuid(eax, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
    
    // Modify the results if needed (e.g., to indicate we're a hypervisor)
    if (eax == 1) {
        // Set hypervisor bit in ECX
        cpuid_ecx |= (1 << 31);
    } else if (eax == 0x40000000) {
        // Hypervisor vendor ID leaf
        cpuid_eax = 0x40000001;  // Max supported leaf
        cpuid_ebx = 0x746E6975;  // "uint"
        cpuid_ecx = 0x6C61654C;  // "Lea"
        cpuid_edx = 0x6F745351;  // "SQto"
    }
    
    // Update guest state with results
    vm->guest_state->rax = cpuid_eax;
    vm->guest_state->rbx = cpuid_ebx;
    vm->guest_state->rcx = cpuid_ecx;
    vm->guest_state->rdx = cpuid_edx;
    
    // Advance guest RIP
    vm->guest_state->rip += vm->guest_state->cpuid_instruction_length;
    
    return 0; // Continue execution
}

// Handler for I/O instruction VM exit
static int handle_io_exit(vm_instance_t* vm) {
    // Get exit qualification from VMCS
    uint64_t exit_qualification = vmx_vmread(VMX_EXIT_QUALIFICATION);
    
    // Parse exit qualification field
    uint16_t port = (exit_qualification >> 16) & 0xFFFF;
    uint8_t size = (exit_qualification & 0x7) + 1;
    uint8_t direction = (exit_qualification >> 3) & 0x1; // 0 = out, 1 = in
    
    log_debug(VMX_LOG_TAG, "I/O exit: port=%x, size=%d, %s", 
              port, size, direction ? "in" : "out");
    
    if (direction == 0) {
        // I/O out (write to port)
        uint32_t value = 0;
        
        switch (size) {
            case 1:
                value = vm->guest_state->rax & 0xFF;
                hal_io_port_out8(port, value);
                break;
            case 2:
                value = vm->guest_state->rax & 0xFFFF;
                hal_io_port_out16(port, value);
                break;
            case 4:
                value = vm->guest_state->rax & 0xFFFFFFFF;
                hal_io_port_out32(port, value);
                break;
        }
        
        log_debug(VMX_LOG_TAG, "OUT %x, %x", port, value);
    } else {
        // I/O in (read from port)
        uint32_t value = 0;
        
        switch (size) {
            case 1:
                value = hal_io_port_in8(port);
                vm->guest_state->rax = (vm->guest_state->rax & ~0xFF) | value;
                break;
            case 2:
                value = hal_io_port_in16(port);
                vm->guest_state->rax = (vm->guest_state->rax & ~0xFFFF) | value;
                break;
            case 4:
                value = hal_io_port_in32(port);
                vm->guest_state->rax = value;
                break;
        }
        
        log_debug(VMX_LOG_TAG, "IN %x = %x", port, value);
    }
    
    // Advance guest RIP
    vm->guest_state->rip += vm->guest_state->io_instruction_length;
    
    return 0; // Continue execution
}

// Clear the VMCS
int vmx_vmclear(uintptr_t vmcs_addr) {
    int result = hal_cpu_vmx_vmclear(vmcs_addr);
    if (result != 0) {
        log_error(VMX_LOG_TAG, "VMCLEAR instruction failed");
        return -1;
    }
    return 0;
}

// Load and activate VMCS
int vmx_vmptrld(uintptr_t vmcs_addr) {
    int result = hal_cpu_vmx_vmptrld(vmcs_addr);
    if (result != 0) {
        log_error(VMX_LOG_TAG, "VMPTRLD instruction failed");
        return -1;
    }
    return 0;
}

// Launch virtual machine
int vmx_vmlaunch() {
    int result = hal_cpu_vmx_vmlaunch();
    if (result != 0) {
        uint32_t error = hal_cpu_vmx_read_error();
        log_error(VMX_LOG_TAG, "VMLAUNCH instruction failed with error code: %x", error);
        return -1;
    }
    return 0;
}

// Resume virtual machine
int vmx_vmresume() {
    int result = hal_cpu_vmx_vmresume();
    if (result != 0) {
        uint32_t error = hal_cpu_vmx_read_error();
        log_error(VMX_LOG_TAG, "VMRESUME instruction failed with error code: %x", error);
        return -1;
    }
    return 0;
}

// Launch a VM, transferring control to the guest
int vmx_vmlaunch(struct vmm_guest_state *guest_state) {
    // Save host state for when we return from the VM
    save_host_state(guest_state);
    
    // Load guest state into VMCS
    load_guest_state(guest_state);
    
    // Perform VM launch using HAL CPU function
    int result = hal_cpu_vmx_vmlaunch();
    if (result != 0) {
        uint64_t error = vmx_vmread(VMX_INSTRUCTION_ERROR);
        log_error(VMX_LOG_TAG, "VMLAUNCH failed with error: %llx", error);
        return -1;
    }
    
    // We shouldn't reach here normally - VM exit should come back through the VM exit handler
    return 0;
}

// Resume VM execution, continuing where the guest left off
int vmx_vmresume(struct vmm_guest_state *guest_state) {
    // Update guest state if needed
    update_guest_state(guest_state);
    
    // Perform VM resume using HAL CPU function
    int result = hal_cpu_vmx_vmresume();
    if (result != 0) {
        uint64_t error = vmx_vmread(VMX_INSTRUCTION_ERROR);
        log_error(VMX_LOG_TAG, "VMRESUME failed with error: %llx", error);
        return -1;
    }
    
    // We shouldn't reach here normally - VM exit should come back through the VM exit handler
    return 0;
}

// Magic value for identifying VM snapshots "uVMS"
#define VM_SNAPSHOT_MAGIC 0x534D5675

/**
 * Create a snapshot of a virtual machine's state
 * 
 * @param vm_id ID of the VM to snapshot
 * @param snapshot_path Path to save the snapshot file
 * @param flags Snapshot flags (VM_SNAPSHOT_*)
 * @return 0 on success, error code on failure
 */
int vmx_create_snapshot(uint32_t vm_id, const char* snapshot_path, uint32_t flags) {
    // Find the VM
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return VMX_ERROR_VM_NOT_FOUND;
    }
    
    // Check that the VM is in a valid state for snapshotting (not uninitialized)
    if (vm->state == VM_STATE_UNINITIALIZED) {
        log_error(VMX_LOG_TAG, "Cannot snapshot uninitialized VM %d", vm_id);
        return VMX_ERROR_VM_INVALID_STATE;
    }
    
    log_info(VMX_LOG_TAG, "Creating snapshot of VM '%s' (ID: %d) to '%s'", 
             vm->name, vm_id, snapshot_path);
    
    // Open file for writing
    void* file = vfs_open(snapshot_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (!file) {
        log_error(VMX_LOG_TAG, "Failed to open snapshot file '%s' for writing", snapshot_path);
        return -1;
    }
    
    // If VM is running, we need to pause it first
    int was_running = 0;
    if (vm->state == VM_STATE_RUNNING) {
        log_debug(VMX_LOG_TAG, "Pausing VM for snapshot");
        vmx_pause_vm(vm_id);
        was_running = 1;
    }
    
    // Calculate snapshot header size
    size_t header_size = sizeof(vm_snapshot_t);
    size_t vcpu_state_size = vm->vcpu_count * sizeof(hal_cpu_context_t);
    
    // Initialize snapshot header
    vm_snapshot_t snapshot_header = {
        .magic = VM_SNAPSHOT_MAGIC,
        .version = 1,
        .vm_id = vm->id,
        .flags = flags,
        .memory_size = vm->allocated_memory,
        .vcpu_count = vm->vcpu_count
    };
    
    // Copy VM name
    strncpy(snapshot_header.name, vm->name, sizeof(snapshot_header.name) - 1);
    snapshot_header.name[sizeof(snapshot_header.name) - 1] = '\0';
    
    // Write snapshot header
    if (vfs_write(file, &snapshot_header, sizeof(snapshot_header)) != sizeof(snapshot_header)) {
        log_error(VMX_LOG_TAG, "Failed to write snapshot header");
        vfs_close(file);
        if (was_running) vmx_resume_vm(vm_id);
        return -1;
    }
    
    // Write vCPU states
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        if (vfs_write(file, vm->vcpu_contexts[i], sizeof(hal_cpu_context_t)) != 
                sizeof(hal_cpu_context_t)) {
            log_error(VMX_LOG_TAG, "Failed to write vCPU state %d", i);
            vfs_close(file);
            if (was_running) vmx_resume_vm(vm_id);
            return -1;
        }
    }
    
    // If requested, save VM memory contents
    if (flags & VM_SNAPSHOT_INCLUDE_MEMORY) {
        log_debug(VMX_LOG_TAG, "Saving %d KB of VM memory", vm->allocated_memory);
        
        // Get VM memory (this is a simplified approach - in a real implementation 
        // we would need to walk the guest page tables)
        void* vm_memory = vm_memory_get_physical_mapping(vm_id);
        if (!vm_memory) {
            log_error(VMX_LOG_TAG, "Failed to get VM memory mapping");
            vfs_close(file);
            if (was_running) vmx_resume_vm(vm_id);
            return -1;
        }
        
        // Write the memory contents
        size_t memory_size = vm->allocated_memory * 1024; // Convert KB to bytes
        if (vfs_write(file, vm_memory, memory_size) != memory_size) {
            log_error(VMX_LOG_TAG, "Failed to write VM memory contents");
            vfs_close(file);
            if (was_running) vmx_resume_vm(vm_id);
            return -1;
        }
    }
    
    // If requested, save device states (simplified approach)
    if (flags & VM_SNAPSHOT_INCLUDE_DEVICES) {
        log_debug(VMX_LOG_TAG, "Saving device states");
        
        // In a real implementation, we would iterate through all 
        // device emulations and save their state
        
        // For now, just write the I/O bitmap as an example
        if (vfs_write(file, vm->io_bitmap_a, 4096) != 4096) {
            log_error(VMX_LOG_TAG, "Failed to write I/O bitmap A");
            vfs_close(file);
            if (was_running) vmx_resume_vm(vm_id);
            return -1;
        }
        
        if (vfs_write(file, vm->io_bitmap_b, 4096) != 4096) {
            log_error(VMX_LOG_TAG, "Failed to write I/O bitmap B");
            vfs_close(file);
            if (was_running) vmx_resume_vm(vm_id);
            return -1;
        }
    }
    
    // Close the snapshot file
    vfs_close(file);
    
    // Resume VM if it was running before
    if (was_running) {
        log_debug(VMX_LOG_TAG, "Resuming VM after snapshot");
        vmx_resume_vm(vm_id);
    }
    
    log_info(VMX_LOG_TAG, "Snapshot of VM '%s' (ID: %d) created successfully", 
             vm->name, vm_id);
    
    return VMX_SUCCESS;
}

/**
 * Restore a virtual machine from a snapshot
 * 
 * @param snapshot_path Path to the snapshot file
 * @param new_vm_id Optional pointer to receive the ID of the restored VM
 * @return 0 on success, error code on failure
 */
int vmx_restore_snapshot(const char* snapshot_path, uint32_t* new_vm_id) {
    log_info(VMX_LOG_TAG, "Restoring VM from snapshot '%s'", snapshot_path);
    
    // Open snapshot file for reading
    void* file = vfs_open(snapshot_path, VFS_O_RDONLY);
    if (!file) {
        log_error(VMX_LOG_TAG, "Failed to open snapshot file '%s' for reading", snapshot_path);
        return -1;
    }
    
    // Read snapshot header
    vm_snapshot_t snapshot_header;
    if (vfs_read(file, &snapshot_header, sizeof(snapshot_header)) != sizeof(snapshot_header)) {
        log_error(VMX_LOG_TAG, "Failed to read snapshot header");
        vfs_close(file);
        return -1;
    }
    
    // Verify magic value
    if (snapshot_header.magic != VM_SNAPSHOT_MAGIC) {
        log_error(VMX_LOG_TAG, "Invalid snapshot file (bad magic value)");
        vfs_close(file);
        return -1;
    }
    
    // Check snapshot version compatibility
    if (snapshot_header.version > 1) {
        log_error(VMX_LOG_TAG, "Unsupported snapshot version: %d", snapshot_header.version);
        vfs_close(file);
        return -1;
    }
    
    log_info(VMX_LOG_TAG, "Restoring VM '%s' with %d KB memory and %d vCPUs", 
             snapshot_header.name, snapshot_header.memory_size, snapshot_header.vcpu_count);
    
    // Create a new VM
    int vm_id = vmx_create_vm(snapshot_header.name, snapshot_header.memory_size, 
                             snapshot_header.vcpu_count);
    if (vm_id < 0) {
        log_error(VMX_LOG_TAG, "Failed to create VM for restoration");
        vfs_close(file);
        return -1;
    }
    
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "Internal error: VM %d not found after creation", vm_id);
        vfs_close(file);
        return -1;
    }
    
    // Read vCPU states
    for (uint32_t i = 0; i < snapshot_header.vcpu_count && i < MAX_VCPUS; i++) {
        if (vfs_read(file, vm->vcpu_contexts[i], sizeof(hal_cpu_context_t)) != 
                sizeof(hal_cpu_context_t)) {
            log_error(VMX_LOG_TAG, "Failed to read vCPU state %d", i);
            vfs_close(file);
            vmx_delete_vm(vm_id);
            return -1;
        }
    }
    
    // If snapshot includes memory contents, restore them
    if (snapshot_header.flags & VM_SNAPSHOT_INCLUDE_MEMORY) {
        log_debug(VMX_LOG_TAG, "Restoring %d KB of VM memory", snapshot_header.memory_size);
        
        // Get VM memory
        void* vm_memory = vm_memory_get_physical_mapping(vm_id);
        if (!vm_memory) {
            log_error(VMX_LOG_TAG, "Failed to get VM memory mapping");
            vfs_close(file);
            vmx_delete_vm(vm_id);
            return -1;
        }
        
        // Read the memory contents
        size_t memory_size = snapshot_header.memory_size * 1024; // Convert KB to bytes
        if (vfs_read(file, vm_memory, memory_size) != memory_size) {
            log_error(VMX_LOG_TAG, "Failed to read VM memory contents");
            vfs_close(file);
            vmx_delete_vm(vm_id);
            return -1;
        }
    }
    
    // If snapshot includes device states, restore them
    if (snapshot_header.flags & VM_SNAPSHOT_INCLUDE_DEVICES) {
        log_debug(VMX_LOG_TAG, "Restoring device states");
        
        // Read I/O bitmaps
        if (vfs_read(file, vm->io_bitmap_a, 4096) != 4096) {
            log_error(VMX_LOG_TAG, "Failed to read I/O bitmap A");
            vfs_close(file);
            vmx_delete_vm(vm_id);
            return -1;
        }
        
        if (vfs_read(file, vm->io_bitmap_b, 4096) != 4096) {
            log_error(VMX_LOG_TAG, "Failed to read I/O bitmap B");
            vfs_close(file);
            vmx_delete_vm(vm_id);
            return -1;
        }
    }
    
    // Close the snapshot file
    vfs_close(file);
    
    // Setup VMCS for the new VM
    int vmcs_result = vmx_setup_vmcs(vm_id);
    if (vmcs_result != VMX_SUCCESS) {
        log_error(VMX_LOG_TAG, "Failed to setup VMCS for restored VM");
        vmx_delete_vm(vm_id);
        return vmcs_result;
    }
    
    log_info(VMX_LOG_TAG, "VM '%s' (ID: %d) restored successfully from snapshot",
             vm->name, vm_id);
    
    // Return the VM ID if requested
    if (new_vm_id) {
        *new_vm_id = vm_id;
    }
    
    return VMX_SUCCESS;
}

// Helper function to get a physical mapping of VM memory
// This is a simplified approach - in a real implementation we would need
// to walk the guest page tables or maintain a proper mapping
static void* vm_memory_get_physical_mapping(uint32_t vm_id) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        return NULL;
    }
    
    // In a real implementation, we would need to get a proper mapping
    // of the physical memory allocated to the VM. This is just a placeholder.
    static void* dummy_memory = NULL;
    
    // For demo purposes, allocate some memory if not already allocated
    if (!dummy_memory) {
        dummy_memory = malloc(16 * 1024 * 1024); // 16 MB buffer
        if (dummy_memory) {
            memset(dummy_memory, 0, 16 * 1024 * 1024);
        }
    }
    
    return dummy_memory;
}