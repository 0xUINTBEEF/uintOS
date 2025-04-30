/**
 * @file ip.h
 * @brief IPv4 protocol implementation for uintOS
 * 
 * This file defines the IPv4 protocol handler for uintOS network stack.
 */

#ifndef IP_H
#define IP_H

#include "network.h"

/**
 * IP protocol version 4
 */
#define IP_VERSION_4 4

/**
 * IPv4 header size in bytes (without options)
 */
#define IP_HEADER_SIZE 20

/**
 * IP protocols
 */
#define IP_PROTO_ICMP  1    // Internet Control Message Protocol
#define IP_PROTO_TCP   6    // Transmission Control Protocol
#define IP_PROTO_UDP  17    // User Datagram Protocol

/**
 * IP flags
 */
#define IP_FLAG_RESERVED 0x8000  // Reserved (must be 0)
#define IP_FLAG_DF       0x4000  // Don't Fragment
#define IP_FLAG_MF       0x2000  // More Fragments

/**
 * IPv4 header structure
 */
typedef struct ip_header {
    uint8_t  ver_ihl;          // Version (4 bits) + Internet header length (4 bits)
    uint8_t  tos;              // Type of service
    uint16_t length;           // Total length
    uint16_t id;               // Identification
    uint16_t flags_offset;     // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t  ttl;              // Time to live
    uint8_t  protocol;         // Protocol
    uint16_t checksum;         // Header checksum
    ipv4_address_t src_addr;   // Source address
    ipv4_address_t dst_addr;   // Destination address
    // Options may follow
} __attribute__((packed)) ip_header_t;

/**
 * Special IPv4 addresses
 */
extern const ipv4_address_t IP_ADDR_ANY;       // 0.0.0.0
extern const ipv4_address_t IP_ADDR_BROADCAST; // 255.255.255.255
extern const ipv4_address_t IP_ADDR_LOOPBACK;  // 127.0.0.1

/**
 * Initialize the IP protocol handler
 * 
 * @return 0 on success, error code on failure
 */
int ip_init();

/**
 * Process an incoming IP packet
 * 
 * @param buffer Network buffer containing the packet
 * @return 0 on success, error code on failure
 */
int ip_rx(net_buffer_t* buffer);

/**
 * Send an IP packet
 * 
 * @param buffer Network buffer containing the packet to send
 * @param src Source IP address (NULL for automatic selection)
 * @param dst Destination IP address
 * @param protocol Protocol number (e.g., IP_PROTO_TCP)
 * @param ttl Time to live value
 * @return 0 on success, error code on failure
 */
int ip_tx(net_buffer_t* buffer, const ipv4_address_t* src, 
          const ipv4_address_t* dst, uint8_t protocol, uint8_t ttl);

/**
 * Create a new IP packet with space for payload
 * 
 * @param payload_size Size of the payload
 * @return Pointer to the new buffer or NULL on failure
 */
net_buffer_t* ip_alloc_packet(size_t payload_size);

/**
 * Calculate the IP header checksum
 * 
 * @param header Pointer to the IP header
 * @return The calculated checksum
 */
uint16_t ip_checksum(const ip_header_t* header);

/**
 * Get the version field from an IP header
 * 
 * @param header Pointer to the IP header
 * @return IP version number
 */
static inline uint8_t ip_get_version(const ip_header_t* header) {
    return (header->ver_ihl >> 4) & 0x0F;
}

/**
 * Get the IHL (Internet Header Length) field from an IP header
 * 
 * @param header Pointer to the IP header
 * @return IHL value in 4-byte units
 */
static inline uint8_t ip_get_ihl(const ip_header_t* header) {
    return header->ver_ihl & 0x0F;
}

/**
 * Get the header length in bytes from an IP header
 * 
 * @param header Pointer to the IP header
 * @return Header length in bytes
 */
static inline uint16_t ip_get_header_length(const ip_header_t* header) {
    return ip_get_ihl(header) * 4;
}

/**
 * Check if two IP addresses are equal
 * 
 * @param a First IP address
 * @param b Second IP address
 * @return 1 if equal, 0 otherwise
 */
int ip_addr_equal(const ipv4_address_t* a, const ipv4_address_t* b);

/**
 * Check if an IP address is in a subnet
 * 
 * @param addr IP address to check
 * @param net Network address
 * @param mask Subnet mask
 * @return 1 if in subnet, 0 otherwise
 */
int ip_addr_in_subnet(const ipv4_address_t* addr, 
                     const ipv4_address_t* net, 
                     const ipv4_address_t* mask);

/**
 * Find the appropriate network device for a destination IP
 * 
 * @param dst_ip Destination IP address
 * @return Pointer to the device or NULL if not found
 */
net_device_t* ip_find_route(const ipv4_address_t* dst_ip);

/**
 * Set the source and destination addresses in an IP header
 * 
 * @param header Pointer to the IP header
 * @param src Source IP address
 * @param dst Destination IP address
 */
void ip_set_addresses(ip_header_t* header, 
                     const ipv4_address_t* src, 
                     const ipv4_address_t* dst);

/**
 * Register a protocol handler for IP packets
 * 
 * @param protocol Protocol number
 * @param handler Function to handle packets of this protocol
 * @return 0 on success, error code on failure
 */
int ip_register_protocol(uint8_t protocol, int (*handler)(net_buffer_t*));

/**
 * Get a string representation of an IP protocol
 * 
 * @param protocol Protocol number
 * @return String representation or "UNKNOWN" if not recognized
 */
const char* ip_protocol_to_str(uint8_t protocol);

/**
 * Enable or disable IP forwarding
 * 
 * @param enable 1 to enable, 0 to disable
 * @return Previous state
 */
int ip_set_forwarding(int enable);

/**
 * Get current IP forwarding status
 * 
 * @return 1 if enabled, 0 if disabled
 */
int ip_get_forwarding();

#endif /* IP_H */