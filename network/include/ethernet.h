/**
 * @file ethernet.h
 * @brief Ethernet protocol implementation for uintOS
 * 
 * This file defines the Ethernet protocol handler for uintOS network stack.
 */

#ifndef ETHERNET_H
#define ETHERNET_H

#include "network.h"

/**
 * Ethernet frame types
 */
#define ETH_TYPE_IP    0x0800  // Internet Protocol v4
#define ETH_TYPE_ARP   0x0806  // Address Resolution Protocol
#define ETH_TYPE_IPV6  0x86DD  // Internet Protocol v6
#define ETH_TYPE_VLAN  0x8100  // VLAN-tagged frame

/**
 * Standard Ethernet header size in bytes
 */
#define ETH_HEADER_SIZE 14

/**
 * Maximum size of an Ethernet frame (without FCS)
 */
#define ETH_FRAME_MAX_SIZE 1518

/**
 * Minimum size of an Ethernet frame (without FCS)
 */
#define ETH_FRAME_MIN_SIZE 60

/**
 * Ethernet frame header structure
 */
typedef struct eth_header {
    mac_address_t dest;        // Destination MAC address
    mac_address_t src;         // Source MAC address
    uint16_t ethertype;        // EtherType field
} __attribute__((packed)) eth_header_t;

/**
 * Initialize the Ethernet protocol handler
 * 
 * @return 0 on success, error code on failure
 */
int ethernet_init();

/**
 * Process an incoming Ethernet frame
 * 
 * @param buffer Network buffer containing the frame
 * @return 0 on success, error code on failure
 */
int ethernet_rx(net_buffer_t* buffer);

/**
 * Send an Ethernet frame
 * 
 * @param dev Network device to send on
 * @param buffer Network buffer containing the frame to send
 * @param dest Destination MAC address
 * @param ethertype Protocol type (e.g., ETH_TYPE_IP)
 * @return 0 on success, error code on failure
 */
int ethernet_tx(net_device_t* dev, net_buffer_t* buffer, const mac_address_t* dest, uint16_t ethertype);

/**
 * Create a new Ethernet frame with space for payload
 * 
 * @param payload_size Size of the payload
 * @return Pointer to the new buffer or NULL on failure
 */
net_buffer_t* ethernet_alloc_frame(size_t payload_size);

/**
 * Format a MAC address as a string
 * 
 * @param mac MAC address to format
 * @param buffer Buffer to store the formatted string (needs at least 18 bytes)
 * @return Pointer to the buffer
 */
char* ethernet_mac_to_str(const mac_address_t* mac, char* buffer);

/**
 * Parse a MAC address from a string
 * 
 * @param str String containing the MAC address in format "xx:xx:xx:xx:xx:xx"
 * @param mac Pointer to store the parsed MAC address
 * @return 0 on success, error code on failure
 */
int ethernet_str_to_mac(const char* str, mac_address_t* mac);

/**
 * Check if a MAC address is a broadcast address
 * 
 * @param mac MAC address to check
 * @return 1 if broadcast, 0 otherwise
 */
int ethernet_is_broadcast(const mac_address_t* mac);

/**
 * Check if a MAC address is a multicast address
 * 
 * @param mac MAC address to check
 * @return 1 if multicast, 0 otherwise
 */
int ethernet_is_multicast(const mac_address_t* mac);

/**
 * Compare two MAC addresses
 * 
 * @param mac1 First MAC address
 * @param mac2 Second MAC address
 * @return 0 if equal, non-zero otherwise
 */
int ethernet_mac_cmp(const mac_address_t* mac1, const mac_address_t* mac2);

/**
 * Copy a MAC address
 * 
 * @param dst Destination MAC address
 * @param src Source MAC address
 */
void ethernet_mac_copy(mac_address_t* dst, const mac_address_t* src);

#endif /* ETHERNET_H */