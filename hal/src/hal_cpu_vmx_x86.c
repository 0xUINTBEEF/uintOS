#include "../include/hal_cpu.h"
#include "../../kernel/asm.h"
#include <stdint.h>
#include <stdbool.h>

/* VMX MSR definitions */
#define IA32_VMX_BASIC_MSR               0x480
#define IA32_VMX_PINBASED_CTLS_MSR       0x481
#define IA32_VMX_PROCBASED_CTLS_MSR      0x482
#define IA32_VMX_EXIT_CTLS_MSR           0x483
#define IA32_VMX_ENTRY_CTLS_MSR          0x484
#define IA32_VMX_MISC_MSR                0x485
#define IA32_VMX_CR0_FIXED0_MSR          0x486
#define IA32_VMX_CR0_FIXED1_MSR          0x487
#define IA32_VMX_CR4_FIXED0_MSR          0x488
#define IA32_VMX_CR4_FIXED1_MSR          0x489
#define IA32_VMX_EPT_VPID_CAP_MSR        0x48C

/* CPUID VMX support flags */
#define CPUID_FEATURE_ECX_VMX            (1 << 5)

/* CR4 bits */
#define CR4_VMXE                         (1 << 13)

/* Feature Control MSR bits */
#define IA32_FEATURE_CONTROL_LOCK_BIT    0x1
#define IA32_FEATURE_CONTROL_VMX_BIT     0x4

/* INVEPT types */
#define INVEPT_SINGLE_CONTEXT            1
#define INVEPT_ALL_CONTEXTS              2

/* VMX error codes */
#define VMX_SUCCESS                      0
#define VMX_ERROR_VMXON                  1
#define VMX_ERROR_VMXOFF                 2
#define VMX_ERROR_VMPTRLD                3
#define VMX_ERROR_VMCLEAR                4
#define VMX_ERROR_VMLAUNCH               5
#define VMX_ERROR_VMRESUME               6
#define VMX_ERROR_VMREAD                 7
#define VMX_ERROR_VMWRITE                8

/**
 * Check if CPU supports VMX (Intel Virtualization Technology)
 */
bool hal_cpu_has_vmx_support(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // Check CPUID for VMX support
    hal_cpu_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    
    return (ecx & CPUID_FEATURE_ECX_VMX) != 0;
}

/**
 * Enable VMX operation by setting CR4.VMXE bit
 */
int hal_cpu_vmx_enable(void) {
    uint64_t cr4, feature_control, fixed0, fixed1;
    
    // Check if VMX is locked by the BIOS
    feature_control = hal_cpu_rdmsr(IA32_FEATURE_CONTROL_MSR);
    
    if (!(feature_control & IA32_FEATURE_CONTROL_LOCK_BIT)) {
        // If not locked, enable VMX and lock the MSR
        feature_control |= IA32_FEATURE_CONTROL_VMX_BIT | IA32_FEATURE_CONTROL_LOCK_BIT;
        hal_cpu_wrmsr(IA32_FEATURE_CONTROL_MSR, feature_control);
    } else if (!(feature_control & IA32_FEATURE_CONTROL_VMX_BIT)) {
        // If locked but VMX is disabled, we can't enable it
        return -1;
    }
    
    // Get the fixed bits for CR4
    fixed0 = hal_cpu_rdmsr(IA32_VMX_CR4_FIXED0_MSR);
    fixed1 = hal_cpu_rdmsr(IA32_VMX_CR4_FIXED1_MSR);
    
    // Read current CR4
    cr4 = hal_cpu_get_cr4();
    
    // Check if we can set the required bits
    if ((cr4 | fixed0) & ~fixed1) {
        return -2; // Can't satisfy fixed bit requirements
    }
    
    // Set the VMXE bit
    cr4 |= CR4_VMXE;
    hal_cpu_set_cr4(cr4);
    
    return VMX_SUCCESS;
}

/**
 * Disable VMX operation by clearing CR4.VMXE bit
 */
int hal_cpu_vmx_disable(void) {
    uint64_t cr4 = hal_cpu_get_cr4();
    
    // Clear the VMXE bit
    cr4 &= ~CR4_VMXE;
    hal_cpu_set_cr4(cr4);
    
    return VMX_SUCCESS;
}

/**
 * Read VMX capability MSR
 */
uint64_t hal_cpu_vmx_read_capability(uint32_t capability) {
    return hal_cpu_rdmsr(capability);
}

