#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Security Capability Flags
 */
#define CAP_NONE               0x00000000  // No capabilities
#define CAP_CHOWN              0x00000001  // Change ownership
#define CAP_DAC_OVERRIDE       0x00000002  // Override DAC access restrictions
#define CAP_DAC_READ_SEARCH    0x00000004  // Override DAC read/search restrictions
#define CAP_FOWNER             0x00000008  // Override file ownership checks
#define CAP_FSETID             0x00000010  // Override file security bits
#define CAP_KILL               0x00000020  // Bypass permission checks for sending signals
#define CAP_SETGID             0x00000040  // Set group ID
#define CAP_SETUID             0x00000080  // Set user ID
#define CAP_SYS_ADMIN          0x00000100  // Perform system administration
#define CAP_SYS_BOOT           0x00000200  // Reboot/shutdown the system
#define CAP_SYS_CHROOT         0x00000400  // Use chroot()
#define CAP_SYS_MODULE         0x00000800  // Load/unload kernel modules
#define CAP_SYS_NICE           0x00001000  // Override scheduling restrictions
#define CAP_SYS_RESOURCE       0x00002000  // Override resource limits
#define CAP_SYS_TIME           0x00004000  // Set system time
#define CAP_NET_BIND_SERVICE   0x00008000  // Bind to privileged ports
#define CAP_NET_BROADCAST      0x00010000  // Broadcast network packets
#define CAP_NET_ADMIN          0x00020000  // Configure network interfaces
#define CAP_NET_RAW            0x00040000  // Use raw sockets
#define CAP_IPC_LOCK           0x00080000  // Lock memory
#define CAP_IPC_OWNER          0x00100000  // Override IPC ownership checks
#define CAP_SYS_PTRACE         0x00200000  // Trace processes
#define CAP_SYS_RAWIO          0x00400000  // Perform raw I/O operations
#define CAP_MKNOD              0x00800000  // Create special files
#define CAP_LEASE              0x01000000  // Establish leases on files
#define CAP_AUDIT_WRITE        0x02000000  // Write records to kernel auditing log
#define CAP_AUDIT_CONTROL      0x04000000  // Configure audit subsystem
#define CAP_SETFCAP            0x08000000  // Set file capabilities
#define CAP_MAC_OVERRIDE       0x10000000  // Override MAC access
#define CAP_MAC_ADMIN          0x20000000  // Configure MAC policy
#define CAP_SYSLOG             0x40000000  // Configure syslog
#define CAP_ALL                0xFFFFFFFF  // All capabilities

/**
 * Access Control Flags
 */
#define ACCESS_READ            0x0001      // Read access
#define ACCESS_WRITE           0x0002      // Write access
#define ACCESS_EXEC            0x0004      // Execute access
#define ACCESS_DELETE          0x0008      // Delete access
#define ACCESS_ATTR_READ       0x0010      // Read attributes
#define ACCESS_ATTR_WRITE      0x0020      // Write attributes
#define ACCESS_CREATE_FILES    0x0040      // Create files
#define ACCESS_CREATE_DIRS     0x0080      // Create directories
#define ACCESS_DELETE_CHILD    0x0100      // Delete child objects
#define ACCESS_CHANGE_PERMS    0x0200      // Change permissions
#define ACCESS_TAKE_OWNER      0x0400      // Take ownership
#define ACCESS_SYNC            0x0800      // Synchronize (wait)
#define ACCESS_ALL             0x0FFF      // All access rights

/**
 * Security Identifier (SID) structure
 */
typedef struct {
    uint32_t authority;           // Authority that issued the SID
    uint32_t sub_authority[8];    // Sub-authorities
    uint8_t sub_authority_count;  // Number of sub-authorities
} security_id_t;

/**
 * Discretionary Access Control List Entry (DACE)
 */
typedef struct dace {
    security_id_t sid;            // Security ID 
    uint16_t access_mask;         // Access rights
    uint8_t flags;                // Entry flags
    struct dace* next;            // Next entry in the list
} dace_t;

/**
 * Discretionary Access Control List (DACL)
 */
typedef struct {
    uint16_t count;               // Number of entries
    dace_t* entries;              // List of entries
} dacl_t;

/**
 * Security Descriptor structure
 */
typedef struct {
    uint16_t revision;            // Revision level
    uint16_t flags;               // Control flags
    security_id_t owner;          // Owner SID
    security_id_t group;          // Group SID
    dacl_t* dacl;                 // Discretionary access control list
    void* sacl;                   // System access control list (audit)
} security_descriptor_t;

/**
 * Access Token structure
 */
typedef struct {
    security_id_t user;           // User SID
    security_id_t* groups;        // Group SIDs
    uint16_t group_count;         // Number of groups
    uint32_t privileges;          // Privileges (capabilities)
    uint32_t session_id;          // Session ID
    void* auth_id;                // Authentication ID
    uint8_t type;                 // Token type
    uint8_t impersonation_level;  // Impersonation level
} access_token_t;

/**
 * Security Audit structure
 */
typedef struct {
    uint32_t event_id;            // Audit event ID
    uint32_t timestamp;           // Event timestamp
    security_id_t user_sid;       // User SID
    uint32_t action;              // Action performed
    uint16_t status;              // Status/result
    char object_name[64];         // Object name
    uint32_t access_mask;         // Access mask
    uint32_t process_id;          // Process ID
} security_audit_t;

