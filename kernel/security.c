#include "security.h"
#include "memory/heap.h"
#include "logging/log.h"
#include <string.h>

/**
 * Security System Implementation
 * 
 * This module implements core security functionality including:
 * - Access control and permissions
 * - Security identifiers (SIDs)
 * - Access tokens
 * - Security descriptors
 * - Discretionary access control lists (DACLs)
 * - User and group management
 * - Security capabilities
 */

// Current security context for system initialization
static access_token_t system_token;

// Well-known SIDs
static const security_id_t SID_SYSTEM   = {1, 0, 0, {0}};
static const security_id_t SID_ROOT     = {1, 0, 1, {0}};
static const security_id_t SID_GUEST    = {1, 0, 999, {0}};
static const security_id_t SID_EVERYONE = {1, 1, 0, {0}};
static const security_id_t SID_ADMIN    = {1, 1, 1, {0}};
static const security_id_t SID_USERS    = {1, 1, 2, {0}};

// Default security descriptor for new objects
static security_descriptor_t default_descriptor;

/**
 * Initialize the security system
 */
int security_init(void) {
    log_info("Initializing Security System");
    
    // Setup system token
    memset(&system_token, 0, sizeof(system_token));
    memcpy(&system_token.user, &SID_SYSTEM, sizeof(security_id_t));
    
    // Set system capabilities (all capabilities)
    system_token.capabilities = ~0UL;
    
    // Setup default security descriptor
    memset(&default_descriptor, 0, sizeof(default_descriptor));
    
    // Create a default DACL
    default_descriptor.dacl = (dacl_t*)heap_alloc(sizeof(dacl_t) + 2 * sizeof(ace_t));
    if (!default_descriptor.dacl) {
        log_error("Failed to allocate memory for default DACL");
        return -1;
    }
    
    default_descriptor.dacl->ace_count = 2;
    
    // Allow SYSTEM full access
    default_descriptor.dacl->aces[0].type = ACE_TYPE_ALLOW;
    default_descriptor.dacl->aces[0].flags = 0;
    default_descriptor.dacl->aces[0].access_mask = ACCESS_ALL;
    memcpy(&default_descriptor.dacl->aces[0].sid, &SID_SYSTEM, sizeof(security_id_t));
    
    // Allow ADMIN full access
    default_descriptor.dacl->aces[1].type = ACE_TYPE_ALLOW;
    default_descriptor.dacl->aces[1].flags = 0;
    default_descriptor.dacl->aces[1].access_mask = ACCESS_ALL;
    memcpy(&default_descriptor.dacl->aces[1].sid, &SID_ADMIN, sizeof(security_id_t));
    
    log_info("Security system initialized");
    return 0;
}

/**
 * Get current token
 */
int security_get_current_token(access_token_t* token) {
    if (!token) {
        return -1;
    }
    
    // TODO: Get token from current task
    // For now, just use the system token
    memcpy(token, &system_token, sizeof(access_token_t));
    
    return 0;
}

/**
 * Set current token
 */
int security_set_current_token(const access_token_t* token) {
    if (!token) {
        return -1;
    }
    
    // TODO: Set token for current task
    return 0;
}

/**
 * Create a new token
 */
int security_create_token(access_token_t* token,
                         const security_id_t* user,
                         const security_id_t* groups,
                         uint32_t group_count,
                         uint64_t capabilities) {
    if (!token || !user) {
        return -1;
    }
    
    // Initialize token
    memset(token, 0, sizeof(access_token_t));
    
    // Set user SID
    memcpy(&token->user, user, sizeof(security_id_t));
    
    // Set groups
    if (groups && group_count > 0) {
        token->groups = (security_id_t*)heap_alloc(sizeof(security_id_t) * group_count);
        if (!token->groups) {
            return -1;
        }
        
        token->group_count = group_count;
        memcpy(token->groups, groups, sizeof(security_id_t) * group_count);
    }
    
    // Set capabilities
    token->capabilities = capabilities;
    
    return 0;
}

/**
 * Free token resources
 */
void security_free_token(access_token_t* token) {
    if (!token) {
        return;
    }
    
    // Free groups
    if (token->groups) {
        heap_free(token->groups);
        token->groups = NULL;
        token->group_count = 0;
    }
}

/**
 * Check if a SID is in a token
 */