/**
 * Enter VMX operation (VMXON)
 */
int hal_cpu_vmx_vmxon(uint64_t vmxon_region_pa) {
    uint8_t error;
    
    // Execute VMXON instruction
    __asm__ volatile (
        "vmxon %1\n\t"
        "setna %0\n\t"
        : "=r" (error)
        : "m" (vmxon_region_pa)
        : "memory"
    );
    
    return error ? VMX_ERROR_VMXON : VMX_SUCCESS;
}

/**
 * Leave VMX operation (VMXOFF)
 */
int hal_cpu_vmx_vmxoff(void) {
    uint8_t error;
    
    // Execute VMXOFF instruction
    __asm__ volatile (
        "vmxoff\n\t"
        "setna %0\n\t"
        : "=r" (error)
        :
        : "memory"
    );
    
    return error ? VMX_ERROR_VMXOFF : VMX_SUCCESS;
}

/**
 * Set current VMCS pointer (VMPTRLD)
 */
int hal_cpu_vmx_vmptrld(uint64_t vmcs_pa) {
    uint8_t error;
    
    // Execute VMPTRLD instruction
    __asm__ volatile (
        "vmptrld %1\n\t"
        "setna %0\n\t"
        : "=r" (error)
        : "m" (vmcs_pa)
        : "memory"
    );
    
    return error ? VMX_ERROR_VMPTRLD : VMX_SUCCESS;
}

/**
 * Clear VMCS state (VMCLEAR)
 */
int hal_cpu_vmx_vmclear(uint64_t vmcs_pa) {
    uint8_t error;
    
    // Execute VMCLEAR instruction
    __asm__ volatile (
        "vmclear %1\n\t"
        "setna %0\n\t"
        : "=r" (error)
        : "m" (vmcs_pa)
        : "memory"
    );
    
    return error ? VMX_ERROR_VMCLEAR : VMX_SUCCESS;
}

/**
 * Launch virtual machine (VMLAUNCH)
 */
int hal_cpu_vmx_vmlaunch(void) {
    uint8_t error;
    
    // Execute VMLAUNCH instruction and capture error if any
    __asm__ volatile (
        "vmlaunch\n\t"
        "setna %0\n\t"
        : "=r" (error)
        :
        : "memory"
    );
    
    return error ? VMX_ERROR_VMLAUNCH : VMX_SUCCESS;
}

/**
 * Resume virtual machine (VMRESUME)
 */
int hal_cpu_vmx_vmresume(void) {
    uint8_t error;
    
    // Execute VMRESUME instruction and capture error if any
    __asm__ volatile (
        "vmresume\n\t"
        "setna %0\n\t"
        : "=r" (error)
        :
        : "memory"
    );
    
    return error ? VMX_ERROR_VMRESUME : VMX_SUCCESS;
}

/**
 * Read from current VMCS (VMREAD)
 */
uint64_t hal_cpu_vmx_vmread(uint32_t field) {
    uint64_t value;
    
    // Execute VMREAD instruction
    __asm__ volatile (
        "vmread %1, %0\n\t"
        : "=r" (value)
        : "r" ((uint64_t)field)
        : "memory"
    );
    
    return value;
}

/**
 * Write to current VMCS (VMWRITE)
 */
int hal_cpu_vmx_vmwrite(uint32_t field, uint64_t value) {
    uint8_t error;
    
    // Execute VMWRITE instruction
    __asm__ volatile (
        "vmwrite %1, %2\n\t"
        "setna %0\n\t"
        : "=r" (error)
        : "r" (value), "r" ((uint64_t)field)
        : "memory"
    );
    
    return error ? VMX_ERROR_VMWRITE : VMX_SUCCESS;
}

/**
 * Invalidate EPT TLB entries (INVEPT)
 */
void hal_cpu_invept(uint64_t eptp, uint32_t type) {
    struct {
        uint64_t eptp;
        uint64_t reserved;
    } descriptor = {eptp, 0};
    
    // Execute INVEPT instruction
    __asm__ volatile (
        "invept %0, %1\n\t"
        :
        : "m" (descriptor), "r" (type)
        : "memory"
    );
}

/**
 * Invalidate all EPT TLB entries
 */
void hal_cpu_invept_all_contexts(void) {
    hal_cpu_invept(0, INVEPT_ALL_CONTEXTS);
}