/**
 * Initialize the security subsystem
 *
 * @return 0 on success, negative error code on failure
 */
int security_init(void);

/**
 * Create a security identifier (SID)
 *
 * @param authority Authority value
 * @param sub_authorities Array of sub-authorities
 * @param count Number of sub-authorities
 * @param sid Pointer to SID structure to fill
 * @return 0 on success, negative error code on failure
 */
int security_create_sid(uint32_t authority, uint32_t* sub_authorities, 
                        uint8_t count, security_id_t* sid);

/**
 * Compare two security identifiers
 *
 * @param sid1 First SID
 * @param sid2 Second SID
 * @return 0 if equal, non-zero if different
 */
int security_compare_sid(const security_id_t* sid1, const security_id_t* sid2);

/**
 * Convert a security identifier to string format
 *
 * @param sid Security identifier
 * @param buffer Buffer to store the string
 * @param size Buffer size
 * @return 0 on success, negative error code on failure
 */
int security_sid_to_string(const security_id_t* sid, char* buffer, size_t size);

/**
 * Create a security descriptor
 *
 * @param owner Owner SID
 * @param group Group SID
 * @param descriptor Pointer to descriptor structure to fill
 * @return 0 on success, negative error code on failure
 */
int security_create_descriptor(const security_id_t* owner, 
                             const security_id_t* group,
                             security_descriptor_t* descriptor);

/**
 * Add an access control entry to a descriptor
 *
 * @param descriptor Security descriptor
 * @param sid Security identifier
 * @param access_mask Access rights
 * @param flags Entry flags
 * @return 0 on success, negative error code on failure
 */
int security_add_ace(security_descriptor_t* descriptor,
                   const security_id_t* sid,
                   uint16_t access_mask,
                   uint8_t flags);

/**
 * Create an access token
 *
 * @param user User SID
 * @param groups Array of group SIDs
 * @param group_count Number of groups
 * @param privileges Privileges (capabilities)
 * @param token Pointer to token structure to fill
 * @return 0 on success, negative error code on failure
 */
int security_create_token(const security_id_t* user,
                        const security_id_t* groups,
                        uint16_t group_count,
                        uint32_t privileges,
                        access_token_t* token);

/**
 * Check if an access token has a capability
 *
 * @param token Access token
 * @param capability Capability to check
 * @return true if token has capability, false otherwise
 */
bool security_check_capability(const access_token_t* token, uint32_t capability);

/**
 * Check if an access token is allowed access to an object
 *
 * @param token Access token
 * @param descriptor Security descriptor
 * @param desired_access Desired access
 * @return true if access is granted, false otherwise
 */
bool security_check_access(const access_token_t* token,
                         const security_descriptor_t* descriptor,
                         uint16_t desired_access);

/**
 * Generate a security audit entry
 *
 * @param token Access token
 * @param object_name Object name
 * @param action Action performed
 * @param access_mask Access mask
 * @param status Status/result
 * @return 0 on success, negative error code on failure
 */
int security_audit(const access_token_t* token,
                  const char* object_name,
                  uint32_t action,
                  uint32_t access_mask,
                  uint16_t status);

/**
 * Get the security descriptor for an object
 *
 * @param object_name Object name
 * @param descriptor Pointer to descriptor structure to fill
 * @return 0 on success, negative error code on failure
 */
int security_get_object_descriptor(const char* object_name,
                                 security_descriptor_t* descriptor);

/**
 * Set the security descriptor for an object
 *
 * @param object_name Object name
 * @param descriptor Security descriptor
 * @return 0 on success, negative error code on failure
 */
int security_set_object_descriptor(const char* object_name,
                                 const security_descriptor_t* descriptor);

/**
 * Get the current process's access token
 *
 * @param token Pointer to token structure to fill
 * @return 0 on success, negative error code on failure
 */
int security_get_current_token(access_token_t* token);

/**
 * Impersonate a user
 *
 * @param token Access token to impersonate
 * @return 0 on success, negative error code on failure
 */
int security_impersonate_user(const access_token_t* token);

/**
 * Revert to self (stop impersonation)
 *
 * @return 0 on success, negative error code on failure
 */
int security_revert_to_self(void);

/**
 * Add privilege to an access token
 * 
 * @param token Access token
 * @param privilege Privilege to add
 * @return 0 on success, negative error code on failure
 */
int security_add_privilege(access_token_t* token, uint32_t privilege);

/**
 * Remove privilege from an access token
 *
 * @param token Access token
 * @param privilege Privilege to remove
 * @return 0 on success, negative error code on failure
 */
int security_remove_privilege(access_token_t* token, uint32_t privilege);

/**
 * Check if the current process has a capability
 *
 * @param capability Capability to check
 * @return true if process has capability, false otherwise
 */
bool security_current_has_capability(uint32_t capability);

/**
 * Check if the current process is allowed access to an object
 *
 * @param object_name Object name
 * @param desired_access Desired access
 * @return true if access is granted, false otherwise
 */
bool security_current_can_access(const char* object_name, uint16_t desired_access);

#endif /* SECURITY_H */