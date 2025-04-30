/**
 * @file network.c
 * @brief Core networking implementation for uintOS
 */

#include "../include/network.h"
#include "../include/ethernet.h"
#include "../include/ip.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include <string.h>
#include <stdio.h>

// Registered network devices
static net_device_t* network_devices[NET_MAX_DEVICES];
static int num_devices = 0;
static net_device_t* default_device = NULL;

// Network stack initialization flag
static int network_initialized = 0;

/**
 * Initialize the network stack
 */
int network_init() {
    if (network_initialized) {
        return 0; // Already initialized
    }
    
    log_info("Initializing network stack");
    
    // Initialize all devices to NULL
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        network_devices[i] = NULL;
    }
    
    // Initialize protocol handlers
    int result = ethernet_init();
    if (result != 0) {
        log_error("Failed to initialize Ethernet protocol: %d", result);
        return result;
    }
    
    result = ip_init();
    if (result != 0) {
        log_error("Failed to initialize IP protocol: %d", result);
        return result;
    }
    
    network_initialized = 1;
    log_info("Network stack initialized successfully");
    
    return 0;
}

/**
 * Register a network device with the stack
 */
int network_register_device(net_device_t* dev) {
    if (!dev) {
        return NET_ERR_INVALID;
    }
    
    // Make sure we have room
    if (num_devices >= NET_MAX_DEVICES) {
        log_error("Maximum number of network devices (%d) reached", NET_MAX_DEVICES);
        return NET_ERR_NOMEM;
    }
    
    // Check if the device is already registered
    for (int i = 0; i < num_devices; i++) {
        if (network_devices[i] == dev || 
            strcmp(network_devices[i]->name, dev->name) == 0) {
            log_error("Network device '%s' already registered", dev->name);
            return NET_ERR_INVALID;
        }
    }
    
    // Initialize device statistics
    memset(&dev->stats, 0, sizeof(dev->stats));
    
    // Register the device
    network_devices[num_devices++] = dev;
    
    log_info("Network device '%s' registered successfully", dev->name);
    
    // If this is the first device, make it the default
    if (num_devices == 1) {
        default_device = dev;
    }
    
    // Try to bring up the device
    if (dev->ops.open) {
        int result = dev->ops.open(dev);
        if (result != 0) {
            log_warning("Failed to open network device '%s': %d", dev->name, result);
        }
    }
    
    return 0;
}

/**
 * Find a network device by name
 */
net_device_t* network_find_device_by_name(const char* name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < num_devices; i++) {
        if (strcmp(network_devices[i]->name, name) == 0) {
            return network_devices[i];
        }
    }
    
    return NULL;
}

/**
 * Find a network device by its IP address
 */
net_device_t* network_find_device_by_ip(const ipv4_address_t* ip) {
    if (!ip) {
        return NULL;
    }
    
    for (int i = 0; i < num_devices; i++) {
        if (memcmp(&network_devices[i]->ip, ip, sizeof(ipv4_address_t)) == 0) {
            return network_devices[i];
        }
    }
    
    return NULL;
}

/**
 * Convert an IPv4 address to string format
 */
char* ipv4_to_str(const ipv4_address_t* ip, char* buffer) {
    if (!ip || !buffer) {
        return NULL;
    }
    
    sprintf(buffer, "%d.%d.%d.%d",
            ip->addr[0], ip->addr[1], ip->addr[2], ip->addr[3]);
    
    return buffer;
}

/**
 * Parse an IPv4 address from a string
 */
int str_to_ipv4(const char* str, ipv4_address_t* ip) {
    if (!str || !ip) {
        return NET_ERR_INVALID;
    }
    
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return NET_ERR_INVALID;
    }
    
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return NET_ERR_INVALID;
    }
    
    ip->addr[0] = (uint8_t)a;
    ip->addr[1] = (uint8_t)b;
    ip->addr[2] = (uint8_t)c;
    ip->addr[3] = (uint8_t)d;
    
    return 0;
}

/**
 * Allocate a network buffer with specified size
 */
net_buffer_t* net_buffer_alloc(size_t size, size_t reserve_header) {
    if (size == 0) {
        return NULL;
    }
    
    // Allocate buffer structure
    net_buffer_t* buffer = (net_buffer_t*)kmalloc(sizeof(net_buffer_t));
    if (!buffer) {
        log_error("Failed to allocate network buffer structure");
        return NULL;
    }
    
    // Initialize buffer fields
    memset(buffer, 0, sizeof(net_buffer_t));
    
    // Allocate actual data buffer
    buffer->data = (uint8_t*)kmalloc(size);
    if (!buffer->data) {
        log_error("Failed to allocate network buffer data of size %zu", size);
        kfree(buffer);
        return NULL;
    }
    
    // Set buffer properties
    buffer->size = size;
    buffer->offset = reserve_header; // Reserve space for headers
    buffer->data += buffer->offset;  // Adjust data pointer
    buffer->len = 0;                 // No data yet
    buffer->flags = NET_BUF_FLAG_ALLOC;
    buffer->next = NULL;
    
    return buffer;
}

