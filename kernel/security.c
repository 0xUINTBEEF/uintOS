#include "security.h"
#include "../memory/heap.h"
#include "logging/log.h"
#include <string.h>

// Current security token for the running task
static security_token_t* current_token = NULL;

// Initialize the security system
int security_init(void) {
    // Create the initial kernel token
    security_sid_t kernel_sid = {0, 0}; // System/kernel SID
    
    current_token = security_create_token(0, NULL, 0, PRIV_LEVEL_KERNEL);
    if (!current_token) {
        log_error("SECURITY", "Failed to initialize security system");
        return -1;
    }
    
    log_info("SECURITY", "Security system initialized");
    return 0;
}

// Create a new security token
security_token_t* security_create_token(uint32_t user_id, security_sid_t *groups, uint32_t group_count, uint32_t privilege_level) {
    security_token_t* token = (security_token_t*)malloc(sizeof(security_token_t));
    if (!token) {
        log_error("SECURITY", "Failed to allocate memory for security token");
        return NULL;
    }
    
    // Initialize token
    token->user.authority = 1; // User authority
    token->user.id = user_id;
    token->group_count = group_count;
    token->privilege_level = privilege_level;
    
    // Assign privileges based on privilege level
    switch (privilege_level) {
        case PRIV_LEVEL_KERNEL:
            token->privileges = PERM_ALL;
            break;
        case PRIV_LEVEL_DRIVER:
            token->privileges = PERM_READ | PERM_WRITE | PERM_EXECUTE | PERM_IO |
                             PERM_MAP | PERM_QUERY | PERM_CONTROL;
            break;
        case PRIV_LEVEL_SYSTEM:
            token->privileges = PERM_READ | PERM_WRITE | PERM_EXECUTE | 
                             PERM_QUERY | PERM_CREATE;
            break;
        case PRIV_LEVEL_USER:
            token->privileges = PERM_READ | PERM_EXECUTE | PERM_QUERY;
            break;
        default:
            token->privileges = PERM_READ | PERM_EXECUTE | PERM_QUERY;
            break;
    }
    
    // Copy groups if provided
    if (group_count > 0 && groups) {
        token->groups = (security_sid_t*)malloc(sizeof(security_sid_t) * group_count);
        if (!token->groups) {
            log_error("SECURITY", "Failed to allocate memory for security token groups");
            free(token);
            return NULL;
        }
        
        memcpy(token->groups, groups, sizeof(security_sid_t) * group_count);
    } else {
        token->groups = NULL;
        token->group_count = 0;
    }
    
    return token;
}

// Free a security token
void security_free_token(security_token_t *token) {
    if (!token) {
        return;
    }
    
    if (token->groups) {
        free(token->groups);
    }
    
    free(token);
}

// Create a security descriptor
security_descriptor_t* security_create_descriptor(security_sid_t owner, security_sid_t group) {
    security_descriptor_t* descriptor = (security_descriptor_t*)malloc(sizeof(security_descriptor_t));
    if (!descriptor) {
        log_error("SECURITY", "Failed to allocate memory for security descriptor");
        return NULL;
    }
    
    // Initialize descriptor
    descriptor->owner = owner;
    descriptor->group = group;
    descriptor->flags = 0;
    
    // Create empty ACLs
    descriptor->dacl = (security_acl_t*)malloc(sizeof(security_acl_t));
    if (!descriptor->dacl) {
        log_error("SECURITY", "Failed to allocate memory for DACL");
        free(descriptor);
        return NULL;
    }
    
    descriptor->dacl->ace_count = 0;
    descriptor->dacl->aces = NULL;
    
    descriptor->sacl = (security_acl_t*)malloc(sizeof(security_acl_t));
    if (!descriptor->sacl) {
        log_error("SECURITY", "Failed to allocate memory for SACL");
        free(descriptor->dacl);
        free(descriptor);
        return NULL;
    }
    
    descriptor->sacl->ace_count = 0;
    descriptor->sacl->aces = NULL;
    
    return descriptor;
}

// Free a security descriptor
void security_free_descriptor(security_descriptor_t *descriptor) {
    if (!descriptor) {
        return;
    }
    
    if (descriptor->dacl) {
        if (descriptor->dacl->aces) {
            free(descriptor->dacl->aces);
        }
        free(descriptor->dacl);
    }
    
    if (descriptor->sacl) {
        if (descriptor->sacl->aces) {
            free(descriptor->sacl->aces);
        }
        free(descriptor->sacl);
    }
    
    free(descriptor);
}

