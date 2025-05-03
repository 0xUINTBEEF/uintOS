/**
 * @file dns.c
 * @brief DNS client implementation for uintOS
 */

#include "../include/dns.h"
#include "../include/udp.h"
#include "../../kernel/logging/log.h"
#include "../../hal/include/hal_timer.h"
#include <string.h>

/* DNS query structure */
typedef struct dns_query {
    char hostname[DNS_MAX_NAME_LENGTH + 1];   // Host name to resolve
    uint16_t id;                              // Query ID
    uint32_t timestamp;                       // When the query was sent
    int retry_count;                          // Retry counter
    dns_callback_t callback;                  // User callback
    void* user_data;                          // User data for callback
    int active;                               // Whether this query is active
} dns_query_t;

/* Static variables */
static ipv4_address_t dns_server;             // DNS server to use
static dns_cache_entry_t dns_cache[DNS_MAX_CACHE_ENTRIES]; // DNS cache
static dns_query_t dns_queries[8];            // Active queries
static uint16_t next_query_id = 0;            // Next query ID to use

/* Forward declarations */
static int dns_send_query(const char* hostname, uint16_t id);
static int dns_parse_name(uint8_t* packet, size_t packet_len, size_t offset, char* name, size_t name_len, size_t* name_end_offset);
static int dns_find_active_query(uint16_t id);
static int dns_find_free_query_slot(void);
static int dns_cache_entry(const char* hostname, const ipv4_address_t* ip, uint32_t ttl);
static void dns_complete_query(int index, const ipv4_address_t* ip, int status);

/**
 * Initialize the DNS client subsystem
 */
int dns_init(void) {
    LOG(LOG_INFO, "Initializing DNS client");
    
    // Clear the cache
    memset(dns_cache, 0, sizeof(dns_cache));
    
    // Clear active queries
    memset(dns_queries, 0, sizeof(dns_queries));
    
    // TODO: Register UDP port for DNS client when UDP port registration is available
    
    return 0;
}

/**
 * Set the primary DNS server to use for lookups
 */
void dns_set_server(const ipv4_address_t* dns_server_ip) {
    if (dns_server_ip) {
        memcpy(&dns_server, dns_server_ip, sizeof(ipv4_address_t));
        
        char ip_str[16];
        LOG(LOG_INFO, "DNS server set to %s", ipv4_to_str(&dns_server, ip_str));
    }
}

/**
 * Get the current DNS server
 */
const ipv4_address_t* dns_get_server(void) {
    // Check if DNS server is set (non-zero)
    for (int i = 0; i < 4; i++) {
        if (dns_server.addr[i] != 0) {
            return &dns_server;
        }
    }
    return NULL;
}

/**
 * Generate the next query ID
 */
static uint16_t dns_get_next_id(void) {
    // Simple incrementing ID
    return next_query_id++;
}

/**
 * Perform a DNS lookup (asynchronous)
 */
int dns_lookup(const char* hostname, dns_callback_t callback, void* user_data) {
    if (!hostname || !callback) {
        return -1;
    }
    
    // Check if we have a DNS server configured
    if (!dns_get_server()) {
        LOG(LOG_ERROR, "No DNS server configured");
        return -1;
    }
    
    // Check cache first
    ipv4_address_t cached_ip;
    if (dns_get_cached(hostname, &cached_ip) == DNS_TTL_VALID) {
        // Call the callback immediately with the cached result
        callback(hostname, &cached_ip, user_data);
        return 0;
    }
    
    // Find a free query slot
    int slot = dns_find_free_query_slot();
    if (slot < 0) {
        LOG(LOG_ERROR, "No free DNS query slots");
        return -1;
    }
    
    // Prepare the query
    dns_query_t* query = &dns_queries[slot];
    strncpy(query->hostname, hostname, DNS_MAX_NAME_LENGTH);
    query->hostname[DNS_MAX_NAME_LENGTH] = '\0';
    query->id = dns_get_next_id();
    query->timestamp = hal_get_time_ms();
    query->retry_count = 0;
    query->callback = callback;
    query->user_data = user_data;
    query->active = 1;
    
    // Send the query
    LOG(LOG_INFO, "Starting DNS lookup for %s", hostname);
    return dns_send_query(hostname, query->id);
}

