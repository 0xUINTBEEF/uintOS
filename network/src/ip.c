/**
 * @file ip.c
 * @brief IP protocol implementation for uintOS
 * 
 * This file implements the IP protocol handler for uintOS network stack.
 */

#include <string.h>
#include <stdio.h>
#include "../include/ip.h"
#include "../include/ethernet.h"
#include "../../memory/heap.h"
#include "../../kernel/logging/log.h"

// Protocol handlers for IP protocols
typedef int (*ip_protocol_handler_t)(net_buffer_t* buffer, const ip_addr_t* src, const ip_addr_t* dest);
static struct {
    uint8_t protocol;
    ip_protocol_handler_t handler;
} ip_protocol_handlers[8];
static int ip_protocol_count = 0;

// Local IP configuration
static ip_addr_t local_ip = {0};
static ip_addr_t subnet_mask = {0};
static ip_addr_t default_gateway = {0};

/**
 * Initialize the IP protocol handler
 */
int ip_init() {
    log_info("Initializing IP protocol handler");
    
    // Initialize protocol handlers
    ip_protocol_count = 0;
    
    // Set default IP configuration
    ip_str_to_addr("0.0.0.0", &local_ip);
    ip_str_to_addr("0.0.0.0", &subnet_mask);
    ip_str_to_addr("0.0.0.0", &default_gateway);
    
    log_info("IP protocol handler initialized");
    return 0;
}

/**
 * Register a protocol handler for an IP protocol
 */
int ip_register_protocol(uint8_t protocol, int (*handler)(net_buffer_t* buffer)) {
    if (handler == NULL) {
        log_error("Cannot register NULL protocol handler");
        return -1;
    }
    
    if (ip_protocol_count >= 8) {
        log_error("Maximum number of IP protocol handlers reached");
        return -1;
    }
    
    // Check for duplicate protocol handler
    for (int i = 0; i < ip_protocol_count; i++) {
        if (ip_protocol_handlers[i].protocol == protocol) {
            log_error("Protocol handler for IP protocol %u already registered", protocol);
            return -1;
        }
    }
    
    // Add protocol handler to the list
    ip_protocol_handlers[ip_protocol_count].protocol = protocol;
    ip_protocol_handlers[ip_protocol_count].handler = (ip_protocol_handler_t)handler;
    ip_protocol_count++;
    
    log_info("Registered IP protocol handler for protocol %u", protocol);
    return 0;
}

/**
 * Find a protocol handler for a given IP protocol
 */
static ip_protocol_handler_t ip_find_protocol_handler(uint8_t protocol) {
    for (int i = 0; i < ip_protocol_count; i++) {
        if (ip_protocol_handlers[i].protocol == protocol) {
            return ip_protocol_handlers[i].handler;
        }
    }
    
    return NULL;
}

/**
 * Calculate the IP header checksum
 */
