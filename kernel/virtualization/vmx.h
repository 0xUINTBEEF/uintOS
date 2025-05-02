#ifndef VMX_H
#define VMX_H

#include <inttypes.h>

// VMX MSRs
#define IA32_VMX_BASIC                      0x480
#define IA32_VMX_PINBASED_CTLS              0x481
#define IA32_VMX_PROCBASED_CTLS             0x482
#define IA32_VMX_EXIT_CTLS                  0x483
#define IA32_VMX_ENTRY_CTLS                 0x484
#define IA32_VMX_MISC                       0x485
#define IA32_VMX_CR0_FIXED0                 0x486
#define IA32_VMX_CR0_FIXED1                 0x487
#define IA32_VMX_CR4_FIXED0                 0x488
#define IA32_VMX_CR4_FIXED1                 0x489
#define IA32_VMX_PROCBASED_CTLS2            0x48B

// VMX BASIC Exit Reasons
#define VMX_EXIT_EXCEPTION_NMI              0
#define VMX_EXIT_EXTERNAL_INTERRUPT         1
#define VMX_EXIT_TRIPLE_FAULT               2
#define VMX_EXIT_INIT                       3
#define VMX_EXIT_SIPI                       4
#define VMX_EXIT_IO_SMI                     5
#define VMX_EXIT_OTHER_SMI                  6
#define VMX_EXIT_PENDING_INTERRUPT          7
#define VMX_EXIT_NMI_WINDOW                 8
#define VMX_EXIT_TASK_SWITCH                9
#define VMX_EXIT_CPUID                      10
#define VMX_EXIT_GETSEC                     11
#define VMX_EXIT_HLT                        12
#define VMX_EXIT_INVD                       13
#define VMX_EXIT_INVLPG                     14
#define VMX_EXIT_RDPMC                      15
#define VMX_EXIT_RDTSC                      16
#define VMX_EXIT_RSM                        17
#define VMX_EXIT_VMCALL                     18
#define VMX_EXIT_VMCLEAR                    19
#define VMX_EXIT_VMLAUNCH                   20
#define VMX_EXIT_VMPTRLD                    21
#define VMX_EXIT_VMPTRST                    22
#define VMX_EXIT_VMREAD                     23
#define VMX_EXIT_VMRESUME                   24
#define VMX_EXIT_VMWRITE                    25
#define VMX_EXIT_VMXOFF                     26
#define VMX_EXIT_VMXON                      27
#define VMX_EXIT_CR_ACCESS                  28
#define VMX_EXIT_DR_ACCESS                  29
#define VMX_EXIT_IO_INSTRUCTION             30
#define VMX_EXIT_RDMSR                      31
#define VMX_EXIT_WRMSR                      32
#define VMX_EXIT_ENTRY_FAILURE_GUEST        33
#define VMX_EXIT_ENTRY_FAILURE_MSR          34
#define VMX_EXIT_MWAIT                      36
#define VMX_EXIT_MONITOR_TRAP_FLAG          37
#define VMX_EXIT_MONITOR                    39
#define VMX_EXIT_PAUSE                      40
#define VMX_EXIT_MACHINE_CHECK              41
#define VMX_EXIT_TPR_BELOW_THRESHOLD        43
#define VMX_EXIT_APIC_ACCESS                44
#define VMX_EXIT_VIRTUALIZED_EOI            45
#define VMX_EXIT_GDTR_IDTR_ACCESS           46
#define VMX_EXIT_LDTR_TR_ACCESS             47
#define VMX_EXIT_EPT_VIOLATION              48
#define VMX_EXIT_EPT_MISCONFIGURATION       49
#define VMX_EXIT_INVEPT                     50
#define VMX_EXIT_RDTSCP                     51
#define VMX_EXIT_VMX_PREEMPTION_TIMER       52
#define VMX_EXIT_INVVPID                    53
#define VMX_EXIT_WBINVD                     54
#define VMX_EXIT_XSETBV                     55
#define VMX_EXIT_APIC_WRITE                 56
#define VMX_EXIT_RDRAND                     57
#define VMX_EXIT_INVPCID                    58
#define VMX_EXIT_VMFUNC                     59
#define VMX_EXIT_ENCLS                      60
#define VMX_EXIT_RDSEED                     61
#define VMX_EXIT_PML_FULL                   62
#define VMX_EXIT_XSAVES                     63
#define VMX_EXIT_XRSTORS                    64
#define VMX_EXIT_SPP_RELATED_EVENT          66
#define VMX_EXIT_UMWAIT                     67
#define VMX_EXIT_TPAUSE                     68

// VMX CPU Features
#define CPUID_VMX_FEATURES                  0x1
#define VMX_FEATURE_BIT                     (1 << 5)  // Bit 5 in ECX register

