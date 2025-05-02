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
#include "../io.h"
#include "../asm.h"
#include <string.h>

// Define the VMX log tag
#define VMX_LOG_TAG "VMX"

// VMX MSR reading/writing assembly wrapper
static inline void read_msr(uint32_t msr, uint32_t *low, uint32_t *high) {
    asm volatile("rdmsr" : "=a"(*low), "=d"(*high) : "c"(msr));
}

static inline void write_msr(uint32_t msr, uint32_t low, uint32_t high) {
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

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
    uint32_t vmx_basic_msr_low, vmx_basic_msr_high;
    read_msr(IA32_VMX_BASIC, &vmx_basic_msr_low, &vmx_basic_msr_high);
    
    // The first 32 bits of the VMXON region must be set to the VMX revision identifier
    uint32_t revision_id = vmx_basic_msr_low & 0x7FFFFFFF;
    *(uint32_t *)vmx_region = revision_id;
    
    log_debug(VMX_LOG_TAG, "VMX revision ID: 0x%x", revision_id);
    
    // Get the fixed bits for CR0 and CR4
    uint32_t cr0_fixed0_low, cr0_fixed0_high;
    uint32_t cr0_fixed1_low, cr0_fixed1_high;
    uint32_t cr4_fixed0_low, cr4_fixed0_high;
    uint32_t cr4_fixed1_low, cr4_fixed1_high;
    
    read_msr(IA32_VMX_CR0_FIXED0, &cr0_fixed0_low, &cr0_fixed0_high);
    read_msr(IA32_VMX_CR0_FIXED1, &cr0_fixed1_low, &cr0_fixed1_high);
    read_msr(IA32_VMX_CR4_FIXED0, &cr4_fixed0_low, &cr4_fixed0_high);
    read_msr(IA32_VMX_CR4_FIXED1, &cr4_fixed1_low, &cr4_fixed1_high);
    
    // Set the required bits in CR0 and CR4
    uint32_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    
    cr0 = (cr0 & cr0_fixed1_low) | cr0_fixed0_low;
    cr4 = (cr4 & cr4_fixed1_low) | cr4_fixed0_low;
    
    // Set the VMX enable bit in CR4
    cr4 |= 0x2000; // CR4.VMXE bit
    
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    asm volatile("mov %0, %%cr4" : : "r"(cr4));
    
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
    // Check CPUID for VMX support
    uint32_t eax, ebx, ecx, edx;
    
    // Execute CPUID with EAX=1 to check processor features
    asm volatile("cpuid" 
               : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
               : "a"(1));
    
    // Check if bit 5 in ECX is set (indicates VMX support)
    if (!(ecx & VMX_FEATURE_BIT)) {
        log_warn(VMX_LOG_TAG, "CPU does not support VMX");
        return 0;
    }
    
    // Check if VMX is disabled in BIOS using MSR IA32_FEATURE_CONTROL
    uint32_t feature_ctrl_low, feature_ctrl_high;
    read_msr(0x3A, &feature_ctrl_low, &feature_ctrl_high);
    
    // Bit 0 = lock bit, Bit 2 = VMX outside SMX
    if (!(feature_ctrl_low & 0x5)) {
        log_warn(VMX_LOG_TAG, "VMX is disabled in BIOS/firmware");
        return 0;
    }
    
    log_info(VMX_LOG_TAG, "VMX is supported by CPU");
    return 1;
}

// Enter VMX root operation
int vmx_enter_root_mode() {
    uint32_t flags, vmx_on_result;
    
    // Execute VMXON instruction
    asm volatile(
        "pushf\n\t"                  // Save EFLAGS
        "push %%eax\n\t"             // Save EAX
        "vmxon %1\n\t"               // Try to enter VMX operation
        "pushf\n\t"                  // Save EFLAGS after VMXON
        "pop %0\n\t"                 // Store EFLAGS in vmx_on_result
        "pop %%eax\n\t"              // Restore EAX
        "popf"                       // Restore original EFLAGS
        : "=r"(vmx_on_result)
        : "m"(vmx_region)
        : "cc", "memory"
    );
    
    // Check if VMX operation was successful by checking the CF flag
    if (vmx_on_result & 0x1) {
        log_error(VMX_LOG_TAG, "VMXON instruction failed");
        return -1;
    }
    
    log_debug(VMX_LOG_TAG, "Entered VMX root operation successfully");
    return 0;
}