static uint16_t ip_checksum(const void* data, size_t len) {
    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)data;
    
    // Sum up 16-bit words
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Add left-over byte, if any
    if (len > 0) {
        sum += *(uint8_t*)ptr;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/**
 * Process an incoming IP packet
 */
int ip_rx(net_buffer_t* buffer) {
    if (buffer == NULL) {
        log_error("Cannot process NULL buffer");
        return -1;
    }
    
    if (buffer->len < IP_HEADER_MIN_SIZE) {
        log_error("IP packet too short (%u bytes)", buffer->len);
        return -1;
    }
    
    // Get the IP header
    ip_header_t* ip_hdr = (ip_header_t*)buffer->data;
    
    // Check IP version
    if (IP_VERSION(ip_hdr) != 4) {
        log_error("Unsupported IP version: %u", IP_VERSION(ip_hdr));
        return -1;
    }
    
    // Get header length in bytes
    uint8_t ihl = IP_IHL(ip_hdr);
    if (ihl < 5) {
        log_error("Invalid IP header length: %u", ihl);
        return -1;
    }
    
    uint16_t hdr_len = ihl * 4;
    if (buffer->len < hdr_len) {
        log_error("IP packet too short for header (%u bytes, header %u bytes)", buffer->len, hdr_len);
        return -1;
    }
    
    // Verify checksum
    uint16_t checksum = ip_hdr->checksum;
    ip_hdr->checksum = 0;
    
    if (checksum != ip_checksum(ip_hdr, hdr_len)) {
        log_error("IP checksum verification failed");
        ip_hdr->checksum = checksum; // Restore original checksum
        return -1;
    }
    
    ip_hdr->checksum = checksum; // Restore original checksum
    
    // Get total length
    uint16_t total_len = ntohs(ip_hdr->total_length);
    if (total_len > buffer->len) {
        log_error("IP total length (%u) exceeds buffer length (%u)", total_len, buffer->len);
        return -1;
    }
    
    // Trim the buffer to the actual packet length
    if (total_len < buffer->len) {
        netbuf_trim(buffer, buffer->len - total_len);
    }
    
    // Check if packet is fragmented
    uint16_t flags_and_offset = ntohs(ip_hdr->flags_and_fragment_offset);
    uint16_t offset = flags_and_offset & IP_FRAGMENT_OFFSET_MASK;
    uint8_t flags = (flags_and_offset >> 13) & 0x07;
    
    if (offset != 0 || (flags & IP_FLAG_MORE_FRAGMENTS)) {
        log_error("IP fragmentation not supported");
        return -1;
    }
    
    // Extract source and destination addresses
    ip_addr_t src_addr, dest_addr;
    memcpy(&src_addr, &ip_hdr->src_addr, sizeof(ip_addr_t));
    memcpy(&dest_addr, &ip_hdr->dest_addr, sizeof(ip_addr_t));
    
    char src_str[16], dest_str[16];
    log_debug("Received IP packet from %s to %s, protocol %u, length %u",
              ip_addr_to_str(&src_addr, src_str),
              ip_addr_to_str(&dest_addr, dest_str),
              ip_hdr->protocol, total_len);
    
    // Check if the packet is addressed to us
    if (!ip_is_local_address(&dest_addr) && !ip_is_broadcast(&dest_addr)) {
        log_debug("Ignoring IP packet not addressed to us");
        return -1;
    }
    
    // Find a protocol handler for this IP protocol
    ip_protocol_handler_t handler = ip_find_protocol_handler(ip_hdr->protocol);
    if (handler == NULL) {
        log_debug("No handler for IP protocol %u", ip_hdr->protocol);
        return -1;
    }
    
    // Skip the IP header
    netbuf_pull(buffer, hdr_len);
    
    // Call the protocol handler
    return handler(buffer, &src_addr, &dest_addr);
}

/**
 * Send an IP packet
 */
int ip_tx(net_device_t* dev, net_buffer_t* buffer, const ip_addr_t* dest_addr, uint8_t protocol) {
    if (dev == NULL || buffer == NULL || dest_addr == NULL) {
        log_error("Invalid parameters for ip_tx");
        return -1;
    }
    
    // Reserve space for the IP header
    void* header = netbuf_push(buffer, IP_HEADER_MIN_SIZE);
    if (header == NULL) {
        log_error("Failed to prepend IP header");
        return -1;
    }
    
    // Fill in the IP header
    ip_header_t* ip_hdr = (ip_header_t*)header;
    ip_hdr->version_and_ihl = (4 << 4) | 5; // IPv4, header length 5 * 4 bytes
    ip_hdr->type_of_service = 0;
    ip_hdr->total_length = htons(buffer->len);
    ip_hdr->identification = 0; // Not used for now
    ip_hdr->flags_and_fragment_offset = htons(IP_FLAG_DONT_FRAGMENT << 13);
    ip_hdr->ttl = 64;
    ip_hdr->protocol = protocol;
    ip_hdr->checksum = 0; // Will be filled in later
    
    // Set source and destination addresses
    memcpy(&ip_hdr->src_addr, &local_ip, sizeof(ip_addr_t));
    memcpy(&ip_hdr->dest_addr, dest_addr, sizeof(ip_addr_t));
    
    // Calculate the checksum
    ip_hdr->checksum = ip_checksum(ip_hdr, IP_HEADER_MIN_SIZE);
    
    char src_str[16], dest_str[16];
    log_debug("Sending IP packet from %s to %s, protocol %u, length %u",
              ip_addr_to_str(&local_ip, src_str),
              ip_addr_to_str(dest_addr, dest_str),
              protocol, buffer->len);
    
    // Determine the next hop address
    ip_addr_t next_hop;
    if (ip_is_on_local_subnet(dest_addr)) {
        // If the destination is on the local subnet, send directly
        memcpy(&next_hop, dest_addr, sizeof(ip_addr_t));
    } else {
        // Otherwise, send to the default gateway
        memcpy(&next_hop, &default_gateway, sizeof(ip_addr_t));
        if (ip_is_zero_address(&next_hop)) {
            log_error("No route to host");
            return -1;
        }
    }
    
    // Get the MAC address for the next hop
    mac_address_t next_hop_mac;
    // For this simple implementation, we'll use a dummy MAC address
    // In a real implementation, this would involve ARP
    next_hop_mac.bytes[0] = 0x12;
    next_hop_mac.bytes[1] = 0x34;
    next_hop_mac.bytes[2] = 0x56;
    next_hop_mac.bytes[3] = 0x78;
    next_hop_mac.bytes[4] = 0x9A;
    next_hop_mac.bytes[5] = 0xBC;
    
    // Send the Ethernet frame
    return ethernet_tx(dev, buffer, &next_hop_mac, ETH_TYPE_IP);
}

/**
 * Create a new IP packet with space for payload
 */
net_buffer_t* ip_alloc_packet(size_t payload_size) {
    // Calculate the total size required for the IP packet
    size_t total_size = IP_HEADER_MIN_SIZE + payload_size;
    
    // Allocate an Ethernet frame with space for the IP packet
    net_buffer_t* buffer = ethernet_alloc_frame(total_size);
    if (buffer == NULL) {
        return NULL;
    }
    
    // Reserve space for the IP header
    netbuf_reserve(buffer, IP_HEADER_MIN_SIZE);
    
    return buffer;
}

/**
 * Format an IP address as a string
 */
char* ip_addr_to_str(const ip_addr_t* addr, char* buffer) {
    if (addr == NULL || buffer == NULL) {
        return NULL;
    }
    
    sprintf(buffer, "%u.%u.%u.%u", addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3]);
    
    return buffer;
}

