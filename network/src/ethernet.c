/**
 * @file ethernet.c
 * @brief Ethernet protocol implementation for uintOS
 */

#include "../include/ethernet.h"
#include "../include/ip.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include <string.h>
#include <stdio.h>

// Broadcast MAC address (FF:FF:FF:FF:FF:FF)
static const mac_address_t broadcast_mac = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

// Function prototypes for supported protocols
typedef int (*eth_protocol_handler_t)(net_buffer_t* buffer);
static int ethernet_handle_ip(net_buffer_t* buffer);
static int ethernet_handle_arp(net_buffer_t* buffer);

// Protocol handlers table
static eth_protocol_handler_t protocol_handlers[] = {
    [ETH_TYPE_IP >> 8]  = ethernet_handle_ip,
    [ETH_TYPE_ARP >> 8] = ethernet_handle_arp
};

/**
 * Initialize the Ethernet protocol handler
 */
int ethernet_init() {
    log_info("Initializing Ethernet protocol handler");
    // Nothing special to do here for now
    return 0;
}

/**
 * Process an incoming Ethernet frame
 */
int ethernet_rx(net_buffer_t* buffer) {
    if (!buffer || buffer->len < ETH_HEADER_SIZE) {
        return NET_ERR_INVALID;
    }
    
    // Get the Ethernet header
    eth_header_t* eth = (eth_header_t*)buffer->data;
    
    // Convert EtherType to host byte order
    uint16_t ethertype = (eth->ethertype >> 8) | ((eth->ethertype & 0xff) << 8);
    
    // Check if we support this protocol
    uint8_t proto_index = ethertype >> 8;
    if (proto_index >= sizeof(protocol_handlers)/sizeof(protocol_handlers[0]) || 
        !protocol_handlers[proto_index]) {
        log_debug("Unsupported EtherType: 0x%04x", ethertype);
        return NET_ERR_NOPROTO;
    }
    
    // Set the buffer protocol
    buffer->protocol = NET_PROTO_ETH;
    
    // Skip the Ethernet header
    net_buffer_pull(buffer, ETH_HEADER_SIZE);
    
    // Set flags for broadcast/multicast
    if (ethernet_is_broadcast(&eth->dest)) {
        buffer->flags |= NET_BUF_FLAG_BROADCAST;
    } else if (ethernet_is_multicast(&eth->dest)) {
        buffer->flags |= NET_BUF_FLAG_MULTICAST;
    }
    
    // Call the appropriate handler based on EtherType
    return protocol_handlers[proto_index](buffer);
}

/**
 * Send an Ethernet frame
 */
int ethernet_tx(net_device_t* dev, net_buffer_t* buffer, const mac_address_t* dest, uint16_t ethertype) {
    if (!dev || !buffer || !dest) {
        return NET_ERR_INVALID;
    }
    
    // Check if the device is up
    if (!(dev->flags & NET_DEV_FLAG_UP)) {
        log_error("Cannot send on down device: %s", dev->name);
        return NET_ERR_BUSY;
    }
    
    // Make sure we have enough headroom for the Ethernet header
    if (buffer->offset < ETH_HEADER_SIZE) {
        log_error("Not enough headroom for Ethernet header");
        return NET_ERR_NOMEM;
    }
    
    // Add the Ethernet header
    eth_header_t* eth = (eth_header_t*)net_buffer_push(buffer, ETH_HEADER_SIZE);
    if (!eth) {
        log_error("Failed to add Ethernet header to buffer");
        return NET_ERR_NOMEM;
    }
    
    // Fill the header
    ethernet_mac_copy(&eth->dest, dest);
    ethernet_mac_copy(&eth->src, &dev->mac);
    
    // Set EtherType (in network byte order)
    eth->ethertype = (ethertype >> 8) | ((ethertype & 0xff) << 8);
    
    // Associate the buffer with this device
    buffer->device = dev;
    
    // Update statistics
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += buffer->len;
    
    // Send the frame
    if (dev->ops.transmit) {
        int result = dev->ops.transmit(dev, buffer);
        if (result != 0) {
            dev->stats.tx_errors++;
        }
        return result;
    }
    
    log_error("Device %s does not support transmit operation", dev->name);
    return NET_ERR_INVALID;
}

