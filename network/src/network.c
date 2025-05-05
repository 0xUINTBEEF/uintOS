/**
 * @file network.c
 * @brief Core networking implementation for uintOS
 */

#include "../include/network.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include <string.h>
#include <stdio.h>

// Global network state
static struct {
    net_device_t* devices[NET_MAX_DEVICES];
    int device_count;
    net_device_t* default_device;
    int initialized;
} net_state;

// Initialize the network stack
int network_init() {
    log_info("Initializing network subsystem");
    
    // Initialize network state
    memset(&net_state, 0, sizeof(net_state));
    net_state.device_count = 0;
    net_state.default_device = NULL;
    net_state.initialized = 1;
    
    log_info("Network subsystem initialized");
    return 0;
}

// Enhanced network initialization with loopback device
int network_init_enhanced() {
    int result = network_init();
    if (result != 0) {
        return result;
    }
    
    // Create and register loopback device
    log_info("Initializing loopback interface");
    
    // Allocate loopback device
    net_device_t* lo = (net_device_t*)heap_alloc(sizeof(net_device_t));
    if (!lo) {
        log_error("Failed to allocate loopback device");
        return NET_ERR_NOMEM;
    }
    
    // Initialize loopback device
    memset(lo, 0, sizeof(net_device_t));
    strncpy(lo->name, "lo", sizeof(lo->name) - 1);
    lo->flags = NET_DEV_FLAG_UP | NET_DEV_FLAG_LOOPBACK;
    lo->mtu = 65536;  // Loopback can handle large packets
    
    // Set loopback IP to 127.0.0.1
    lo->ip.addr[0] = 127;
    lo->ip.addr[1] = 0;
    lo->ip.addr[2] = 0;
    lo->ip.addr[3] = 1;
    
    // Set netmask to 255.0.0.0
    lo->netmask.addr[0] = 255;
    lo->netmask.addr[1] = 0;
    lo->netmask.addr[2] = 0;
    lo->netmask.addr[3] = 0;
    
    // Register the loopback device
    result = network_register_device(lo);
    if (result != 0) {
        log_error("Failed to register loopback device");
        heap_free(lo);
        return result;
    }
    
    log_info("Loopback interface initialized");
    return 0;
}

// Register a network device with the stack
int network_register_device(net_device_t* dev) {
    if (!dev) {
        return NET_ERR_INVALID;
    }
    
    if (!net_state.initialized) {
        log_error("Network subsystem not initialized");
        return NET_ERR_INVALID;
    }
    
    if (net_state.device_count >= NET_MAX_DEVICES) {
        log_error("Maximum number of network devices reached");
        return NET_ERR_INVALID;
    }
    
    // Check if device with same name already exists
    for (int i = 0; i < net_state.device_count; i++) {
        if (strcmp(net_state.devices[i]->name, dev->name) == 0) {
            log_warning("Network device with name '%s' already registered", dev->name);
            return NET_ERR_INVALID;
        }
    }
    
    // Register device
    net_state.devices[net_state.device_count] = dev;
    net_state.device_count++;
    
    // If this is the first device, make it the default
    if (net_state.device_count == 1) {
        net_state.default_device = dev;
    }
    
    log_info("Registered network device '%s'", dev->name);
    return 0;
}

// Find a network device by name
net_device_t* network_find_device_by_name(const char* name) {
    if (!name || !net_state.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < net_state.device_count; i++) {
        if (strcmp(net_state.devices[i]->name, name) == 0) {
            return net_state.devices[i];
        }
    }
    
    return NULL;
}

// Find a network device by IP address
net_device_t* network_find_device_by_ip(const ipv4_address_t* ip) {
    if (!ip || !net_state.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < net_state.device_count; i++) {
        if (memcmp(&net_state.devices[i]->ip, ip, sizeof(ipv4_address_t)) == 0) {
            return net_state.devices[i];
        }
    }
    
    return NULL;
}

// Get the default network device
net_device_t* network_get_default_device() {
    if (!net_state.initialized) {
        return NULL;
    }
    
    return net_state.default_device;
}

// Set the default network device
void network_set_default_device(net_device_t* dev) {
    if (!net_state.initialized) {
        return;
    }
    
    // Check if the device is registered
    int found = 0;
    for (int i = 0; i < net_state.device_count; i++) {
        if (net_state.devices[i] == dev) {
            found = 1;
            break;
        }
    }
    
    if (found) {
        net_state.default_device = dev;
        log_info("Set default network device to '%s'", dev->name);
    } else {
        log_warning("Attempted to set unregistered device as default");
    }
}

// Process incoming network packet
int network_receive_packet(net_device_t* dev, const void* data, size_t len) {
    if (!dev || !data || len == 0 || !net_state.initialized) {
        return NET_ERR_INVALID;
    }
    
    // Allocate a network buffer for the packet
    net_buffer_t* buffer = net_buffer_alloc(len + 64, 0);  // Extra space for headers
    if (!buffer) {
        log_error("Failed to allocate network buffer for incoming packet");
        return NET_ERR_NOMEM;
    }
    
    // Copy the packet data into the buffer
    memcpy(buffer->data, data, len);
    buffer->len = len;
    buffer->device = dev;
    
    // Update statistics
    dev->stats.rx_packets++;
    dev->stats.rx_bytes += len;
    
    // Determine the protocol and process the packet
    // For Ethernet packets, check ethertype
    if (len >= 14) {  // Minimum Ethernet frame size
        uint8_t* eth_data = (uint8_t*)data;
        uint16_t eth_type = (eth_data[12] << 8) | eth_data[13];
        
        log_debug("Received packet with ethertype 0x%04x on %s", eth_type, dev->name);
        
        // Process based on protocol
        switch (eth_type) {
            case 0x0800:  // IPv4
                buffer->protocol = NET_PROTO_IP;
                // Call IPv4 handler
                // ip_receive(buffer);
                break;
            case 0x0806:  // ARP
                buffer->protocol = NET_PROTO_ARP;
                // Call ARP handler
                // arp_receive(buffer);
                break;
            default:
                log_debug("Unknown ethertype 0x%04x", eth_type);
                break;
        }
    }
    
    // Free the buffer, since we don't have full protocol handling yet
    net_buffer_free(buffer);
    
    return 0;
}