/**
 * Perform a DNS lookup (synchronous)
 */
int dns_lookup_sync(const char* hostname, ipv4_address_t* ip, uint32_t timeout_ms) {
    if (!hostname || !ip) {
        return -1;
    }
    
    // Check if we have a DNS server configured
    if (!dns_get_server()) {
        LOG(LOG_ERROR, "No DNS server configured");
        return -1;
    }
    
    // Check cache first
    if (dns_get_cached(hostname, ip) == DNS_TTL_VALID) {
        return 0;
    }
    
    // Find a free query slot
    int slot = dns_find_free_query_slot();
    if (slot < 0) {
        LOG(LOG_ERROR, "No free DNS query slots");
        return -1;
    }
    
    // Prepare the query
    dns_query_t* query = &dns_queries[slot];
    strncpy(query->hostname, hostname, DNS_MAX_NAME_LENGTH);
    query->hostname[DNS_MAX_NAME_LENGTH] = '\0';
    query->id = dns_get_next_id();
    query->timestamp = hal_get_time_ms();
    query->retry_count = 0;
    query->callback = NULL;
    query->user_data = NULL;
    query->active = 1;
    
    // Send the query
    LOG(LOG_INFO, "Starting synchronous DNS lookup for %s", hostname);
    int result = dns_send_query(hostname, query->id);
    if (result != 0) {
        query->active = 0;
        return result;
    }
    
    // Wait for response or timeout
    uint32_t start_time = hal_get_time_ms();
    while (1) {
        // Process any DNS tasks (like timeouts and retransmits)
        dns_client_task();
        
        // Check if our query is still active
        if (!query->active) {
            // Query completed - check if we have a cached result
            if (dns_get_cached(hostname, ip) == DNS_TTL_VALID) {
                return 0;
            }
            return -1;
        }
        
        // Check for timeout
        if (hal_get_time_ms() - start_time >= timeout_ms) {
            LOG(LOG_WARNING, "DNS lookup timeout for %s", hostname);
            query->active = 0;
            return -1;
        }
        
        // Sleep a bit to avoid hogging CPU
        // TODO: Replace with proper sleep function when available
        for (volatile int i = 0; i < 1000; i++);
    }
}

/**
 * Send a DNS query packet
 */
static int dns_send_query(const char* hostname, uint16_t id) {
    // Validate hostname
    if (!hostname || hostname[0] == '\0') {
        return -1;
    }
    
    // Allocate a buffer for the DNS packet
    net_buffer_t* buffer = net_buffer_alloc(DNS_MAX_PACKET_SIZE, 0);
    if (!buffer) {
        LOG(LOG_ERROR, "Failed to allocate buffer for DNS query");
        return -1;
    }
    
    uint8_t* packet = buffer->data;
    size_t offset = 0;
    
    // Fill in the DNS header
    dns_header_t* header = (dns_header_t*)packet;
    header->id = htons(id);
    header->flags = htons(DNS_FLAG_RD);  // Recursion desired
    header->qdcount = htons(1);          // One question
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;
    
    offset += sizeof(dns_header_t);
    
    // Encode the hostname in DNS format (length-prefixed labels)
    const char* label_start = hostname;
    while (*label_start) {
        const char* label_end = strchr(label_start, '.');
        size_t label_len;
        
        if (label_end) {
            label_len = label_end - label_start;
        } else {
            label_len = strlen(label_start);
        }
        
        // Make sure we have room for the label
        if (offset + 1 + label_len > DNS_MAX_PACKET_SIZE) {
            net_buffer_free(buffer);
            return -1;
        }
        
        // Write the length byte and then the label
        packet[offset++] = label_len;
        memcpy(packet + offset, label_start, label_len);
        offset += label_len;
        
        if (label_end) {
            label_start = label_end + 1;
        } else {
            break;
        }
    }
    
    // Terminate with a zero-length label
    packet[offset++] = 0;
    
    // Add the question fields (type and class)
    if (offset + sizeof(dns_question_t) > DNS_MAX_PACKET_SIZE) {
        net_buffer_free(buffer);
        return -1;
    }
    
    dns_question_t* question = (dns_question_t*)(packet + offset);
    question->type = htons(DNS_TYPE_A);     // A record (IPv4 address)
    question->class = htons(DNS_CLASS_IN);  // Internet class
    
    offset += sizeof(dns_question_t);
    
    // Set the buffer length
    buffer->len = offset;
    
    // Get the default network device
    net_device_t* dev = network_get_default_device();
    if (!dev) {
        LOG(LOG_ERROR, "No default network device for DNS query");
        net_buffer_free(buffer);
        return -1;
    }
    
    // Send the packet
    int result = udp_send(dev, &dns_server, DNS_PORT, DNS_PORT, buffer);
    
    net_buffer_free(buffer);
    return result;
}

