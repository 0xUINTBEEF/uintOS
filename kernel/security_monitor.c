// security_monitor.c - Security Audit and Monitoring System
// Enhanced security monitoring, auditing and intrusion detection

#include "security_monitor.h"
#include "security.h"
#include "logging/log.h"
#include "kernel.h"
#include "sync.h"
#include <stdint.h>
#include <string.h>

#define MAX_SECURITY_EVENTS 256
#define MAX_RESOURCE_NAME_LEN 64
#define MAX_DETAILS_LEN 128

typedef struct {
    uint64_t timestamp;
    uint32_t event_type;
    uint32_t severity;
    security_sid_t subject_sid;
    security_sid_t object_sid;
    char resource_name[MAX_RESOURCE_NAME_LEN];
    uint32_t desired_access;
    int success;
    char details[MAX_DETAILS_LEN];
} security_event_t;

// Circular buffer for security events
static security_event_t security_events[MAX_SECURITY_EVENTS];
static uint32_t security_event_count = 0;
static uint32_t security_event_next = 0;
static mutex_t security_monitor_mutex;

// Configuration
static security_monitor_config_t current_config = {
    .log_level = 2,
    .monitor_flags = 0xFFFFFFFF,  // Monitor all events by default
    .alert_threshold = 3,
    .retention_period = 86400,    // 24 hours
    .auto_block_threshold = 5
};

// Statistics
static uint32_t events_by_type[8] = {0}; // Corresponds to event types defined in header
static uint32_t blocked_sids[32] = {0};  // SIDs that have been auto-blocked
static uint32_t blocked_sid_count = 0;

int security_monitor_init(void) {
    LOG_INFO("Initializing security monitoring system");
    
    mutex_init(&security_monitor_mutex);
    memset(security_events, 0, sizeof(security_events));
    security_event_count = 0;
    security_event_next = 0;
    
    for (int i = 0; i < 8; i++) {
        events_by_type[i] = 0;
    }
    
    LOG_INFO("Security monitoring system initialized");
    return 0;
}

void security_monitor_record_event(
    uint32_t event_type,
    uint32_t severity,
    security_sid_t subject_sid, 
    security_sid_t object_sid,
    const char* resource_name,
    uint32_t desired_access,
    int success,
    const char* details
) {
    mutex_lock(&security_monitor_mutex);
    
    // Only record if the event type is enabled in configuration
    if (!(current_config.monitor_flags & (1 << (event_type & 0x07)))) {
        mutex_unlock(&security_monitor_mutex);
        return;
    }
    
    security_event_t *event = &security_events[security_event_next];
    
    // Record the event
    event->timestamp = get_system_time();
    event->event_type = event_type;
    event->severity = severity;
    event->subject_sid = subject_sid;
    event->object_sid = object_sid;
    event->desired_access = desired_access;
    event->success = success;
    
    // Copy strings with length limits
    if (resource_name) {
        strncpy(event->resource_name, resource_name, MAX_RESOURCE_NAME_LEN - 1);
        event->resource_name[MAX_RESOURCE_NAME_LEN - 1] = '\0';
    } else {
        event->resource_name[0] = '\0';
    }
    
    if (details) {
        strncpy(event->details, details, MAX_DETAILS_LEN - 1);
        event->details[MAX_DETAILS_LEN - 1] = '\0';
    } else {
        event->details[0] = '\0';
    }
    
    // Update statistics
    if (event_type < 8) {
        events_by_type[event_type]++;
    }
    
    // Advance circular buffer
    security_event_next = (security_event_next + 1) % MAX_SECURITY_EVENTS;
    if (security_event_count < MAX_SECURITY_EVENTS) {
        security_event_count++;
    }
    
    // Log based on severity and configuration
    if (severity >= current_config.log_level) {
        if (severity >= 3) {
            LOG_ERROR("SECURITY [%s]: %s access to %s (SID:%d->%d) %s", 
                     (event_type == SEC_EVENT_ACCESS_VIOLATION) ? "VIOLATION" :
                     (event_type == SEC_EVENT_PRIVILEGE_ESCALATION) ? "PRIVILEGE" :
                     (event_type == SEC_EVENT_UNAUTHORIZED_ACCESS) ? "UNAUTHORIZED" :
                     (event_type == SEC_EVENT_INTRUSION_ATTEMPT) ? "INTRUSION" : "EVENT",
                     success ? "Allowed" : "Denied",
                     event->resource_name, 
                     event->subject_sid,
                     event->object_sid,
                     event->details);
        } else if (severity >= 2) {
            LOG_WARNING("SECURITY: %s - %s", event->resource_name, event->details);
        } else {
            LOG_INFO("SECURITY: %s - %s", event->resource_name, event->details);
        }
    }
    
    // Auto-block if configured and threshold exceeded
    if (current_config.auto_block_threshold > 0 && 
        !success && event_type == SEC_EVENT_ACCESS_VIOLATION) {
        
        // Count violations for this SID
        int violation_count = 0;
        for (uint32_t i = 0; i < security_event_count && i < MAX_SECURITY_EVENTS; i++) {
            uint32_t idx = (security_event_next + MAX_SECURITY_EVENTS - i - 1) % MAX_SECURITY_EVENTS;
            security_event_t *past_event = &security_events[idx];
            
            // Check if this is a violation from the same subject in the last minute
            if (past_event->subject_sid == subject_sid &&
                past_event->event_type == SEC_EVENT_ACCESS_VIOLATION &&
                !past_event->success &&
                event->timestamp - past_event->timestamp < 60) {
                violation_count++;
            }
        }
        
        // If threshold exceeded, auto-block the SID
        if (violation_count >= current_config.auto_block_threshold && 
            blocked_sid_count < sizeof(blocked_sids) / sizeof(blocked_sids[0])) {
            
            // Check if SID is already blocked
            int already_blocked = 0;
            for (uint32_t i = 0; i < blocked_sid_count; i++) {
                if (blocked_sids[i] == subject_sid) {
                    already_blocked = 1;
                    break;
                }
            }
            
            if (!already_blocked) {
                blocked_sids[blocked_sid_count++] = subject_sid;
                
                LOG_ERROR("SECURITY: Auto-blocked SID:%d after %d violations", 
                         subject_sid, violation_count);
                
                // Can call security system to revoke permissions
                // security_block_sid(subject_sid);
            }
        }
    }
    
    mutex_unlock(&security_monitor_mutex);
}