/**
 * Free a network buffer
 */
void net_buffer_free(net_buffer_t* buffer) {
    if (!buffer) {
        return;
    }
    
    if (buffer->flags & NET_BUF_FLAG_ALLOC) {
        // Restore original data pointer
        if (buffer->offset > 0) {
            buffer->data -= buffer->offset;
        }
        
        // Free the data buffer
        kfree(buffer->data);
    }
    
    // Free protocol-specific data if any
    if (buffer->protocol_data) {
        kfree(buffer->protocol_data);
    }
    
    // Free the buffer structure itself
    kfree(buffer);
}

/**
 * Add data to the start of a network buffer (prepend)
 */
void* net_buffer_push(net_buffer_t* buffer, size_t len) {
    if (!buffer || buffer->offset < len) {
        return NULL;
    }
    
    // Move data pointer back
    buffer->offset -= len;
    buffer->data -= len;
    buffer->len += len;
    
    return buffer->data;
}

/**
 * Remove data from the start of a network buffer
 */
void* net_buffer_pull(net_buffer_t* buffer, size_t len) {
    if (!buffer || buffer->len < len) {
        return NULL;
    }
    
    // Move data pointer forward
    buffer->data += len;
    buffer->offset += len;
    buffer->len -= len;
    
    return buffer->data;
}

/**
 * Reserve headroom in a network buffer
 */
int net_buffer_reserve(net_buffer_t* buffer, size_t len) {
    if (!buffer || buffer->len > 0) {
        return NET_ERR_INVALID;
    }
    
    if (buffer->size < len) {
        return NET_ERR_NOMEM;
    }
    
    // Adjust offset to reserve space
    buffer->offset = len;
    buffer->data = (uint8_t*)buffer->data + len;
    
    return 0;
}

/**
 * Add data to the end of a network buffer (append)
 */
int net_buffer_append(net_buffer_t* buffer, const void* data, size_t len) {
    if (!buffer || !data) {
        return NET_ERR_INVALID;
    }
    
    // Check if we have enough space
    if (buffer->len + len > buffer->size - buffer->offset) {
        return NET_ERR_NOMEM;
    }
    
    // Copy data to the end of the buffer
    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    
    return 0;
}

/**
 * Get the default network device
 */
net_device_t* network_get_default_device() {
    return default_device;
}

/**
 * Set the default network device
 */
void network_set_default_device(net_device_t* dev) {
    default_device = dev;
}

/**
 * Process incoming network packet
 */
int network_receive_packet(net_device_t* dev, const void* data, size_t len) {
    if (!dev || !data || len == 0) {
        return NET_ERR_INVALID;
    }
    
    // Update device statistics
    dev->stats.rx_packets++;
    dev->stats.rx_bytes += len;
    
    // Allocate a buffer for this packet
    net_buffer_t* buffer = net_buffer_alloc(len, 0);
    if (!buffer) {
        dev->stats.rx_dropped++;
        log_error("Failed to allocate buffer for received packet");
        return NET_ERR_NOMEM;
    }
    
    // Copy packet data
    memcpy(buffer->data, data, len);
    buffer->len = len;
    buffer->device = dev;
    
    // Assume Ethernet for now, dispatch to Ethernet handler
    int result = ethernet_rx(buffer);
    if (result != 0) {
        dev->stats.rx_errors++;
        net_buffer_free(buffer);
    }
    
    return result;
}

/**
 * Get device count
 */
int network_get_device_count() {
    return num_devices;
}

/**
 * Get a device by index
 */
net_device_t* network_get_device(int index) {
    if (index < 0 || index >= num_devices) {
        return NULL;
    }
    
    return network_devices[index];
}

/**
 * Enhanced network initialization - sets up loopback device
 */
int network_init_enhanced() {
    int result = network_init();
    if (result != 0) {
        return result;
    }
    
    // Setup a loopback device
    // This would need to be implemented by the actual device driver
    // but we define the prototype here
    extern net_device_t* loopback_create();
    
    net_device_t* loopback = loopback_create();
    if (loopback) {
        result = network_register_device(loopback);
        if (result != 0) {
            log_error("Failed to register loopback device: %d", result);
        } else {
            log_info("Loopback device registered");
        }
    } else {
        log_error("Failed to create loopback device");
    }
    
    return result;
}