bool security_sid_in_token(const access_token_t* token, const security_id_t* sid) {
    if (!token || !sid) {
        return false;
    }
    
    // Check user SID
    if (security_sid_equal(&token->user, sid)) {
        return true;
    }
    
    // Check group SIDs
    for (uint32_t i = 0; i < token->group_count; i++) {
        if (security_sid_equal(&token->groups[i], sid)) {
            return true;
        }
    }
    
    return false;
}

/**
 * Compare two SIDs
 */
bool security_sid_equal(const security_id_t* sid1, const security_id_t* sid2) {
    if (!sid1 || !sid2) {
        return false;
    }
    
    // Compare authority and sub-authorities
    if (sid1->authority != sid2->authority) {
        return false;
    }
    
    if (sid1->sub_authority_count != sid2->sub_authority_count) {
        return false;
    }
    
    if (sid1->primary_id != sid2->primary_id) {
        return false;
    }
    
    for (uint8_t i = 0; i < sid1->sub_authority_count; i++) {
        if (sid1->sub_authorities[i] != sid2->sub_authorities[i]) {
            return false;
        }
    }
    
    return true;
}

/**
 * Convert SID to string representation (S-A-P-S1-S2...)
 */
int security_sid_to_string(const security_id_t* sid, char* buffer, size_t buffer_size) {
    if (!sid || !buffer || buffer_size == 0) {
        return -1;
    }
    
    // Format: S-A-P-S1-S2...
    int written = snprintf(buffer, buffer_size, "S-%u-%u", sid->authority, sid->primary_id);
    
    // Add sub-authorities
    for (uint8_t i = 0; i < sid->sub_authority_count && i < SID_MAX_SUB_AUTHORITIES; i++) {
        int append_len = snprintf(buffer + written, buffer_size - written, 
                                "-%u", sid->sub_authorities[i]);
        
        if (append_len < 0 || (size_t)append_len >= buffer_size - written) {
            // Buffer too small
            buffer[buffer_size - 1] = '\0';
            return -1;
        }
        
        written += append_len;
    }
    
    return written;
}

/**
 * Create a DACL
 */
dacl_t* create_dacl(uint32_t ace_count) {
    if (ace_count == 0) {
        return NULL;
    }
    
    // Allocate memory for DACL and ACEs
    dacl_t* dacl = (dacl_t*)heap_alloc(sizeof(dacl_t) + ace_count * sizeof(ace_t));
    if (!dacl) {
        return NULL;
    }
    
    // Initialize DACL
    dacl->ace_count = ace_count;
    
    return dacl;
}

/**
 * Free a DACL
 */
void free_dacl(dacl_t* dacl) {
    if (dacl) {
        heap_free(dacl);
    }
}

/**
 * Create a security descriptor
 */
int security_create_descriptor(security_descriptor_t* descriptor,
                              const security_id_t* owner,
                              const security_id_t* group,
                              const dacl_t* dacl) {
    if (!descriptor) {
        return -1;
    }
    
    // Initialize descriptor
    memset(descriptor, 0, sizeof(security_descriptor_t));
    
    // Copy owner SID if provided
    if (owner) {
        memcpy(&descriptor->owner, owner, sizeof(security_id_t));
        descriptor->flags |= SD_FLAG_OWNER_VALID;
    }
    
    // Copy group SID if provided
    if (group) {
        memcpy(&descriptor->group, group, sizeof(security_id_t));
        descriptor->flags |= SD_FLAG_GROUP_VALID;
    }
    
    // Copy DACL if provided
    if (dacl) {
        // Allocate memory for DACL
        descriptor->dacl = create_dacl(dacl->ace_count);
        if (!descriptor->dacl) {
            return -1;
        }
        
        // Copy ACEs
        memcpy(descriptor->dacl->aces, dacl->aces, dacl->ace_count * sizeof(ace_t));
        descriptor->flags |= SD_FLAG_DACL_PRESENT;
    }
    
    return 0;
}

/**
 * Get object security descriptor
 */
int security_get_object_descriptor(const char* object_name,
                                  security_descriptor_t* descriptor) {
    if (!object_name || !descriptor) {
        return -1;
    }
    
    // TODO: Lookup object in registry/database
    
    // For now, return the default descriptor
    security_create_descriptor(descriptor, 
                             &SID_SYSTEM, 
                             &SID_ADMIN, 
                             default_descriptor.dacl);
    
    return 0;
}

/**
 * Set object security descriptor
 */
int security_set_object_descriptor(const char* object_name,
                                  const security_descriptor_t* descriptor) {
    if (!object_name || !descriptor) {
        return -1;
    }
    
    // TODO: Store object descriptor in registry/database
    return 0;
}

