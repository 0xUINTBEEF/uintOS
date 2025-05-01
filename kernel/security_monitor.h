// security_monitor.h - Security Audit and Monitoring System Header
// Provides enhanced security auditing, intrusion detection, and security event analysis

#ifndef _SECURITY_MONITOR_H
#define _SECURITY_MONITOR_H

#include "security.h"
#include <stdint.h>

// Security event types for monitoring
#define SEC_EVENT_ACCESS_VIOLATION   0x01
#define SEC_EVENT_PRIVILEGE_ESCALATION 0x02
#define SEC_EVENT_UNAUTHORIZED_ACCESS 0x03
#define SEC_EVENT_RESOURCE_EXHAUSTION 0x04
#define SEC_EVENT_TAMPERING          0x05
#define SEC_EVENT_AUTHENTICATION     0x06
#define SEC_EVENT_INTRUSION_ATTEMPT  0x07

// Security monitoring configuration
typedef struct {
    uint32_t log_level;            // Audit log verbosity (0-3)
    uint32_t monitor_flags;        // What types of events to monitor
    uint32_t alert_threshold;      // Number of violations before generating alert
    uint32_t retention_period;     // How long to keep audit records (in seconds)
    int auto_block_threshold;      // Auto-block after this many violations (-1 to disable)
} security_monitor_config_t;

// Initialize the security monitoring system
int security_monitor_init(void);

// Record a security event
void security_monitor_record_event(
    uint32_t event_type,
    uint32_t severity,
    security_sid_t subject_sid, 
    security_sid_t object_sid,
    const char* resource_name,
    uint32_t desired_access,
    int success,
    const char* details
);

// Enhanced audit function that builds on security_audit_access
void security_monitor_access(
    security_descriptor_t *descriptor, 
    security_token_t *token, 
    uint32_t desired_access, 
    int success,
    const char* resource_name
);

// Monitor for privilege escalation attempts
void security_monitor_privilege(
    security_token_t *token, 
    uint32_t requested_privilege,
    int success
);

// Check for potential intrusion based on multiple access violations
int security_monitor_check_intrusion(security_sid_t sid);

// Configure the security monitor
void security_monitor_configure(security_monitor_config_t *config);

// Get security monitoring statistics
void security_monitor_get_stats(uint32_t *events_by_type, uint32_t *total);

// Clean up old security events
void security_monitor_cleanup(void);

// Analyze recent security events for patterns
void security_monitor_analyze(void);

#endif // _SECURITY_MONITOR_H