void security_monitor_access(
    security_descriptor_t *descriptor, 
    security_token_t *token, 
    uint32_t desired_access, 
    int success,
    const char* resource_name
) {
    // Forward to audit system if it exists
    if (security_audit_access) {
        security_audit_access(descriptor, token, desired_access, success);
    }
    
    uint32_t severity = 1; // Default to low severity
    uint32_t event_type = success ? SEC_EVENT_AUTHENTICATION : SEC_EVENT_ACCESS_VIOLATION;
    
    // Determine severity based on access type
    if ((desired_access & PERM_KERNEL) == PERM_KERNEL) {
        severity = 3; // High severity for kernel access
    } else if ((desired_access & PERM_WRITE) == PERM_WRITE) {
        severity = 2; // Medium severity for write access
    }
    
    char details[MAX_DETAILS_LEN];
    snprintf(details, MAX_DETAILS_LEN, "Access request: 0x%08X %s", 
             desired_access, 
             success ? "granted" : "denied");
    
    security_monitor_record_event(
        event_type,
        severity,
        token ? token->user_sid : 0,
        descriptor ? descriptor->owner_sid : 0,
        resource_name ? resource_name : "unknown",
        desired_access,
        success,
        details
    );
}

void security_monitor_privilege(
    security_token_t *token, 
    uint32_t requested_privilege,
    int success
) {
    char details[MAX_DETAILS_LEN];
    snprintf(details, MAX_DETAILS_LEN, "Privilege request: 0x%08X %s", 
             requested_privilege, 
             success ? "granted" : "denied");
    
    security_monitor_record_event(
        success ? SEC_EVENT_AUTHENTICATION : SEC_EVENT_PRIVILEGE_ESCALATION,
        success ? 1 : 3,  // Failed privilege escalation is high severity
        token ? token->user_sid : 0,
        0,  // No object SID for privilege operations
        "privilege",
        requested_privilege,
        success,
        details
    );
}

int security_monitor_check_intrusion(security_sid_t sid) {
    int is_intrusion = 0;
    int violation_count = 0;
    uint64_t current_time = get_system_time();
    
    mutex_lock(&security_monitor_mutex);
    
    // Check if SID is already blocked
    for (uint32_t i = 0; i < blocked_sid_count; i++) {
        if (blocked_sids[i] == sid) {
            is_intrusion = 1;
            break;
        }
    }
    
    if (!is_intrusion) {
        // Count violations for this SID in the last 5 minutes
        for (uint32_t i = 0; i < security_event_count && i < MAX_SECURITY_EVENTS; i++) {
            uint32_t idx = (security_event_next + MAX_SECURITY_EVENTS - i - 1) % MAX_SECURITY_EVENTS;
            security_event_t *event = &security_events[idx];
            
            if (event->subject_sid == sid &&
                (event->event_type == SEC_EVENT_ACCESS_VIOLATION || 
                 event->event_type == SEC_EVENT_PRIVILEGE_ESCALATION) &&
                !event->success &&
                current_time - event->timestamp < 300) { // 5 minutes
                violation_count++;
            }
        }
        
        if (violation_count >= current_config.alert_threshold) {
            is_intrusion = 1;
            
            // Record the intrusion attempt
            char details[MAX_DETAILS_LEN];
            snprintf(details, MAX_DETAILS_LEN, 
                     "Potential intrusion detected: %d violations in 5 minutes", 
                     violation_count);
            
            security_monitor_record_event(
                SEC_EVENT_INTRUSION_ATTEMPT,
                3,  // High severity
                sid,
                0,  // No object SID
                "system",
                0,  // No specific access
                0,  // Always unsuccessful
                details
            );
        }
    }
    
    mutex_unlock(&security_monitor_mutex);
    return is_intrusion;
}