// Add an ACE to a security descriptor
int security_add_ace(security_descriptor_t *descriptor, uint8_t type, uint8_t flags, uint32_t mask, security_sid_t sid) {
    if (!descriptor) {
        return -1;
    }
    
    // Determine which ACL to add to
    security_acl_t *acl;
    if (type == ACE_TYPE_AUDIT) {
        acl = descriptor->sacl;
    } else {
        acl = descriptor->dacl;
    }
    
    if (!acl) {
        return -2;
    }
    
    // Expand ACE array
    security_ace_t *new_aces = (security_ace_t*)malloc(sizeof(security_ace_t) * (acl->ace_count + 1));
    if (!new_aces) {
        log_error("SECURITY", "Failed to allocate memory for new ACE");
        return -3;
    }
    
    if (acl->aces && acl->ace_count > 0) {
        memcpy(new_aces, acl->aces, sizeof(security_ace_t) * acl->ace_count);
        free(acl->aces);
    }
    
    // Add new ACE
    new_aces[acl->ace_count].type = type;
    new_aces[acl->ace_count].flags = flags;
    new_aces[acl->ace_count].mask = mask;
    new_aces[acl->ace_count].sid = sid;
    
    acl->aces = new_aces;
    acl->ace_count++;
    
    return 0;
}

// Check if a token has access to an object based on its security descriptor
int security_check_access(security_descriptor_t *descriptor, security_token_t *token, uint32_t desired_access) {
    if (!descriptor || !token) {
        return 0; // Deny access if descriptor or token is NULL
    }
    
    // Kernel-level tokens have full access
    if (token->privilege_level == PRIV_LEVEL_KERNEL) {
        return 1;
    }
    
    // Owner always has certain rights
    if (security_sid_equal(descriptor->owner, token->user)) {
        // Owner can always read security and modify DACL
        if (desired_access == PERM_READ || desired_access == PERM_QUERY) {
            return 1;
        }
    }
    
    // Check DACL for access
    if (!descriptor->dacl || descriptor->dacl->ace_count == 0) {
        // No DACL means full access - not recommended but supported
        return 1;
    }
    
    int i, j;
    int access_allowed = 0;
    
    // Process ACEs in order (deny entries take precedence)
    for (i = 0; i < descriptor->dacl->ace_count; i++) {
        security_ace_t *ace = &descriptor->dacl->aces[i];
        
        // Check if SID matches user or groups
        int sid_match = security_sid_equal(ace->sid, token->user);
        
        if (!sid_match && token->groups) {
            for (j = 0; j < token->group_count; j++) {
                if (security_sid_equal(ace->sid, token->groups[j])) {
                    sid_match = 1;
                    break;
                }
            }
        }
        
        // If SID matches, apply the ACE
        if (sid_match) {
            if (ace->type == ACE_TYPE_ACCESS_DENIED) {
                // If any bit matches a denied permission, deny access
                if (ace->mask & desired_access) {
                    security_audit_access(descriptor, token, desired_access, 0);
                    return 0; // Access denied
                }
            } else if (ace->type == ACE_TYPE_ACCESS_ALLOWED) {
                // If all bits are allowed, grant access
                if ((ace->mask & desired_access) == desired_access) {
                    access_allowed = 1;
                }
            }
        }
    }
    
    security_audit_access(descriptor, token, desired_access, access_allowed);
    return access_allowed;
}

// Check if the current task has a specific permission
int security_check_permission(uint32_t permission) {
    if (!current_token) {
        return 0; // No token, no permissions
    }
    
    // Kernel has all permissions
    if (current_token->privilege_level == PRIV_LEVEL_KERNEL) {
        return 1;
    }
    
    // Check against the token's privileges
    int result = ((current_token->privileges & permission) == permission);
    security_audit_permission(permission, result);
    return result;
}

// Set the current security token for the running task
void security_set_current_token(security_token_t *token) {
    current_token = token;
}

// Get the current security token for the running task
security_token_t* security_get_current_token(void) {
    return current_token;
}

// Create a SID with given authority and ID
security_sid_t security_create_sid(uint32_t authority, uint32_t id) {
    security_sid_t sid;
    sid.authority = authority;
    sid.id = id;
    return sid;
}

