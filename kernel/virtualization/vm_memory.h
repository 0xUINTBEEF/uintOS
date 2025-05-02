#ifndef VM_MEMORY_H
#define VM_MEMORY_H

#include <inttypes.h>

/**
 * Initialize the memory virtualization subsystem
 * 
 * @return 0 on success, error code on failure
 */
int vm_memory_init(void);

/**
 * Allocate physical memory for a virtual machine
 * 
 * @param vm_id ID of the virtual machine
 * @param size Size of memory to allocate in bytes
 * @return Virtual address of allocated memory, or NULL on failure
 */
void* vm_memory_allocate(uint32_t vm_id, uint32_t size);

/**
 * Free previously allocated VM memory
 * 
 * @param vm_id ID of the virtual machine
 * @param addr Virtual address of the memory to free
 * @param size Size of memory to free
 * @return 0 on success, error code on failure
 */
int vm_memory_free(uint32_t vm_id, void* addr, uint32_t size);

/**
 * Map a physical address into a VM's address space
 * 
 * @param vm_id ID of the virtual machine
 * @param guest_virtual Guest virtual address
 * @param host_physical Host physical address
 * @param size Size of the memory region to map
 * @param writable Whether the mapping should be writable
 * @param executable Whether the mapping should be executable
 * @return 0 on success, error code on failure
 */
int vm_memory_map(uint32_t vm_id, uint32_t guest_virtual, uint32_t host_physical, 
                  uint32_t size, int writable, int executable);

/**
 * Translate a guest virtual address to host physical address
 * 
 * @param vm_id ID of the virtual machine
 * @param guest_virtual Guest virtual address
 * @param host_physical Pointer to store the host physical address
 * @return 0 on success, error code on failure
 */
int vm_memory_translate(uint32_t vm_id, uint32_t guest_virtual, uint32_t* host_physical);

/**
 * Setup Extended Page Tables (EPT) for a virtual machine
 * 
 * @param vm_id ID of the virtual machine
 * @return 0 on success, error code on failure
 */
int vm_memory_setup_ept(uint32_t vm_id);

#endif /* VM_MEMORY_H */