void security_monitor_configure(security_monitor_config_t *config) {
    mutex_lock(&security_monitor_mutex);
    
    if (config) {
        memcpy(&current_config, config, sizeof(security_monitor_config_t));
        LOG_INFO("Security monitor configuration updated");
    }
    
    mutex_unlock(&security_monitor_mutex);
}

void security_monitor_get_stats(uint32_t *events_by_type_out, uint32_t *total) {
    mutex_lock(&security_monitor_mutex);
    
    if (events_by_type_out) {
        memcpy(events_by_type_out, events_by_type, sizeof(events_by_type));
    }
    
    if (total) {
        *total = security_event_count;
    }
    
    mutex_unlock(&security_monitor_mutex);
}

void security_monitor_cleanup(void) {
    mutex_lock(&security_monitor_mutex);
    
    uint64_t cutoff_time = get_system_time() - current_config.retention_period;
    uint32_t removed = 0;
    
    // This is inefficient in a circular buffer but works for periodic cleanup
    for (uint32_t i = 0; i < security_event_count; i++) {
        uint32_t idx = (security_event_next + MAX_SECURITY_EVENTS - i - 1) % MAX_SECURITY_EVENTS;
        security_event_t *event = &security_events[idx];
        
        if (event->timestamp < cutoff_time) {
            // This event is old enough to remove
            // Actually just mark it as invalid by zeroing the timestamp
            event->timestamp = 0;
            removed++;
        }
    }
    
    if (removed > 0) {
        LOG_INFO("Security monitor cleanup: removed %d old events", removed);
    }
    
    mutex_unlock(&security_monitor_mutex);
}

void security_monitor_analyze(void) {
    mutex_lock(&security_monitor_mutex);
    
    uint64_t current_time = get_system_time();
    uint64_t analysis_window = 3600;  // Last hour
    uint64_t window_start = current_time - analysis_window;
    
    // Maps from SID to violation count
    uint32_t violation_counts[32] = {0};
    security_sid_t violation_sids[32] = {0};
    uint32_t unique_violators = 0;
    
    // Analyze events within the time window
    for (uint32_t i = 0; i < security_event_count && i < MAX_SECURITY_EVENTS; i++) {
        uint32_t idx = (security_event_next + MAX_SECURITY_EVENTS - i - 1) % MAX_SECURITY_EVENTS;
        security_event_t *event = &security_events[idx];
        
        if (event->timestamp >= window_start && 
            (event->event_type == SEC_EVENT_ACCESS_VIOLATION || 
             event->event_type == SEC_EVENT_PRIVILEGE_ESCALATION || 
             event->event_type == SEC_EVENT_UNAUTHORIZED_ACCESS) &&
            !event->success) {
            
            // Look for existing SID in our tracking
            int found = 0;
            for (uint32_t j = 0; j < unique_violators; j++) {
                if (violation_sids[j] == event->subject_sid) {
                    violation_counts[j]++;
                    found = 1;
                    break;
                }
            }
            
            // Add new SID if not found and we have space
            if (!found && unique_violators < 32) {
                violation_sids[unique_violators] = event->subject_sid;
                violation_counts[unique_violators] = 1;
                unique_violators++;
            }
        }
    }
    
    // Report on findings
    if (unique_violators > 0) {
        LOG_INFO("Security analysis: Found %d SIDs with security violations in the last hour", 
                 unique_violators);
        
        for (uint32_t i = 0; i < unique_violators; i++) {
            if (violation_counts[i] >= current_config.alert_threshold) {
                LOG_WARNING("Security analysis: SID:%d has %d violations - possible intrusion attempt", 
                           violation_sids[i], violation_counts[i]);
            }
        }
    } else {
        LOG_INFO("Security analysis: No security violations detected in the last hour");
    }
    
    mutex_unlock(&security_monitor_mutex);
}