// Exit VMX operation
int vmx_exit_root_mode() {
    uint32_t vmx_off_result;
    
    // Execute VMXOFF instruction
    asm volatile(
        "pushf\n\t"                  // Save EFLAGS
        "vmxoff\n\t"                 // Exit VMX operation
        "pushf\n\t"                  // Save EFLAGS after VMXOFF
        "pop %0\n\t"                 // Store EFLAGS in vmx_off_result
        "popf"                       // Restore original EFLAGS
        : "=r"(vmx_off_result)
        :
        : "cc", "memory"
    );
    
    // Check if VMXOFF was successful
    if (vmx_off_result & 0x1) {
        log_error(VMX_LOG_TAG, "VMXOFF instruction failed");
        return -1;
    }
    
    // Clear VMX enable bit in CR4
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~0x2000; // Clear CR4.VMXE
    asm volatile("mov %0, %%cr4" : : "r"(cr4));
    
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
    
    // Allocate memory for VMCS
    vm->vmcs = (vmcs_t *)allocate_pages(1);
    if (!vm->vmcs) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for VMCS");
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Initialize VMCS
    uint32_t vmx_basic_msr_low, vmx_basic_msr_high;
    read_msr(IA32_VMX_BASIC, &vmx_basic_msr_low, &vmx_basic_msr_high);
    uint32_t revision_id = vmx_basic_msr_low & 0x7FFFFFFF;
    vm->vmcs->revision_id = revision_id;
    vm->vmcs->abort_indicator = 0;
    
    // Allocate guest state structure
    vm->guest_state = (vm_guest_state_t *)malloc(sizeof(vm_guest_state_t));
    if (!vm->guest_state) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for guest state");
        free_pages(vm->vmcs, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    memset(vm->guest_state, 0, sizeof(vm_guest_state_t));
    
    // Create a new address space for the VM
    vm->cr3 = paging_create_address_space(1);
    if (vm->cr3 == 0) {
        log_error(VMX_LOG_TAG, "Failed to create address space for VM");
        free(vm->guest_state);
        free_pages(vm->vmcs, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Allocate memory for I/O bitmaps (4KB each)
    vm->io_bitmap_a = allocate_pages(1);
    vm->io_bitmap_b = allocate_pages(1);
    if (!vm->io_bitmap_a || !vm->io_bitmap_b) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for I/O bitmaps");
        if (vm->io_bitmap_a) free_pages(vm->io_bitmap_a, 1);
        if (vm->io_bitmap_b) free_pages(vm->io_bitmap_b, 1);
        free(vm->guest_state);
        free_pages(vm->vmcs, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Set all bits to 1 to intercept all I/O operations (can be refined later)
    memset(vm->io_bitmap_a, 0xFF, 4096);
    memset(vm->io_bitmap_b, 0xFF, 4096);
    
    // Allocate memory for MSR bitmap (4KB)
    vm->msr_bitmap = allocate_pages(1);
    if (!vm->msr_bitmap) {
        log_error(VMX_LOG_TAG, "Failed to allocate memory for MSR bitmap");
        free_pages(vm->io_bitmap_a, 1);
        free_pages(vm->io_bitmap_b, 1);
        free(vm->guest_state);
        free_pages(vm->vmcs, 1);
        memset(vm, 0, sizeof(vm_instance_t));
        return VMX_ERROR_INSUFFICIENT_MEM;
    }
    
    // Set all bits to 1 to intercept all MSR accesses (can be refined later)
    memset(vm->msr_bitmap, 0xFF, 4096);
    
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
    if (vm->msr_bitmap) free_pages(vm->msr_bitmap, 1);
    if (vm->io_bitmap_a) free_pages(vm->io_bitmap_a, 1);
    if (vm->io_bitmap_b) free_pages(vm->io_bitmap_b, 1);
    if (vm->guest_state) free(vm->guest_state);
    if (vm->vmcs) free_pages(vm->vmcs, 1);
    
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
    uint32_t vmclear_result;
    asm volatile(
        "pushf\n\t"
        "vmclear %1\n\t"
        "pushf\n\t"
        "pop %0\n\t"
        "popf"
        : "=r"(vmclear_result)
        : "m"(vmcs)
        : "cc", "memory"
    );
    
    if (vmclear_result & 0x1) {
        log_error(VMX_LOG_TAG, "VMCLEAR failed");
        return VMX_ERROR_VMCS_SETUP;
    }
    
    // Execute VMPTRLD to make this the current VMCS
    uint32_t vmptrld_result;
    asm volatile(
        "pushf\n\t"
        "vmptrld %1\n\t"
        "pushf\n\t"
        "pop %0\n\t"
        "popf"
        : "=r"(vmptrld_result)
        : "m"(vmcs)
        : "cc", "memory"
    );
    
    if (vmptrld_result & 0x1) {
        log_error(VMX_LOG_TAG, "VMPTRLD failed");
        return VMX_ERROR_VMCS_SETUP;
    }
    
    // Set up control fields
    // TODO: Implement proper VMCS setup with vmx_vmwrite
    
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
    
    // TODO: Execute VMLAUNCH to start the VM (in the real implementation)
    // For now, simulate VM creation without actual execution
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
    
    // TODO: Properly terminate VM execution (in the real implementation)
    // For now, just update the state
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
    
    // TODO: Properly pause VM execution (in the real implementation)
    // For now, just update the state
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
    
    // TODO: Properly resume VM execution (in the real implementation)
    // For now, just update the state
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
    
    // TODO: Implement actual kernel image loading
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

// Read a field from the current VMCS
uint64_t vmx_vmread(uint32_t field_id) {
    uint32_t value = 0;
    
    // Execute VMREAD instruction
    asm volatile(
        "vmread %1, %0"
        : "=r"(value)
        : "r"((uint32_t)field_id)
        : "cc", "memory"
    );
    
    return value;
}

// Write a value to a field in the current VMCS
int vmx_vmwrite(uint32_t field_id, uint64_t value) {
    uint32_t result;
    
    // Execute VMWRITE instruction
    asm volatile(
        "pushf\n\t"
        "vmwrite %1, %2\n\t"
        "pushf\n\t"
        "pop %0\n\t"
        "popf"
        : "=r"(result)
        : "r"((uint32_t)value), "r"((uint32_t)field_id)
        : "cc", "memory"
    );
    
    // Check CF flag for error
    if (result & 0x1) {
        return -1;
    }
    
    return 0;
}

// Main VM exit handler
int vmx_handle_vm_exit(uint32_t vm_id, uint32_t exit_reason) {
    vm_instance_t* vm = find_vm_by_id(vm_id);
    if (!vm) {
        log_error(VMX_LOG_TAG, "VM with ID %d not found", vm_id);
        return -1;
    }
    
    log_debug(VMX_LOG_TAG, "VM exit occurred for VM %d, reason: %d", vm_id, exit_reason);
    
    // If a custom handler is registered, call it
    if (vm->vm_exit_handler) {
        vmx_exit_handler_t handler = (vmx_exit_handler_t)vm->vm_exit_handler;
        return handler(vm_id, exit_reason);
    }
    
    // Default handling based on exit reason
    switch (exit_reason) {
        case VMX_EXIT_CPUID:
            // Handle CPUID instruction
            return 0; // Continue VM execution
            
        case VMX_EXIT_HLT:
            // Handle HLT instruction
            return 1; // Terminate VM
            
        case VMX_EXIT_IO_INSTRUCTION:
            // Handle I/O instruction
            return 0; // Continue VM execution
            
        case VMX_EXIT_TRIPLE_FAULT:
            log_error(VMX_LOG_TAG, "VM %d experienced triple fault", vm_id);
            return 1; // Terminate VM
            
        default:
            log_warn(VMX_LOG_TAG, "Unhandled VM exit reason %d for VM %d", exit_reason, vm_id);
            return 0; // Continue VM execution
    }
}