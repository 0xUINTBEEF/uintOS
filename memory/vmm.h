#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

/**
 * Memory region types
 */
#define VM_TYPE_KERNEL    0x00000001  // Kernel memory
#define VM_TYPE_USER      0x00000002  // User memory
#define VM_TYPE_STACK     0x00000003  // Stack memory
#define VM_TYPE_HEAP      0x00000004  // Heap memory
#define VM_TYPE_MMIO      0x00000005  // Memory-mapped I/O
#define VM_TYPE_SHARED    0x00000006  // Shared memory
#define VM_TYPE_MODULE    0x00000007  // Loadable module
#define VM_TYPE_FRAMEBUF  0x00000008  // Framebuffer
#define VM_TYPE_DMA       0x00000009  // DMA buffer
#define VM_TYPE_RESERVED  0x0000000A  // Reserved/system

/**
 * Memory region permissions
 */
#define VM_PERM_NONE      0x00000000  // No access
#define VM_PERM_READ      0x00000001  // Read access
#define VM_PERM_WRITE     0x00000002  // Write access
#define VM_PERM_EXEC      0x00000004  // Execute access
#define VM_PERM_USER      0x00000008  // User-mode accessible

/**
 * Memory region flags
 */
#define VM_FLAG_NOCACHE   0x00000100  // Disable caching
#define VM_FLAG_SHARED    0x00000200  // Shared between processes
#define VM_FLAG_FIXED     0x00000400  // Fixed address
#define VM_FLAG_GUARD     0x00000800  // Guard page
#define VM_FLAG_STACK     0x00001000  // Stack grows downward
#define VM_FLAG_HEAP      0x00002000  // Heap grows upward

/**
 * Initialize the Virtual Memory Manager
 * 
 * @param mem_size_kb Size of physical memory in kilobytes
 * @return 0 on success, -1 on failure
 */
int vmm_init(uint32_t mem_size_kb);

/**
 * Handle a page fault
 * 
 * @param address Virtual address that caused the fault
 * @param error_code Error code from the CPU
 */
void vmm_handle_page_fault(uint32_t address, uint32_t error_code);

/**
 * Register a custom page fault handler
 * 
 * @param handler Pointer to handler function
 */
void vmm_register_page_fault_handler(void (*handler)(uint32_t, uint32_t));

/**
 * Allocate a region of virtual memory
 * 
 * @param size Size in bytes
 * @param flags Protection flags
 * @param type Type of memory
 * @param name Name of the region (for debugging)
 * @return Virtual address of the allocated region, or NULL on failure
 */
void* vmm_alloc(size_t size, uint32_t flags, uint32_t type, const char* name);

/**
 * Free a previously allocated memory region
 * 
 * @param addr Start address of the region
 * @param size Size in bytes
 */
void vmm_free(void* addr, size_t size);

/**
 * Map physical memory into virtual address space
 * 
 * @param phys Physical address
 * @param size Size in bytes
 * @param flags Protection flags
 * @param name Name of the region (for debugging)
 * @return Virtual address of the mapped region, or NULL on failure
 */
void* vmm_map_physical(uint32_t phys, size_t size, uint32_t flags, const char* name);

/**
 * Get statistics about memory usage
 * 
 * @param stats Pointer to statistics structure to fill
 */
void vmm_get_stats(void* stats);

/**
 * Dump memory regions for debugging
 */
void vmm_dump_regions(void);

/**
 * Create address space for a new process
 * 
 * @param process_id ID of the process
 * @return 0 on success, -1 on failure
 */
int vmm_create_process_space(int process_id);

/**
 * Destroy address space for a process
 * 
 * @param process_id ID of the process
 */
void vmm_destroy_process_space(int process_id);

/**
 * Switch to a process's address space
 * 
 * @param process_id ID of the process
 * @return 0 on success, -1 on failure
 */
int vmm_switch_to_process(int process_id);

/**
 * Map a shared memory region between processes
 * 
 * @param source_proc ID of the source process
 * @param source_addr Start address in source process
 * @param target_proc ID of the target process
 * @param target_addr Start address in target process
 * @param size Size of the region in bytes
 * @param flags Protection flags
 * @return 0 on success, -1 on failure
 */
int vmm_share_memory(int source_proc, void* source_addr,
                     int target_proc, void* target_addr,
                     size_t size, uint32_t flags);

/**
 * Set protection flags for a memory region
 * 
 * @param addr Start address of the region
 * @param size Size of the region in bytes
 * @param flags New protection flags
 * @return 0 on success, -1 on failure
 */
int vmm_protect(void* addr, size_t size, uint32_t flags);

/**
 * Query information about a memory region
 * 
 * @param addr Address within the region
 * @param start Pointer to variable to receive start address
 * @param size Pointer to variable to receive size
 * @param flags Pointer to variable to receive flags
 * @param type Pointer to variable to receive type
 * @return 0 on success, -1 if address not in any region
 */
int vmm_query_region(void* addr, void** start, size_t* size,
                    uint32_t* flags, uint32_t* type);

#endif /* VMM_H */