/**
 * Parse an IP address from a string
 */
int ip_str_to_addr(const char* str, ip_addr_t* addr) {
    if (str == NULL || addr == NULL) {
        return -1;
    }
    
    unsigned int values[4];
    int matched = sscanf(str, "%u.%u.%u.%u", &values[0], &values[1], &values[2], &values[3]);
    
    if (matched != 4) {
        return -1;
    }
    
    for (int i = 0; i < 4; i++) {
        if (values[i] > 255) {
            return -1;
        }
        addr->bytes[i] = (uint8_t)values[i];
    }
    
    return 0;
}

/**
 * Check if an IP address is a broadcast address
 */
int ip_is_broadcast(const ip_addr_t* addr) {
    if (addr == NULL) {
        return 0;
    }
    
    // Check for limited broadcast address (255.255.255.255)
    if (addr->bytes[0] == 0xFF && addr->bytes[1] == 0xFF &&
        addr->bytes[2] == 0xFF && addr->bytes[3] == 0xFF) {
        return 1;
    }
    
    // Check for subnet broadcast (ip | ~netmask)
    ip_addr_t subnet_broadcast;
    for (int i = 0; i < 4; i++) {
        subnet_broadcast.bytes[i] = local_ip.bytes[i] | ~subnet_mask.bytes[i];
    }
    
    return ip_addr_cmp(addr, &subnet_broadcast) == 0;
}