// VMCS Field IDs
#define VMCS_GUEST_ES_SELECTOR              0x0800
#define VMCS_GUEST_CS_SELECTOR              0x0802
#define VMCS_GUEST_SS_SELECTOR              0x0804
#define VMCS_GUEST_DS_SELECTOR              0x0806
#define VMCS_GUEST_FS_SELECTOR              0x0808
#define VMCS_GUEST_GS_SELECTOR              0x080A
#define VMCS_GUEST_LDTR_SELECTOR            0x080C
#define VMCS_GUEST_TR_SELECTOR              0x080E
#define VMCS_GUEST_INTERRUPT_STATUS         0x0810
#define VMCS_GUEST_CR0                      0x6800
#define VMCS_GUEST_CR3                      0x6802
#define VMCS_GUEST_CR4                      0x6804
#define VMCS_GUEST_ES_BASE                  0x6806
#define VMCS_GUEST_CS_BASE                  0x6808
#define VMCS_GUEST_SS_BASE                  0x680A
#define VMCS_GUEST_DS_BASE                  0x680C
#define VMCS_GUEST_FS_BASE                  0x680E
#define VMCS_GUEST_GS_BASE                  0x6810
#define VMCS_GUEST_LDTR_BASE                0x6812
#define VMCS_GUEST_TR_BASE                  0x6814
#define VMCS_GUEST_GDTR_BASE                0x6816
#define VMCS_GUEST_IDTR_BASE                0x6818
#define VMCS_GUEST_DR7                      0x681A
#define VMCS_GUEST_RSP                      0x681C
#define VMCS_GUEST_RIP                      0x681E
#define VMCS_GUEST_RFLAGS                   0x6820
#define VMCS_GUEST_ES_LIMIT                 0x4800
#define VMCS_GUEST_CS_LIMIT                 0x4802
#define VMCS_GUEST_SS_LIMIT                 0x4804
#define VMCS_GUEST_DS_LIMIT                 0x4806
#define VMCS_GUEST_FS_LIMIT                 0x4808
#define VMCS_GUEST_GS_LIMIT                 0x480A
#define VMCS_GUEST_LDTR_LIMIT               0x480C
#define VMCS_GUEST_TR_LIMIT                 0x480E
#define VMCS_GUEST_GDTR_LIMIT               0x4810
#define VMCS_GUEST_IDTR_LIMIT               0x4812
#define VMCS_GUEST_ES_AR_BYTES              0x4814
#define VMCS_GUEST_CS_AR_BYTES              0x4816
#define VMCS_GUEST_SS_AR_BYTES              0x4818
#define VMCS_GUEST_DS_AR_BYTES              0x481A
#define VMCS_GUEST_FS_AR_BYTES              0x481C
#define VMCS_GUEST_GS_AR_BYTES              0x481E
#define VMCS_GUEST_LDTR_AR_BYTES            0x4820
#define VMCS_GUEST_TR_AR_BYTES              0x4822
#define VMCS_GUEST_ACTIVITY_STATE           0x4826
#define VMCS_GUEST_SMBASE                   0x4828
#define VMCS_GUEST_IA32_SYSENTER_CS         0x482A
#define VMCS_GUEST_IA32_SYSENTER_ESP        0x6824
#define VMCS_GUEST_IA32_SYSENTER_EIP        0x6826
#define VMCS_CONTROL_CR0_MASK               0x6000
#define VMCS_CONTROL_CR0_SHADOW             0x6004
#define VMCS_CONTROL_CR4_MASK               0x6002
#define VMCS_CONTROL_CR4_SHADOW             0x6006
#define VMCS_CONTROL_ENTRY_CONTROLS         0x4012
#define VMCS_CONTROL_ENTRY_MSR_LOAD_COUNT   0x4014
#define VMCS_CONTROL_ENTRY_MSR_LOAD_ADDR    0x2010
#define VMCS_CONTROL_EXIT_CONTROLS          0x400C
#define VMCS_CONTROL_EXIT_MSR_STORE_COUNT   0x400E
#define VMCS_CONTROL_EXIT_MSR_STORE_ADDR    0x2006

// VM Types
#define VM_TYPE_NORMAL                      0
#define VM_TYPE_PARAVIRT                    1
#define VM_TYPE_FULLVIRT                    2

// Maximum number of supported VMs
#define MAX_VMS                             16

// VM State 
#define VM_STATE_UNINITIALIZED              0
#define VM_STATE_READY                      1
#define VM_STATE_RUNNING                    2
#define VM_STATE_PAUSED                     3
#define VM_STATE_ERROR                      4
#define VM_STATE_TERMINATED                 5

// Forward declarations
struct vmcs;
struct vm_guest_state;

// VM instance structure
typedef struct vm_instance {
    uint32_t id;                        // VM identifier
    uint32_t type;                      // VM type (normal, paravirt, fullvirt)
    uint32_t state;                     // Current VM state
    uint32_t vcpu_count;                // Number of virtual CPUs
    uint32_t allocated_memory;          // Amount of memory allocated to VM (in KB)
    uint32_t cr3;                       // Page directory physical address
    char name[32];                      // VM name
    struct vmcs* vmcs;                  // Pointer to VMCS
    struct vm_guest_state* guest_state; // Guest state information
    void* shadow_page_tables;           // Shadow page tables for memory virtualization
    void* vm_exit_handler;              // Custom VM exit handler if any
    uint8_t* io_bitmap_a;               // I/O bitmap A
    uint8_t* io_bitmap_b;               // I/O bitmap B
    uint8_t* msr_bitmap;                // MSR bitmap
} vm_instance_t;