/**
 * Process a received DNS packet
 */
int dns_process_packet(net_buffer_t* buffer) {
    if (!buffer || buffer->len < sizeof(dns_header_t)) {
        return -1;
    }
    
    uint8_t* packet = buffer->data;
    size_t packet_len = buffer->len;
    size_t offset = 0;
    
    // Parse the DNS header
    dns_header_t* header = (dns_header_t*)packet;
    uint16_t id = ntohs(header->id);
    uint16_t flags = ntohs(header->flags);
    uint16_t qdcount = ntohs(header->qdcount);
    uint16_t ancount = ntohs(header->ancount);
    uint16_t rcode = flags & DNS_FLAG_RCODE;
    
    // Check if this is a response
    if (!(flags & DNS_FLAG_QR)) {
        return -1;  // Not a response
    }
    
    // Find the corresponding query
    int query_index = dns_find_active_query(id);
    if (query_index < 0) {
        LOG(LOG_DEBUG, "Received DNS response with unknown ID %u", id);
        return -1;  // Unknown query ID
    }
    
    dns_query_t* query = &dns_queries[query_index];
    
    // Check response code
    if (rcode != DNS_RCODE_NOERROR) {
        LOG(LOG_WARNING, "DNS error response %u for %s", rcode, query->hostname);
        dns_complete_query(query_index, NULL, -rcode);
        return -1;
    }
    
    // Skip past the header
    offset = sizeof(dns_header_t);
    
    // Skip past the question section
    for (int i = 0; i < qdcount; i++) {
        // Skip name
        while (offset < packet_len) {
            uint8_t len = packet[offset++];
            if (len == 0) {
                break;  // End of name
            } else if (len & 0xC0) {
                // This is a pointer - skip one more byte
                offset++;
                break;
            } else {
                // Regular label - skip 'len' bytes
                offset += len;
            }
        }
        
        // Skip type and class
        offset += sizeof(dns_question_t);
    }
    
    // Process answer section
    ipv4_address_t resolved_ip;
    int found_ip = 0;
    
    for (int i = 0; i < ancount; i++) {
        // Parse the name
        char name[DNS_MAX_NAME_LENGTH + 1];
        size_t name_end;
        
        if (dns_parse_name(packet, packet_len, offset, name, sizeof(name), &name_end) < 0) {
            LOG(LOG_WARNING, "Malformed DNS response (name)");
            break;
        }
        
        offset = name_end;
        
        // Make sure we have enough bytes for the resource record
        if (offset + sizeof(dns_resource_t) > packet_len) {
            LOG(LOG_WARNING, "Malformed DNS response (rr header)");
            break;
        }
        
        // Parse the resource record
        dns_resource_t* rr = (dns_resource_t*)(packet + offset);
        uint16_t type = ntohs(rr->type);
        uint16_t rdlength = ntohs(rr->rdlength);
        uint32_t ttl = ntohl(rr->ttl);
        
        offset += sizeof(dns_resource_t);
        
        // Check if this is an A record
        if (type == DNS_TYPE_A && rdlength == 4) {
            // Make sure we have enough bytes for the IPv4 address
            if (offset + 4 > packet_len) {
                LOG(LOG_WARNING, "Malformed DNS response (A record data)");
                break;
            }
            
            // Copy the IPv4 address
            memcpy(&resolved_ip, packet + offset, 4);
            found_ip = 1;
            
            // Cache this result
            dns_cache_entry(query->hostname, &resolved_ip, ttl);
            
            char ip_str[16];
            LOG(LOG_INFO, "DNS resolved %s to %s", query->hostname, ipv4_to_str(&resolved_ip, ip_str));
            
            // Complete the query with success
            dns_complete_query(query_index, &resolved_ip, 0);
            return 0;
        }
        
        // Skip the resource data
        offset += rdlength;
    }
    
    if (!found_ip) {
        LOG(LOG_WARNING, "DNS response contained no A records for %s", query->hostname);
        dns_complete_query(query_index, NULL, -1);
        return -1;
    }
    
    return 0;
}