/**
 * Create a new Ethernet frame with space for payload
 */
net_buffer_t* ethernet_alloc_frame(size_t payload_size) {
    // Allocate a buffer with enough room for Ethernet header + payload
    net_buffer_t* buffer = net_buffer_alloc(ETH_HEADER_SIZE + payload_size, ETH_HEADER_SIZE);
    
    if (!buffer) {
        log_error("Failed to allocate Ethernet frame buffer");
        return NULL;
    }
    
    return buffer;
}

/**
 * Handle IP packets received via Ethernet
 */
static int ethernet_handle_ip(net_buffer_t* buffer) {
    // Update protocol
    buffer->protocol = NET_PROTO_IP;
    
    // Forward to IP layer
    return ip_rx(buffer);
}

/**
 * Handle ARP packets received via Ethernet
 */
static int ethernet_handle_arp(net_buffer_t* buffer) {
    // Update protocol
    buffer->protocol = NET_PROTO_ARP;
    
    // This would call the ARP handler, but it's not implemented yet
    log_info("ARP packet received, but handling not implemented");
    
    return 0;
}

/**
 * Format a MAC address as a string
 */
char* ethernet_mac_to_str(const mac_address_t* mac, char* buffer) {
    if (!mac || !buffer) {
        return NULL;
    }
    
    sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac->addr[0], mac->addr[1], mac->addr[2],
            mac->addr[3], mac->addr[4], mac->addr[5]);
    
    return buffer;
}

/**
 * Parse a MAC address from a string
 */
int ethernet_str_to_mac(const char* str, mac_address_t* mac) {
    if (!str || !mac) {
        return NET_ERR_INVALID;
    }
    
    // Try to parse the MAC address
    unsigned int a[6];
    
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", 
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
        // Try with dash separator
        if (sscanf(str, "%x-%x-%x-%x-%x-%x", 
                   &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
            return NET_ERR_INVALID;
        }
    }
    
    // Validate each byte
    for (int i = 0; i < 6; i++) {
        if (a[i] > 0xFF) {
            return NET_ERR_INVALID;
        }
        mac->addr[i] = (uint8_t)a[i];
    }
    
    return 0;
}

/**
 * Check if a MAC address is a broadcast address
 */
int ethernet_is_broadcast(const mac_address_t* mac) {
    if (!mac) {
        return 0;
    }
    
    return (memcmp(mac->addr, broadcast_mac.addr, sizeof(mac_address_t)) == 0);
}

/**
 * Check if a MAC address is a multicast address
 */
int ethernet_is_multicast(const mac_address_t* mac) {
    if (!mac) {
        return 0;
    }
    
    // An Ethernet multicast address has the least significant bit of the first byte set
    return (mac->addr[0] & 0x01);
}

/**
 * Compare two MAC addresses
 */
int ethernet_mac_cmp(const mac_address_t* mac1, const mac_address_t* mac2) {
    if (!mac1 || !mac2) {
        return -1;
    }
    
    return memcmp(mac1->addr, mac2->addr, sizeof(mac_address_t));
}

/**
 * Copy a MAC address
 */
void ethernet_mac_copy(mac_address_t* dst, const mac_address_t* src) {
    if (!dst || !src) {
        return;
    }
    
    memcpy(dst->addr, src->addr, sizeof(mac_address_t));
}

/**
 * Generate a random MAC address (for virtual interfaces)
 * The address will be a locally administered, unicast MAC
 */
void ethernet_generate_mac(mac_address_t* mac) {
    if (!mac) {
        return;
    }
    
    // Get some randomness
    // In a real implementation, this would use a proper PRNG
    uint32_t random = 0x12345678;
    
    // Set the local bit (bit 1) and clear the multicast bit (bit 0)
    mac->addr[0] = 0x02; // 00000010 in binary: local, unicast
    
    // Fill the rest with "random" data
    mac->addr[1] = (random >> 24) & 0xFF;
    mac->addr[2] = (random >> 16) & 0xFF;
    mac->addr[3] = (random >> 8) & 0xFF;
    mac->addr[4] = random & 0xFF;
    mac->addr[5] = 0xAA; // Just a fixed value
}