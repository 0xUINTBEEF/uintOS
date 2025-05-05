// security_monitor.c - Security Audit and Monitoring System
// Enhanced security monitoring, auditing and intrusion detection

#include "security_monitor.h"
#include "security.h"
#include "logging/log.h"
#include "kernel.h"
#include "sync.h"
#include "memory/heap.h"
#include "task.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Security event types
#define SEC_EVENT_LOGIN            1  // User login
#define SEC_EVENT_LOGOUT           2  // User logout
#define SEC_EVENT_ACCESS_DENIED    3  // Access denied
#define SEC_EVENT_PRIVILEGE_USE    4  // Privilege use
#define SEC_EVENT_POLICY_CHANGE    5  // Policy change
#define SEC_EVENT_ACCOUNT_CHANGE   6  // Account change
#define SEC_EVENT_PROCESS_START    7  // Process start
#define SEC_EVENT_PROCESS_STOP     8  // Process stop
#define SEC_EVENT_OBJECT_ACCESS    9  // Object access
#define SEC_EVENT_SYSTEM_START     10 // System start
#define SEC_EVENT_SYSTEM_STOP      11 // System stop

// Security modes
#define SEC_MODE_DISABLED          0  // Security disabled
#define SEC_MODE_AUDIT_ONLY        1  // Audit only
#define SEC_MODE_ENFORCING         2  // Enforcing

// Current security mode
static uint8_t security_mode = SEC_MODE_ENFORCING;

// Security policy settings
typedef struct {
    uint32_t min_password_length;
    uint32_t max_login_attempts;
    uint32_t password_expiry_days;
    uint32_t audit_level;
    uint32_t mandatory_access_policy;
    bool enforce_object_permissions;
    bool restrict_kernel_access;
    bool enable_capabilities;
} security_policy_t;

static security_policy_t security_policy = {
    .min_password_length = 8,
    .max_login_attempts = 5,
    .password_expiry_days = 90,
    .audit_level = 3,
    .mandatory_access_policy = 2,
    .enforce_object_permissions = true,
    .restrict_kernel_access = true,
    .enable_capabilities = true
};

// Security statistics
typedef struct {
    uint32_t access_denied_count;
    uint32_t login_failure_count;
    uint32_t privilege_escalations;
    uint32_t security_violations;
    uint32_t audit_events;
} security_stats_t;

static security_stats_t security_stats = {0};

// Security event log circular buffer
#define MAX_SECURITY_EVENTS 128
typedef struct {
    uint32_t event_type;
    uint32_t timestamp;
    security_id_t sid;
    char resource[64];
    uint32_t result;
    char details[128];
} security_event_t;

static security_event_t event_log[MAX_SECURITY_EVENTS];
static uint32_t next_event = 0;
static uint32_t total_events = 0;

// Internal function prototypes
static void log_security_event(uint32_t event_type, const security_id_t* sid,
                         const char* resource, uint32_t result, const char* details);
static bool check_security_policy(const access_token_t* token, 
                                 const char* resource, 
                                 uint32_t access_type);

/**
 * Initialize the security monitor
 */
int security_monitor_init(void) {
    log_info("Initializing Security Monitor");
    
    // Log system start event
    access_token_t token;
    security_get_current_token(&token);
    log_security_event(SEC_EVENT_SYSTEM_START, &token.user, 
                     "system", 0, "System security monitor initialized");
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    log_info("Security monitor initialized in %s mode", 
             security_mode == SEC_MODE_DISABLED ? "disabled" :
             security_mode == SEC_MODE_AUDIT_ONLY ? "audit only" : "enforcing");
    
    return 0;
}

/**
 * Set security mode
 */
int security_monitor_set_mode(uint8_t mode) {
    if (mode > SEC_MODE_ENFORCING) {
        return -1;
    }
    
    // Check if user has capability to change security mode
    if (!security_current_has_capability(CAP_SYS_ADMIN)) {
        log_warning("Attempt to change security mode without proper privileges");
        security_stats.security_violations++;
        return -1;
    }
    
    // Log the mode change
    access_token_t token;
    security_get_current_token(&token);
    char details[128];
    snprintf(details, sizeof(details), "Security mode changed from %u to %u", 
             security_mode, mode);
    log_security_event(SEC_EVENT_POLICY_CHANGE, &token.user, 
                     "security_mode", 0, details);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    // Change the mode
    security_mode = mode;
    
    return 0;
}

/**
 * Get security mode
 */
uint8_t security_monitor_get_mode(void) {
    return security_mode;
}

/**
 * Monitor a resource access
 * 
 * @return 0 if access is allowed, negative error code if access is denied
 */