/**
 * Parse a DNS-encoded name
 */
static int dns_parse_name(uint8_t* packet, size_t packet_len, size_t offset, 
                        char* name, size_t name_len, size_t* name_end_offset) {
    size_t name_offset = 0;
    int pointer_followed = 0;
    size_t next_offset = offset;
    
    // Initialize name with empty string
    name[0] = '\0';
    
    while (offset < packet_len) {
        uint8_t len = packet[offset++];
        
        // Check for end of name
        if (len == 0) {
            break;
        }
        
        // Check for compression pointer
        if (len & 0xC0) {
            if (offset >= packet_len) {
                return -1;  // Packet too short
            }
            
            // This is a pointer (high 2 bits set)
            uint16_t pointer = ((len & 0x3F) << 8) | packet[offset++];
            
            // If this is the first pointer, save the next offset
            if (!pointer_followed) {
                next_offset = offset;
                pointer_followed = 1;
            }
            
            // Update offset to the pointer target
            offset = pointer;
            
            // Prevent infinite loop
            if (offset >= packet_len) {
                return -1;
            }
            
            continue;
        }
        
        // Regular label
        if (offset + len > packet_len) {
            return -1;  // Packet too short
        }
        
        // Check if we have room in the name buffer
        if (name_offset > 0) {
            // Add a dot separator
            if (name_offset + 1 >= name_len) {
                return -1;  // Name buffer too small
            }
            name[name_offset++] = '.';
        }
        
        // Make sure we have room for the label
        if (name_offset + len >= name_len) {
            return -1;  // Name buffer too small
        }
        
        // Copy the label
        memcpy(name + name_offset, packet + offset, len);
        name_offset += len;
        name[name_offset] = '\0';
        
        // Move to the next label
        offset += len;
    }
    
    // Set the end offset
    if (pointer_followed) {
        *name_end_offset = next_offset;
    } else {
        *name_end_offset = offset;
    }
    
    return 0;
}

/**
 * Find an active query by ID
 */
static int dns_find_active_query(uint16_t id) {
    for (int i = 0; i < sizeof(dns_queries) / sizeof(dns_queries[0]); i++) {
        if (dns_queries[i].active && dns_queries[i].id == id) {
            return i;
        }
    }
    return -1;
}

/**
 * Find a free query slot
 */
