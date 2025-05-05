/**
 * @file ip.h
 * @brief IPv4 protocol implementation for uintOS
 * 
 * This file defines the IPv4 protocol handler for uintOS network stack.
 */

#ifndef IP_H
#define IP_H

#include <stdint.h>
#include <stddef.h>
#include "network.h"

/**
 * IP address length in bytes
 */
#define IP_ADDR_LENGTH 4

/**
 * IP address structure (IPv4)
 */
typedef struct ip_addr {
    uint8_t bytes[IP_ADDR_LENGTH];
} __attribute__((packed)) ip_addr_t;

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
 * IP header constants
 */
#define IP_HEADER_MIN_SIZE 20
#define IP_VERSION(hdr)    (((hdr)->version_and_ihl >> 4) & 0x0F)
#define IP_IHL(hdr)        ((hdr)->version_and_ihl & 0x0F)

/**
 * IP flags
 */
#define IP_FLAG_RESERVED        0x0
#define IP_FLAG_DONT_FRAGMENT   0x2
#define IP_FLAG_MORE_FRAGMENTS  0x4

/**
 * IP fragment offset mask
 */
#define IP_FRAGMENT_OFFSET_MASK 0x1FFF

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
 * Register a protocol handler for an IP protocol
 * 
 * @param protocol The IP protocol number
 * @param handler The handler function
 * @return 0 on success, error code on failure
 */
int ip_register_protocol(uint8_t protocol, int (*handler)(net_buffer_t* buffer));

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
 * @param dev Network device to send on
 * @param buffer Network buffer containing the payload
 * @param dest_addr Destination IP address
 * @param protocol IP protocol number
 * @return 0 on success, error code on failure
 */
int ip_tx(net_device_t* dev, net_buffer_t* buffer, const ip_addr_t* dest_addr, uint8_t protocol);

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

/**
 * Format an IP address as a string
 * 
 * @param addr IP address
 * @param buffer Output buffer (must be at least 16 bytes)
 * @return Pointer to the buffer or NULL on failure
 */
char* ip_addr_to_str(const ip_addr_t* addr, char* buffer);

/**
 * Parse an IP address from a string
 * 
 * @param str String containing the IP address
 * @param addr Output IP address
 * @return 0 on success, error code on failure
 */
int ip_str_to_addr(const char* str, ip_addr_t* addr);

/**
 * Check if an IP address is a broadcast address
 * 
 * @param addr IP address
 * @return 1 if broadcast, 0 otherwise
 */
int ip_is_broadcast(const ip_addr_t* addr);

/**
 * Check if an IP address is a multicast address
 * 
 * @param addr IP address
 * @return 1 if multicast, 0 otherwise
 */
int ip_is_multicast(const ip_addr_t* addr);

/**
 * Check if an IP address is zero (0.0.0.0)
 * 
 * @param addr IP address
 * @return 1 if zero, 0 otherwise
 */
int ip_is_zero_address(const ip_addr_t* addr);

/**
 * Compare two IP addresses
 * 
 * @param addr1 First IP address
 * @param addr2 Second IP address
 * @return 0 if equal, non-zero otherwise
 */
int ip_addr_cmp(const ip_addr_t* addr1, const ip_addr_t* addr2);

/**
 * Copy an IP address
 * 
 * @param dst Destination IP address
 * @param src Source IP address
 */
void ip_addr_copy(ip_addr_t* dst, const ip_addr_t* src);

/**
 * Check if an IP address is on the local subnet
 * 
 * @param addr IP address
 * @return 1 if on local subnet, 0 otherwise
 */
int ip_is_on_local_subnet(const ip_addr_t* addr);

/**
 * Check if an IP address is a local address (assigned to this host)
 * 
 * @param addr IP address
 * @return 1 if local, 0 otherwise
 */
int ip_is_local_address(const ip_addr_t* addr);

/**
 * Configure the local IP settings
 * 
 * @param ip Local IP address
 * @param mask Subnet mask
 * @param gateway Default gateway
 */
void ip_configure(const ip_addr_t* ip, const ip_addr_t* mask, const ip_addr_t* gateway);

/**
 * Get the current IP configuration
 * 
 * @param ip Output for local IP address
 * @param mask Output for subnet mask
 * @param gateway Output for default gateway
 */
void ip_get_config(ip_addr_t* ip, ip_addr_t* mask, ip_addr_t* gateway);

/**
 * Handle incoming IPv4 packet from the network stack
 * 
 * @param buffer Network buffer containing the IP packet
 * @return 0 on success, error code on failure
 */
int ip_receive(net_buffer_t* buffer);

/**
 * Send an IPv4 packet to the network
 * 
 * @param buffer Network buffer with the payload
 * @param dest_ip Destination IP address
 * @param protocol IP protocol number
 * @return 0 on success, error code on failure
 */
int ip_send(net_buffer_t* buffer, const ipv4_address_t* dest_ip, uint8_t protocol);

/**
 * Get IP address for a network interface
 * 
 * @param interface Interface name
 * @param addr Output IP address
 * @return 0 on success, error code on failure
 */
int ip_get_address(const char* interface, ipv4_address_t* addr);

/**
 * Set IP address for a network interface
 * 
 * @param interface Interface name
 * @param addr IP address
 * @return 0 on success, error code on failure
 */
int ip_set_address(const char* interface, const ipv4_address_t* addr);

/**
 * Set netmask for a network interface
 * 
 * @param interface Interface name
 * @param netmask Subnet mask
 * @return 0 on success, error code on failure
 */
int ip_set_netmask(const char* interface, const ipv4_address_t* netmask);

/**
 * Set default gateway for a network interface
 * 
 * @param interface Interface name
 * @param gateway Default gateway IP address
 * @return 0 on success, error code on failure
 */
int ip_set_gateway(const char* interface, const ipv4_address_t* gateway);

#endif /* IP_H */