int security_monitor_resource_access(const char* resource, uint32_t access_type) {
    if (!resource) {
        return -1;
    }
    
    // Get current token
    access_token_t token;
    if (security_get_current_token(&token) != 0) {
        return -1;
    }
    
    // Check if access is allowed
    bool allowed = check_security_policy(&token, resource, access_type);
    
    // Log the access attempt
    char details[128];
    snprintf(details, sizeof(details), "Access type: %u", access_type);
    log_security_event(SEC_EVENT_OBJECT_ACCESS, &token.user, 
                     resource, allowed ? 0 : 1, details);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    // If access is denied and we're in enforcing mode, return error
    if (!allowed && security_mode == SEC_MODE_ENFORCING) {
        security_stats.access_denied_count++;
        log_warning("Access denied to resource '%s', access type %u", 
                  resource, access_type);
        return -1;
    }
    
    return 0;
}

/**
 * Monitor a privilege use
 * 
 * @return 0 if privilege use is allowed, negative error code if denied
 */
int security_monitor_privilege_use(uint32_t privilege) {
    // Get current token
    access_token_t token;
    if (security_get_current_token(&token) != 0) {
        return -1;
    }
    
    // Check if token has the privilege
    bool has_privilege = security_check_capability(&token, privilege);
    
    // Log the privilege use
    char details[128];
    snprintf(details, sizeof(details), "Privilege: 0x%08x", privilege);
    log_security_event(SEC_EVENT_PRIVILEGE_USE, &token.user, 
                     "privilege", has_privilege ? 0 : 1, details);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    // If privilege is not granted and we're in enforcing mode, return error
    if (!has_privilege && security_mode == SEC_MODE_ENFORCING) {
        security_stats.privilege_escalations++;
        log_warning("Privilege use denied: 0x%08x", privilege);
        return -1;
    }
    
    return 0;
}

/**
 * Monitor user login
 * 
 * @param username Username
 * @param success Whether login was successful
 */
void security_monitor_login(const char* username, bool success) {
    if (!username) {
        return;
    }
    
    // Get current token
    access_token_t token;
    if (security_get_current_token(&token) != 0) {
        return;
    }
    
    // Log the login
    char details[128];
    snprintf(details, sizeof(details), "Username: %s", username);
    log_security_event(SEC_EVENT_LOGIN, &token.user, 
                     "login", success ? 0 : 1, details);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    // Update statistics
    if (!success) {
        security_stats.login_failure_count++;
    }
}

/**
 * Monitor process start
 * 
 * @param process_name Process name
 * @param pid Process ID
 */
void security_monitor_process_start(const char* process_name, uint32_t pid) {
    if (!process_name) {
        return;
    }
    
    // Get current token
    access_token_t token;
    if (security_get_current_token(&token) != 0) {
        return;
    }
    
    // Log the process start
    char details[128];
    snprintf(details, sizeof(details), "PID: %u", pid);
    log_security_event(SEC_EVENT_PROCESS_START, &token.user, 
                     process_name, 0, details);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
}

/**
 * Monitor process stop
 * 
 * @param process_name Process name
 * @param pid Process ID
 * @param exit_code Exit code
 */
void security_monitor_process_stop(const char* process_name, uint32_t pid, int exit_code) {
    if (!process_name) {
        return;
    }
    
    // Get current token
    access_token_t token;
    if (security_get_current_token(&token) != 0) {
        return;
    }
    
    // Log the process stop
    char details[128];
    snprintf(details, sizeof(details), "PID: %u, Exit code: %d", pid, exit_code);
    log_security_event(SEC_EVENT_PROCESS_STOP, &token.user, 
                     process_name, 0, details);
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
}

/**
 * Get security statistics
 */
void security_monitor_get_stats(security_monitor_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    // Copy statistics
    stats->access_denied_count = security_stats.access_denied_count;
    stats->login_failure_count = security_stats.login_failure_count;
    stats->privilege_escalations = security_stats.privilege_escalations;
    stats->security_violations = security_stats.security_violations;
    stats->audit_events = security_stats.audit_events;
    stats->total_events = total_events;
}

/**
 * Reset security statistics
 */
void security_monitor_reset_stats(void) {
    // Check if user has capability to reset stats
    if (!security_current_has_capability(CAP_SYS_ADMIN)) {
        log_warning("Attempt to reset security stats without proper privileges");
        security_stats.security_violations++;
        return;
    }
    
    // Reset statistics
    memset(&security_stats, 0, sizeof(security_stats));
    log_info("Security statistics reset");
}

/**
 * Set security policy
 */