static int dns_find_free_query_slot(void) {
    for (int i = 0; i < sizeof(dns_queries) / sizeof(dns_queries[0]); i++) {
        if (!dns_queries[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * Complete a query with result
 */
static void dns_complete_query(int index, const ipv4_address_t* ip, int status) {
    if (index < 0 || index >= sizeof(dns_queries) / sizeof(dns_queries[0])) {
        return;
    }
    
    dns_query_t* query = &dns_queries[index];
    
    // If this query has a callback, invoke it
    if (query->callback) {
        query->callback(query->hostname, ip, query->user_data);
    }
    
    // Mark the query as inactive
    query->active = 0;
}

/**
 * Cache a DNS result
 */
static int dns_cache_entry(const char* hostname, const ipv4_address_t* ip, uint32_t ttl) {
    if (!hostname || !ip) {
        return -1;
    }
    
    // Find an existing entry or an empty slot
    int slot = -1;
    int oldest_slot = -1;
    uint32_t oldest_time = 0xFFFFFFFF;
    
    for (int i = 0; i < DNS_MAX_CACHE_ENTRIES; i++) {
        if (dns_cache[i].valid) {
            // Check if this is the hostname we're looking for
            if (strcmp(dns_cache[i].hostname, hostname) == 0) {
                slot = i;
                break;
            }
            
            // Track the oldest entry
            uint32_t entry_age = hal_get_time_ms() - dns_cache[i].timestamp;
            if (entry_age > oldest_time) {
                oldest_time = entry_age;
                oldest_slot = i;
            }
        } else if (slot < 0) {
            // Found an empty slot
            slot = i;
        }
    }
    
    // If no empty slots, use the oldest one
    if (slot < 0) {
        slot = oldest_slot;
    }
    
    // If still no slot, fail
    if (slot < 0) {
        return -1;
    }
    
    // Update the cache entry
    strncpy(dns_cache[slot].hostname, hostname, DNS_MAX_NAME_LENGTH);
    dns_cache[slot].hostname[DNS_MAX_NAME_LENGTH] = '\0';
    memcpy(&dns_cache[slot].ip, ip, sizeof(ipv4_address_t));
    dns_cache[slot].ttl = ttl;
    dns_cache[slot].timestamp = hal_get_time_ms();
    dns_cache[slot].valid = 1;
    
    return 0;
}

/**
 * Get a cached DNS entry
 */
int dns_get_cached(const char* hostname, ipv4_address_t* ip) {
    if (!hostname || !ip) {
        return -1;
    }
    
    for (int i = 0; i < DNS_MAX_CACHE_ENTRIES; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            // Found a match - check if it has expired
            uint32_t entry_age_sec = (hal_get_time_ms() - dns_cache[i].timestamp) / 1000;
            
            if (entry_age_sec < dns_cache[i].ttl) {
                // Entry is still valid
                memcpy(ip, &dns_cache[i].ip, sizeof(ipv4_address_t));
                return DNS_TTL_VALID;
            } else {
                // Entry has expired
                return DNS_TTL_EXPIRED;
            }
        }
    }
    
    // Not found
    return -1;
}

/**
 * Clear the DNS cache
 */
void dns_clear_cache(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
}

/**
 * DNS client task - handles timeouts and retries
 */
void dns_client_task(void) {
    uint32_t current_time = hal_get_time_ms();
    
    for (int i = 0; i < sizeof(dns_queries) / sizeof(dns_queries[0]); i++) {
        dns_query_t* query = &dns_queries[i];
        
        if (query->active) {
            uint32_t elapsed_ms = current_time - query->timestamp;
            
            // Check for timeout (3 seconds per attempt, max 3 attempts)
            if (elapsed_ms > 3000) {
                if (query->retry_count < 2) {
                    // Retry
                    LOG(LOG_INFO, "DNS query timeout for %s, retrying (%d)", query->hostname, query->retry_count + 1);
                    query->retry_count++;
                    query->timestamp = current_time;
                    dns_send_query(query->hostname, query->id);
                } else {
                    // Give up
                    LOG(LOG_WARNING, "DNS query failed for %s after %d attempts", query->hostname, query->retry_count + 1);
                    dns_complete_query(i, NULL, -1);
                }
            }
        }
    }
}