/**
 * Check if an IP address is a multicast address
 */
int ip_is_multicast(const ip_addr_t* addr) {
    if (addr == NULL) {
        return 0;
    }
    
    // Multicast addresses are in the range 224.0.0.0 to 239.255.255.255
    return (addr->bytes[0] >= 224) && (addr->bytes[0] <= 239);
}

/**
 * Check if an IP address is zero (0.0.0.0)
 */
int ip_is_zero_address(const ip_addr_t* addr) {
    if (addr == NULL) {
        return 0;
    }
    
    return (addr->bytes[0] == 0 && addr->bytes[1] == 0 &&
            addr->bytes[2] == 0 && addr->bytes[3] == 0);
}

/**
 * Compare two IP addresses
 */
int ip_addr_cmp(const ip_addr_t* addr1, const ip_addr_t* addr2) {
    if (addr1 == NULL || addr2 == NULL) {
        return addr1 == addr2 ? 0 : -1;
    }
    
    return memcmp(addr1->bytes, addr2->bytes, IP_ADDR_LENGTH);
}

/**
 * Copy an IP address
 */
void ip_addr_copy(ip_addr_t* dst, const ip_addr_t* src) {
    if (dst == NULL || src == NULL) {
        return;
    }
    
    memcpy(dst->bytes, src->bytes, IP_ADDR_LENGTH);
}

/**
 * Check if an IP address is on the local subnet
 */
int ip_is_on_local_subnet(const ip_addr_t* addr) {
    if (addr == NULL) {
        return 0;
    }
    
    // If no subnet mask is configured, assume everything is local
    if (ip_is_zero_address(&subnet_mask)) {
        return 1;
    }
    
    // Check if (addr & mask) == (local_ip & mask)
    for (int i = 0; i < 4; i++) {
        if ((addr->bytes[i] & subnet_mask.bytes[i]) != (local_ip.bytes[i] & subnet_mask.bytes[i])) {
            return 0;
        }
    }
    
    return 1;
}

/**
 * Check if an IP address is a local address (assigned to this host)
 */
int ip_is_local_address(const ip_addr_t* addr) {
    if (addr == NULL) {
        return 0;
    }
    
    // Check if it's our configured IP address
    if (ip_addr_cmp(addr, &local_ip) == 0) {
        return 1;
    }
    
    // Check if it's a loopback address (127.x.x.x)
    if (addr->bytes[0] == 127) {
        return 1;
    }
    
    return 0;
}

/**
 * Configure the local IP settings
 */
void ip_configure(const ip_addr_t* ip, const ip_addr_t* mask, const ip_addr_t* gateway) {
    if (ip != NULL) {
        ip_addr_copy(&local_ip, ip);
    }
    
    if (mask != NULL) {
        ip_addr_copy(&subnet_mask, mask);
    }
    
    if (gateway != NULL) {
        ip_addr_copy(&default_gateway, gateway);
    }
    
    char ip_str[16], mask_str[16], gateway_str[16];
    log_info("IP configured: address %s, mask %s, gateway %s",
             ip_addr_to_str(&local_ip, ip_str),
             ip_addr_to_str(&subnet_mask, mask_str),
             ip_addr_to_str(&default_gateway, gateway_str));
}

/**
 * Get the current IP configuration
 */
void ip_get_config(ip_addr_t* ip, ip_addr_t* mask, ip_addr_t* gateway) {
    if (ip != NULL) {
        ip_addr_copy(ip, &local_ip);
    }
    
    if (mask != NULL) {
        ip_addr_copy(mask, &subnet_mask);
    }
    
    if (gateway != NULL) {
        ip_addr_copy(gateway, &default_gateway);
    }
}