// Guest CPU state structure
typedef struct vm_guest_state {
    uint32_t cr0;
    uint32_t cr3;
    uint32_t cr4;
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eflags;
    uint16_t cs;
    uint16_t ds;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;
    uint16_t ss;
    uint32_t sysenter_cs;
    uint32_t sysenter_esp;
    uint32_t sysenter_eip;
} vm_guest_state_t;

// VMCS structure
typedef struct vmcs {
    uint32_t revision_id;               // VMCS revision identifier
    uint32_t abort_indicator;           // VMX-abort indicator
    uint8_t data[4088];                 // VMCS data area
} __attribute__((aligned(4096))) vmcs_t;

/**
 * Initialize the virtualization subsystem
 * 
 * @return 0 on success, error code on failure
 */
int vmx_init(void);

/**
 * Check if VMX (virtualization) is supported on this CPU
 * 
 * @return 1 if supported, 0 if not
 */
int vmx_is_supported(void);

/**
 * Enter VMX operation mode
 * 
 * @return 0 on success, error code on failure
 */
int vmx_enter_root_mode(void);

/**
 * Exit VMX operation mode
 * 
 * @return 0 on success, error code on failure
 */
int vmx_exit_root_mode(void);

/**
 * Create a new virtual machine
 * 
 * @param name Name for the VM
 * @param memory_size Amount of memory to allocate (in KB)
 * @param vcpu_count Number of virtual CPUs
 * @return VM ID on success, -1 on failure
 */
int vmx_create_vm(const char* name, uint32_t memory_size, uint32_t vcpu_count);

/**
 * Delete a virtual machine
 * 
 * @param vm_id ID of the VM to delete
 * @return 0 on success, error code on failure
 */
int vmx_delete_vm(uint32_t vm_id);

/**
 * Start a virtual machine
 * 
 * @param vm_id ID of the VM to start
 * @return 0 on success, error code on failure
 */
int vmx_start_vm(uint32_t vm_id);

/**
 * Stop a virtual machine
 * 
 * @param vm_id ID of the VM to stop
 * @return 0 on success, error code on failure
 */
int vmx_stop_vm(uint32_t vm_id);

/**
 * Pause a running virtual machine
 * 
 * @param vm_id ID of the VM to pause
 * @return 0 on success, error code on failure
 */
int vmx_pause_vm(uint32_t vm_id);

/**
 * Resume a paused virtual machine
 * 
 * @param vm_id ID of the VM to resume
 * @return 0 on success, error code on failure
 */
int vmx_resume_vm(uint32_t vm_id);

/**
 * Load a kernel image into a virtual machine
 * 
 * @param vm_id ID of the target VM
 * @param image_path Path to the kernel image
 * @return 0 on success, error code on failure
 */
int vmx_load_kernel(uint32_t vm_id, const char* image_path);

/**
 * Get information about a virtual machine
 * 
 * @param vm_id ID of the VM
 * @param vm Pointer to VM instance structure to fill with information
 * @return 0 on success, error code on failure
 */
int vmx_get_vm_info(uint32_t vm_id, vm_instance_t* vm);

/**
 * Get a list of all virtual machines
 * 
 * @param vms Array to store VM information
 * @param max_vms Maximum number of VMs to return
 * @return Number of VMs found, or -1 on error
 */
int vmx_list_vms(vm_instance_t* vms, uint32_t max_vms);

/**
 * Set up VMCS (Virtual Machine Control Structure) for a VM
 * 
 * @param vm_id ID of the VM
 * @return 0 on success, error code on failure
 */
int vmx_setup_vmcs(uint32_t vm_id);

/**
 * VM exit handler function type
 */
typedef int (*vmx_exit_handler_t)(uint32_t vm_id, uint32_t exit_reason);

/**
 * Register a VM exit handler for a specific VM
 * 
 * @param vm_id ID of the VM
 * @param handler Exit handler function
 * @return 0 on success, error code on failure
 */
int vmx_register_exit_handler(uint32_t vm_id, vmx_exit_handler_t handler);

/**
 * Main VM exit handler that dispatches to the appropriate specific handler
 * 
 * @param vm_id ID of the VM
 * @param exit_reason VM exit reason
 * @return 0 to continue VM, 1 to terminate VM
 */
int vmx_handle_vm_exit(uint32_t vm_id, uint32_t exit_reason);

/**
 * Read a VMCS field
 * 
 * @param field_id VMCS field identifier
 * @return The value of the VMCS field
 */
uint64_t vmx_vmread(uint32_t field_id);

/**
 * Write to a VMCS field
 * 
 * @param field_id VMCS field identifier
 * @param value Value to write
 * @return 0 on success, error code on failure
 */
int vmx_vmwrite(uint32_t field_id, uint64_t value);

#endif /* VMX_H */