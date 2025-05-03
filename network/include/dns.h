/**
 * @file dns.h
 * @brief DNS client implementation for uintOS
 * 
 * This file defines the structures and functions for DNS resolution,
 * allowing hostname to IP address lookup.
 */

#ifndef DNS_H
#define DNS_H

#include "network.h"
#include "udp.h"

/* DNS constants */
#define DNS_PORT 53
#define DNS_MAX_NAME_LENGTH 255
#define DNS_MAX_PACKET_SIZE 512
#define DNS_MAX_CACHE_ENTRIES 32
#define DNS_DEFAULT_TTL 3600  // 1 hour default TTL

/* DNS types */
#define DNS_TYPE_A     1   // IPv4 address record
#define DNS_TYPE_NS    2   // Name server record
#define DNS_TYPE_CNAME 5   // Canonical name record
#define DNS_TYPE_PTR   12  // Pointer record
#define DNS_TYPE_MX    15  // Mail exchange record
#define DNS_TYPE_TXT   16  // Text record
#define DNS_TYPE_AAAA  28  // IPv6 address record

/* DNS classes */
#define DNS_CLASS_IN   1   // Internet

/* DNS header flags */
#define DNS_FLAG_QR    0x8000  // Query/Response
#define DNS_FLAG_AA    0x0400  // Authoritative Answer
#define DNS_FLAG_TC    0x0200  // Truncated
#define DNS_FLAG_RD    0x0100  // Recursion Desired
#define DNS_FLAG_RA    0x0080  // Recursion Available
#define DNS_FLAG_Z     0x0070  // Reserved
#define DNS_FLAG_AD    0x0020  // Authentic Data (DNSSEC)
#define DNS_FLAG_CD    0x0010  // Checking Disabled (DNSSEC)
#define DNS_FLAG_RCODE 0x000F  // Response Code

/* DNS response codes */
#define DNS_RCODE_NOERROR   0  // No error
#define DNS_RCODE_FORMERR   1  // Format error
#define DNS_RCODE_SERVFAIL  2  // Server failure
#define DNS_RCODE_NXDOMAIN  3  // Non-existent domain
#define DNS_RCODE_NOTIMP    4  // Not implemented
#define DNS_RCODE_REFUSED   5  // Query refused

/* DNS record time-to-live status */
#define DNS_TTL_EXPIRED     0  // Record has expired
#define DNS_TTL_VALID       1  // Record is valid

/* DNS header structure */
typedef struct dns_header {
    uint16_t id;        // Identification
    uint16_t flags;     // Flags
    uint16_t qdcount;   // Question count
    uint16_t ancount;   // Answer count
    uint16_t nscount;   // Authority count
    uint16_t arcount;   // Additional count
} __attribute__((packed)) dns_header_t;

/* DNS question structure (fixed part) */
typedef struct dns_question {
    // Variable-length name field precedes this structure in the packet
    uint16_t type;      // Question type
    uint16_t class;     // Question class
} __attribute__((packed)) dns_question_t;

/* DNS resource record structure (fixed part) */
typedef struct dns_resource {
    // Variable-length name field precedes this structure in the packet
    uint16_t type;      // Resource type
    uint16_t class;     // Resource class
    uint32_t ttl;       // Time to live
    uint16_t rdlength;  // Length of rdata
    // Variable-length rdata field follows this structure in the packet
} __attribute__((packed)) dns_resource_t;

/* DNS cache entry */
typedef struct dns_cache_entry {
    char hostname[DNS_MAX_NAME_LENGTH + 1];  // Host name
    ipv4_address_t ip;                       // IPv4 address
    uint32_t ttl;                            // Time to live (seconds)
    uint32_t timestamp;                      // When this entry was cached
    int valid;                               // Whether this entry is valid
} dns_cache_entry_t;

/* DNS lookup callback function type */
typedef void (*dns_callback_t)(const char* hostname, const ipv4_address_t* ip, void* user_data);

/**
 * Initialize the DNS client subsystem
 * 
 * @return 0 on success, error code on failure
 */
int dns_init(void);

/**
 * Set the primary DNS server to use for lookups
 * 
 * @param dns_server DNS server IP address
 */
void dns_set_server(const ipv4_address_t* dns_server);

/**
 * Get the current DNS server
 * 
 * @return Pointer to the current DNS server IP or NULL if none set
 */
const ipv4_address_t* dns_get_server(void);

/**
 * Perform a DNS lookup (asynchronous)
 * 
 * @param hostname Host name to resolve
 * @param callback Callback function to invoke when lookup completes
 * @param user_data User data to pass to the callback
 * @return 0 on success (lookup started), error code on failure
 */
int dns_lookup(const char* hostname, dns_callback_t callback, void* user_data);

/**
 * Perform a DNS lookup (synchronous)
 * 
 * @param hostname Host name to resolve
 * @param ip Pointer to store the resolved IP address
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, error code on failure
 */
int dns_lookup_sync(const char* hostname, ipv4_address_t* ip, uint32_t timeout_ms);

/**
 * Process a received DNS packet
 * 
 * @param buffer Network buffer containing the DNS packet
 * @return 0 on success, error code on failure
 */
int dns_process_packet(net_buffer_t* buffer);

/**
 * DNS client task - handles timeouts and retries
 * Called periodically by the kernel
 */
void dns_client_task(void);

/**
 * Clear the DNS cache
 */
void dns_clear_cache(void);

/**
 * Get a cached DNS entry
 * 
 * @param hostname Host name to look up
 * @param ip Pointer to store the IP address
 * @return DNS_TTL_VALID if found and valid, DNS_TTL_EXPIRED if expired, negative error code if not found
 */
int dns_get_cached(const char* hostname, ipv4_address_t* ip);

#endif /* DNS_H */