/**
 * Check access against a security descriptor
 */
bool security_check_access(const access_token_t* token,
                          const security_descriptor_t* descriptor,
                          uint16_t desired_access) {
    if (!token || !descriptor) {
        return false;
    }
    
    // System token has full access
    if (security_sid_equal(&token->user, &SID_SYSTEM)) {
        return true;
    }
    
    // Object owner has full access
    if ((descriptor->flags & SD_FLAG_OWNER_VALID) &&
        security_sid_equal(&token->user, &descriptor->owner)) {
        return true;
    }
    
    // If no DACL is present, allow all access
    if (!(descriptor->flags & SD_FLAG_DACL_PRESENT) || !descriptor->dacl) {
        return true;
    }
    
    // Initialize granted and denied access masks
    uint16_t granted_access = 0;
    uint16_t denied_access = 0;
    
    // Check each ACE in the DACL
    for (uint32_t i = 0; i < descriptor->dacl->ace_count; i++) {
        const ace_t* ace = &descriptor->dacl->aces[i];
        
        // Check if ACE applies to this token
        if (!security_sid_in_token(token, &ace->sid)) {
            continue;
        }
        
        // Process ACE based on type
        switch (ace->type) {
            case ACE_TYPE_ALLOW:
                granted_access |= ace->access_mask;
                break;
                
            case ACE_TYPE_DENY:
                denied_access |= ace->access_mask;
                break;
                
            default:
                // Unknown ACE type, skip
                break;
        }
    }
    
    // If any desired access is explicitly denied, deny access
    if (denied_access & desired_access) {
        return false;
    }
    
    // Check if all desired access is granted
    return (granted_access & desired_access) == desired_access;
}

/**
 * Check if token has a capability
 */
bool security_check_capability(const access_token_t* token, uint64_t capability) {
    if (!token) {
        return false;
    }
    
    // Check capability bit
    return (token->capabilities & capability) != 0;
}

/**
 * Check if current process has a capability
 */
bool security_current_has_capability(uint64_t capability) {
    access_token_t token;
    if (security_get_current_token(&token) != 0) {
        return false;
    }
    
    bool result = security_check_capability(&token, capability);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    return result;
}

/**
 * Check if a task has permission to make a specific system call
 *
 * This function determines whether the given task is allowed to
 * execute the specified system call based on its security context,
 * capabilities, and system policies.
 *
 * @param task The task attempting to make the system call
 * @param syscall_num The system call number being requested
 * @return true if the task has permission, false otherwise
 */
bool security_check_syscall_permission(struct task* task, uint64_t syscall_num) {
    // If task is NULL, deny access
    if (!task) {
        log_warn("SECURITY", "NULL task attempted syscall %llu", syscall_num);
        return false;
    }
    
    // Root/system tasks can make any syscall
    if (task->euid == 0) {
        return true;
    }

    // Check specific restricted syscalls
    switch (syscall_num) {
        // Module management restricted to root
        case SYS_MODULE_LOAD:
        case SYS_MODULE_UNLOAD:
            return task->euid == 0;
            
        // Memory management may require CAP_SYS_RESOURCE
        case SYS_MMAP:
        case SYS_MUNMAP:
        case SYS_BRK:
            // Regular users can allocate memory up to their limits
            // For advanced operations, check for CAP_SYS_RESOURCE
            return true;
            
        // Process management
        case SYS_FORK:
        case SYS_EXECVE:
        case SYS_EXIT:
        case SYS_WAITPID:
        case SYS_GETPID:
        case SYS_GETPPID:
        case SYS_YIELD:
            // All processes allowed to use basic process management
            return true;
            
        // File operations
        case SYS_OPEN:
        case SYS_READ:
        case SYS_WRITE:
        case SYS_CLOSE:
            // Access control is checked at the VFS level
            return true;
            
        // Time-related operations
        case SYS_TIME:
            // Reading time is allowed for all processes
            return true;

        // Directory operations
        case SYS_CHDIR:
        case SYS_MKDIR:
        case SYS_RMDIR:
        case SYS_UNLINK:
            // Directory access control is checked at the VFS level
            return true;
            
        default:
            // For any syscalls we don't explicitly handle, deny access
            // to non-root users until proper permissions are defined
            if (syscall_num > 50) {
                log_warn("SECURITY", "Unrecognized syscall %llu by task %d", 
                         syscall_num, task->id);
                return false;
            }
            return true;
    }
}