// Compare two SIDs for equality
int security_sid_equal(security_sid_t sid1, security_sid_t sid2) {
    return (sid1.authority == sid2.authority) && (sid1.id == sid2.id);
}

// Security audit functions
void security_audit_access(security_descriptor_t *descriptor, security_token_t *token, uint32_t desired_access, int success) {
    // Only log security events if a SACL exists with audit entries
    if (!descriptor || !descriptor->sacl || !token) {
        return;
    }
    
    int i;
    for (i = 0; i < descriptor->sacl->ace_count; i++) {
        security_ace_t *ace = &descriptor->sacl->aces[i];
        
        // Check if this is an audit ACE
        if (ace->type == ACE_TYPE_AUDIT) {
            int should_audit = 0;
            
            // Check if we should audit success or failure
            if (success && (ace->flags & ACE_FLAG_AUDIT_SUCCESS)) {
                should_audit = 1;
            } else if (!success && (ace->flags & ACE_FLAG_AUDIT_FAILURE)) {
                should_audit = 1;
            }
            
            if (should_audit && (ace->mask & desired_access)) {
                log_info("SECURITY_AUDIT", "Access %s: User %u.%u requested access 0x%x",
                       success ? "GRANTED" : "DENIED",
                       token->user.authority, token->user.id, desired_access);
            }
        }
    }
}

void security_audit_permission(uint32_t permission, int success) {
    if (!current_token) {
        return;
    }
    
    // For now, we'll just log all permission checks to debug level
    log_debug("SECURITY_AUDIT", "Permission check %s: User %u.%u requested permission 0x%x",
           success ? "GRANTED" : "DENIED",
           current_token->user.authority, current_token->user.id, permission);
}

void security_audit_action(const char *action, const char *object, security_token_t *token, int success) {
    if (!action || !object || !token) {
        return;
    }
    
    log_info("SECURITY_AUDIT", "Action %s: User %u.%u attempted '%s' on '%s'",
           success ? "ALLOWED" : "DENIED",
           token->user.authority, token->user.id, action, object);
}

// Resource protection and security validation
int security_validate_pointer(const void *ptr, size_t size, uint32_t access) {
    if (!ptr) {
        return 0;
    }
    
    // TODO: Add actual memory protection checks here
    // For now, just check for NULL and assume kernel memory access is valid
    
    // If we're requesting write access, check if memory is writable
    if (access & PERM_WRITE) {
        // Would check page tables here
    }
    
    return 1;
}

int security_validate_buffer(const void *buffer, size_t size, uint32_t access) {
    return security_validate_pointer(buffer, size, access);
}

int security_validate_string(const char *str, uint32_t access) {
    if (!str) {
        return 0;
    }
    
    // Calculate string length
    size_t len = 0;
    const char *p = str;
    while (*p++) len++;
    
    return security_validate_pointer(str, len + 1, access);
}

// Set up security for a new process
int security_setup_process(int process_id, security_token_t *token) {
    // Implementation would depend on how processes are stored
    // For now, just audit the action
    if (!token) {
        log_error("SECURITY", "Cannot set up security for process %d: NULL token", process_id);
        return -1;
    }
    
    security_audit_action("setup_process_security", "process", token, 1);
    return 0;
}

// Create default security descriptor for kernel objects
security_descriptor_t* security_create_default_descriptor(void) {
    // Create system SIDs
    security_sid_t system_sid = {0, 0}; // System/kernel SID
    security_sid_t admin_sid = {1, 0};  // Administrator SID
    
    // Create descriptor with system as owner
    security_descriptor_t *descriptor = security_create_descriptor(system_sid, system_sid);
    if (!descriptor) {
        return NULL;
    }
    
    // Add ACEs to allow system full access
    security_add_ace(descriptor, ACE_TYPE_ACCESS_ALLOWED, ACE_FLAG_OBJECT_INHERIT, PERM_ALL, system_sid);
    
    // Allow administrators full access
    security_add_ace(descriptor, ACE_TYPE_ACCESS_ALLOWED, ACE_FLAG_OBJECT_INHERIT, PERM_ALL, admin_sid);
    
    // Add audit ACE for all access attempts
    security_add_ace(descriptor, ACE_TYPE_AUDIT, ACE_FLAG_AUDIT_SUCCESS | ACE_FLAG_AUDIT_FAILURE, PERM_ALL, system_sid);
    
    return descriptor;
}