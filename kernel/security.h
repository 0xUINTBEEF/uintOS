#ifndef SECURITY_H
#define SECURITY_H

#include <inttypes.h>
#include <stddef.h>

// Security Identifier (SID) - identifies users, groups, and resources
typedef struct {
    uint32_t authority;     // Authority that issued the SID
    uint32_t id;            // Identifier value
} security_sid_t;

// Privilege levels for tasks and resources
#define PRIV_LEVEL_KERNEL   0
#define PRIV_LEVEL_DRIVER   1
#define PRIV_LEVEL_SYSTEM   2
#define PRIV_LEVEL_USER     3

// Standard permissions (bitflags)
#define PERM_READ           0x0001
#define PERM_WRITE          0x0002
#define PERM_EXECUTE        0x0004
#define PERM_DELETE         0x0008
#define PERM_MODIFY         0x0010
#define PERM_QUERY          0x0020
#define PERM_CONTROL        0x0040
#define PERM_MAP            0x0080
#define PERM_CREATE         0x0100
#define PERM_ALLOCATE       0x0200
#define PERM_MODIFY_SECURITY 0x0400
#define PERM_CHANGE_PRIVILEGE 0x0800
#define PERM_SHUTDOWN       0x1000
#define PERM_DEBUG          0x2000
#define PERM_IO             0x4000
#define PERM_ALL            0xFFFF

// Security token - represents the security context of a task
typedef struct {
    security_sid_t user;            // User SID
    security_sid_t *groups;         // Group SIDs
    uint32_t group_count;           // Number of groups
    uint32_t privileges;            // Task privileges
    uint32_t privilege_level;       // Privilege level (0-3)
} security_token_t;

// ACE (Access Control Entry) types
#define ACE_TYPE_ACCESS_ALLOWED 0
#define ACE_TYPE_ACCESS_DENIED  1
#define ACE_TYPE_AUDIT          2

// ACE flags
#define ACE_FLAG_OBJECT_INHERIT    0x0001
#define ACE_FLAG_CONTAINER_INHERIT 0x0002
#define ACE_FLAG_NO_PROPAGATE      0x0004
#define ACE_FLAG_INHERIT_ONLY      0x0008
#define ACE_FLAG_AUDIT_SUCCESS     0x0010
#define ACE_FLAG_AUDIT_FAILURE     0x0020

// Access Control Entry - defines permissions for a SID
typedef struct {
    uint8_t type;               // Type of ACE (allow, deny, audit)
    uint8_t flags;              // Inheritance flags
    uint32_t mask;              // Access mask (permissions)
    security_sid_t sid;         // SID this ACE applies to
} security_ace_t;

// Access Control List - list of ACEs
typedef struct {
    uint32_t ace_count;         // Number of ACEs
    security_ace_t *aces;       // Array of ACEs
} security_acl_t;

// Security descriptor - defines the security for an object
typedef struct {
    security_sid_t owner;       // Owner SID
    security_sid_t group;       // Primary group SID
    security_acl_t *dacl;       // Discretionary ACL - controls access
    security_acl_t *sacl;       // System ACL - for auditing
    uint32_t flags;             // Various flags
} security_descriptor_t;

// Initialize the security system
int security_init(void);

// Create a new security token
security_token_t* security_create_token(uint32_t user_id, security_sid_t *groups, uint32_t group_count, uint32_t privilege_level);

// Free a security token
void security_free_token(security_token_t *token);

// Create a security descriptor
security_descriptor_t* security_create_descriptor(security_sid_t owner, security_sid_t group);

// Free a security descriptor
void security_free_descriptor(security_descriptor_t *descriptor);

// Add an ACE to a security descriptor
int security_add_ace(security_descriptor_t *descriptor, uint8_t type, uint8_t flags, uint32_t mask, security_sid_t sid);

// Check if a token has access to an object based on its security descriptor
int security_check_access(security_descriptor_t *descriptor, security_token_t *token, uint32_t desired_access);

// Check if the current task has a specific permission
int security_check_permission(uint32_t permission);

// Set the current security token for the running task
void security_set_current_token(security_token_t *token);

// Get the current security token for the running task
security_token_t* security_get_current_token(void);

// Create a SID with given authority and ID
security_sid_t security_create_sid(uint32_t authority, uint32_t id);

// Compare two SIDs for equality
int security_sid_equal(security_sid_t sid1, security_sid_t sid2);

// Security audit functions
void security_audit_access(security_descriptor_t *descriptor, security_token_t *token, uint32_t desired_access, int success);
void security_audit_permission(uint32_t permission, int success);
void security_audit_action(const char *action, const char *object, security_token_t *token, int success);

// Resource protection and security validation
int security_validate_pointer(const void *ptr, size_t size, uint32_t access);
int security_validate_buffer(const void *buffer, size_t size, uint32_t access);
int security_validate_string(const char *str, uint32_t access);

// Set up security for a new process
int security_setup_process(int process_id, security_token_t *token);

// Create default security descriptor for kernel objects
security_descriptor_t* security_create_default_descriptor(void);

#endif // SECURITY_H