int security_monitor_set_policy(const security_monitor_policy_t* policy) {
    if (!policy) {
        return -1;
    }
    
    // Check if user has capability to change policy
    if (!security_current_has_capability(CAP_SYS_ADMIN)) {
        log_warning("Attempt to change security policy without proper privileges");
        security_stats.security_violations++;
        return -1;
    }
    
    // Apply policy changes
    security_policy.min_password_length = policy->min_password_length;
    security_policy.max_login_attempts = policy->max_login_attempts;
    security_policy.password_expiry_days = policy->password_expiry_days;
    security_policy.audit_level = policy->audit_level;
    security_policy.enforce_object_permissions = policy->enforce_object_permissions;
    security_policy.restrict_kernel_access = policy->restrict_kernel_access;
    security_policy.enable_capabilities = policy->enable_capabilities;
    
    // Log the policy change
    access_token_t token;
    security_get_current_token(&token);
    log_security_event(SEC_EVENT_POLICY_CHANGE, &token.user, 
                     "security_policy", 0, "Security policy updated");
    
    // Free the token if groups were allocated
    if (token.groups) {
        heap_free(token.groups);
    }
    
    log_info("Security policy updated");
    return 0;
}

/**
 * Get security policy
 */
void security_monitor_get_policy(security_monitor_policy_t* policy) {
    if (!policy) {
        return;
    }
    
    // Copy policy
    policy->min_password_length = security_policy.min_password_length;
    policy->max_login_attempts = security_policy.max_login_attempts;
    policy->password_expiry_days = security_policy.password_expiry_days;
    policy->audit_level = security_policy.audit_level;
    policy->enforce_object_permissions = security_policy.enforce_object_permissions;
    policy->restrict_kernel_access = security_policy.restrict_kernel_access;
    policy->enable_capabilities = security_policy.enable_capabilities;
}

/**
 * Log a security event
 */
static void log_security_event(uint32_t event_type, const security_id_t* sid,
                         const char* resource, uint32_t result, const char* details) {
    if (!sid || !resource || !details) {
        return;
    }
    
    // Skip logging if audit level is too low
    if (security_policy.audit_level < 2 && result == 0) {
        // Success events are only logged at level 2 and above
        return;
    }
    
    // Get next event slot
    security_event_t* event = &event_log[next_event];
    next_event = (next_event + 1) % MAX_SECURITY_EVENTS;
    
    // Fill in event
    event->event_type = event_type;
    event->timestamp = 0; // TODO: Get system time
    memcpy(&event->sid, sid, sizeof(security_id_t));
    strncpy(event->resource, resource, sizeof(event->resource) - 1);
    event->resource[sizeof(event->resource) - 1] = '\0';
    event->result = result;
    strncpy(event->details, details, sizeof(event->details) - 1);
    event->details[sizeof(event->details) - 1] = '\0';
    
    // Update statistics
    security_stats.audit_events++;
    total_events++;
    
    // Log to system log if result indicates failure
    if (result != 0) {
        char sid_str[64];
        security_sid_to_string(sid, sid_str, sizeof(sid_str));
        log_warning("SECURITY: Event %u, User %s, Resource %s, Result %u, %s", 
                  event_type, sid_str, resource, result, details);
    } else if (security_policy.audit_level >= 3) {
        // Log all events at audit level 3+
        char sid_str[64];
        security_sid_to_string(sid, sid_str, sizeof(sid_str));
        log_info("SECURITY: Event %u, User %s, Resource %s, Result %u, %s", 
               event_type, sid_str, resource, result, details);
    }
}

/**
 * Check if an access is allowed by security policy
 */
static bool check_security_policy(const access_token_t* token, 
                                 const char* resource, 
                                 uint32_t access_type) {
    if (!token || !resource) {
        return false;
    }
    
    // If security is disabled, allow all access
    if (security_mode == SEC_MODE_DISABLED) {
        return true;
    }
    
    // Check if object permissions are enforced
    if (security_policy.enforce_object_permissions) {
        // Convert access_type to access mask for security
        uint16_t desired_access = 0;
        
        // Map access types to access mask
        if (access_type & RESOURCE_ACCESS_READ) {
            desired_access |= ACCESS_READ;
        }
        if (access_type & RESOURCE_ACCESS_WRITE) {
            desired_access |= ACCESS_WRITE;
        }
        if (access_type & RESOURCE_ACCESS_EXECUTE) {
            desired_access |= ACCESS_EXEC;
        }
        if (access_type & RESOURCE_ACCESS_DELETE) {
            desired_access |= ACCESS_DELETE;
        }
        
        // Get security descriptor for the resource
        security_descriptor_t descriptor;
        if (security_get_object_descriptor(resource, &descriptor) != 0) {
            return false;
        }
        
        // Check if token has access
        bool result = security_check_access(token, &descriptor, desired_access);
        
        // Free the DACL if it was created
        if (descriptor.dacl) {
            free_dacl(descriptor.dacl);
        }
        
        // If access is denied, return false
        if (!result) {
            return false;
        }
    }
    
    // Check if kernel access is restricted
    if (security_policy.restrict_kernel_access && 
        strncmp(resource, "kernel/", 7) == 0) {
        // For kernel resources, require SYS_ADMIN capability
        if (!security_check_capability(token, CAP_SYS_ADMIN)) {
            return false;
        }
    }
    
    return true;
}