// Allocate a network buffer
net_buffer_t* net_buffer_alloc(size_t size, size_t reserve_header) {
    if (size == 0) {
        return NULL;
    }
    
    // Allocate the buffer structure
    net_buffer_t* buffer = (net_buffer_t*)heap_alloc(sizeof(net_buffer_t));
    if (!buffer) {
        return NULL;
    }
    
    // Initialize the buffer structure
    memset(buffer, 0, sizeof(net_buffer_t));
    
    // Allocate the data buffer
    buffer->data = (uint8_t*)heap_alloc(size);
    if (!buffer->data) {
        heap_free(buffer);
        return NULL;
    }
    
    // Set up buffer parameters
    buffer->size = size;
    buffer->len = 0;
    buffer->offset = reserve_header;
    buffer->data += reserve_header;  // Reserve space for headers
    buffer->flags = NET_BUF_FLAG_ALLOC;
    
    return buffer;
}

// Free a network buffer
void net_buffer_free(net_buffer_t* buffer) {
    if (!buffer) {
        return;
    }
    
    // Free the data buffer if it was allocated
    if (buffer->flags & NET_BUF_FLAG_ALLOC && buffer->data) {
        buffer->data -= buffer->offset;  // Restore original pointer
        heap_free(buffer->data);
    }
    
    // Free the linked buffers in the chain
    if (buffer->next) {
        net_buffer_free(buffer->next);
    }
    
    // Free the buffer structure
    heap_free(buffer);
}

// Add data to the start of a network buffer (prepend)
void* net_buffer_push(net_buffer_t* buffer, size_t len) {
    if (!buffer || !buffer->data) {
        return NULL;
    }
    
    // Check if there's enough headroom
    if (buffer->offset < len) {
        return NULL;
    }
    
    // Move the data pointer back
    buffer->data -= len;
    buffer->offset -= len;
    buffer->len += len;
    
    return buffer->data;
}

// Remove data from the start of a network buffer
void* net_buffer_pull(net_buffer_t* buffer, size_t len) {
    if (!buffer || !buffer->data || buffer->len < len) {
        return NULL;
    }
    
    // Move the data pointer forward
    buffer->data += len;
    buffer->offset += len;
    buffer->len -= len;
    
    return buffer->data;
}

// Reserve headroom in a network buffer
int net_buffer_reserve(net_buffer_t* buffer, size_t len) {
    if (!buffer || !buffer->data) {
        return NET_ERR_INVALID;
    }
    
    // Check if there's already data in the buffer
    if (buffer->len > 0) {
        // Can't modify headroom if there's already data
        return NET_ERR_INVALID;
    }
    
    // Check if there's enough space in the buffer
    if (buffer->size < len) {
        return NET_ERR_NOMEM;
    }
    
    // Reset the data pointer
    if (buffer->flags & NET_BUF_FLAG_ALLOC) {
        buffer->data -= buffer->offset;  // Restore original pointer
    }
    
    // Set up new offset
    buffer->offset = len;
    buffer->data += len;
    
    return 0;
}

// Add data to the end of a network buffer (append)
int net_buffer_append(net_buffer_t* buffer, const void* data, size_t len) {
    if (!buffer || !buffer->data || !data) {
        return NET_ERR_INVALID;
    }
    
    // Check if there's enough space in the buffer
    if (buffer->offset + buffer->len + len > buffer->size) {
        return NET_ERR_NOMEM;
    }
    
    // Copy the data to the end of the buffer
    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    
    return 0;
}

// Convert an IPv4 address to string format
char* ipv4_to_str(const ipv4_address_t* ip, char* buffer) {
    if (!ip || !buffer) {
        return NULL;
    }
    
    sprintf(buffer, "%d.%d.%d.%d",
            ip->addr[0], ip->addr[1], ip->addr[2], ip->addr[3]);
    
    return buffer;
}

// Parse an IPv4 address from a string
int str_to_ipv4(const char* str, ipv4_address_t* ip) {
    if (!str || !ip) {
        return NET_ERR_INVALID;
    }
    
    int a, b, c, d;
    if (sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return NET_ERR_INVALID;
    }
    
    // Validate ranges
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) {
        return NET_ERR_INVALID;
    }
    
    // Set the IP address
    ip->addr[0] = (uint8_t)a;
    ip->addr[1] = (uint8_t)b;
    ip->addr[2] = (uint8_t)c;
    ip->addr[3] = (uint8_t)d;
    
    return 0;
}

// Get device count
int network_get_device_count() {
    if (!net_state.initialized) {
        return 0;
    }
    
    return net_state.device_count;
}

// Get a device by index
net_device_t* network_get_device(int index) {
    if (!net_state.initialized || index < 0 || index >= net_state.device_count) {
        return NULL;
    }
    
    return net_state.devices[index];
}