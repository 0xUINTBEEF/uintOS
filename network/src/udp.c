/**
 * @file udp.c
 * @brief UDP protocol implementation for uintOS
 * 
 * This file implements the User Datagram Protocol (UDP)
 * for the uintOS network stack.
 */

#include <string.h>
#include "../include/udp.h"
#include "../include/ip.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"

// Array of UDP sockets
static udp_socket_t udp_sockets[UDP_MAX_SOCKETS];

// Dynamic port allocation starts from this number
#define UDP_DYNAMIC_PORT_START 49152
#define UDP_DYNAMIC_PORT_END   65535

// Last allocated dynamic port
static uint16_t last_dynamic_port = UDP_DYNAMIC_PORT_START;

/**
 * Initialize the UDP protocol handler
 */
int udp_init() {
    log_info("NET", "Initializing UDP protocol handler");
    
    // Initialize socket array
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        udp_sockets[i].state = UDP_SOCKET_CLOSED;
    }
    
    // Register UDP as a protocol handler with IP
    int result = ip_register_protocol(IP_PROTO_UDP, udp_rx);
    if (result != 0) {
        log_error("NET", "Failed to register UDP with IP protocol handler");
        return result;
    }
    
    log_info("NET", "UDP protocol handler initialized successfully");
    return 0;
}

/**
 * Calculate UDP checksum
 */
uint16_t udp_checksum(const udp_header_t* header, const void* data, size_t data_len,
                     const ipv4_address_t* src_addr, const ipv4_address_t* dest_addr) {
    // Prepare pseudo-header for checksum calculation
    udp_pseudo_header_t pseudo;
    memcpy(&pseudo.src_addr, src_addr, sizeof(ipv4_address_t));
    memcpy(&pseudo.dest_addr, dest_addr, sizeof(ipv4_address_t));
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_UDP;
    pseudo.udp_length = htons(sizeof(udp_header_t) + data_len);
    
    // Calculate sum over pseudo-header
    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)&pseudo;
    size_t len = sizeof(udp_pseudo_header_t);
    
    // Sum up 16-bit words of pseudo header
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Sum up 16-bit words of UDP header
    ptr = (const uint16_t*)header;
    len = sizeof(udp_header_t);
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Sum up 16-bit words of data
    ptr = (const uint16_t*)data;
    len = data_len;
    
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
    
    return (uint16_t)(~sum);
}

/**
 * Find a socket for an incoming UDP packet
 */
static udp_socket_t* udp_find_socket(uint16_t dest_port, const ipv4_address_t* dest_addr) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].state == UDP_SOCKET_OPEN &&
            udp_sockets[i].local_port == dest_port) {
            
            // Check if socket is bound to a specific address
            if (!ip_addr_is_any(&udp_sockets[i].local_addr)) {
                // If bound to specific address, it must match
                if (ip_addr_cmp(&udp_sockets[i].local_addr, dest_addr) != 0) {
                    continue;
                }
            }
            
            return &udp_sockets[i];
        }
    }
    
    return NULL;
}

/**
 * Process an incoming UDP packet
 */
int udp_rx(net_buffer_t* buffer) {
    if (buffer == NULL) {
        log_error("NET", "Cannot process NULL UDP buffer");
        return -1;
    }
    
    if (buffer->len < sizeof(udp_header_t)) {
        log_warning("NET", "UDP packet too short");
        return -1;
    }
    
    // Get UDP header
    udp_header_t* udp = (udp_header_t*)buffer->data;
    
    // Extract and convert ports to host byte order
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dest_port = ntohs(udp->dest_port);
    
    // Extract and convert length to host byte order
    uint16_t udp_len = ntohs(udp->length);
    
    // Validate length
    if (udp_len < sizeof(udp_header_t) || udp_len > buffer->len) {
        log_warning("NET", "UDP invalid length: %u", udp_len);
        return -1;
    }
    
    // Get IP addresses from the buffer context
    ipv4_address_t* src_addr = (ipv4_address_t*)buffer->protocol_data;
    ipv4_address_t* dest_addr = src_addr + 1;
    
    // Format addresses for logging
    char src_addr_str[16], dest_addr_str[16];
    ipv4_to_str(src_addr, src_addr_str);
    ipv4_to_str(dest_addr, dest_addr_str);
    
    log_debug("NET", "UDP packet from %s:%u to %s:%u, length %u", 
             src_addr_str, src_port, dest_addr_str, dest_port, udp_len);
    
    // Verify checksum if present
    if (udp->checksum != 0) {
        uint16_t checksum = udp->checksum;
        udp->checksum = 0;
        
        uint16_t calculated = udp_checksum(udp, 
                                         buffer->data + sizeof(udp_header_t),
                                         udp_len - sizeof(udp_header_t),
                                         src_addr,
                                         dest_addr);
        
        if (checksum != calculated) {
            log_warning("NET", "UDP checksum verification failed");
            udp->checksum = checksum; // Restore original
            return -1;
        }
        
        // Restore original checksum
        udp->checksum = checksum;
    }
    
    // Find the socket for this packet
    udp_socket_t* socket = udp_find_socket(dest_port, dest_addr);
    
    if (socket == NULL) {
        // No socket found for this destination
        log_debug("NET", "No UDP socket found for port %u", dest_port);
        return -1;
    }
    
    // Skip the UDP header
    uint8_t* data = buffer->data + sizeof(udp_header_t);
    size_t data_len = udp_len - sizeof(udp_header_t);
    
    // Call the socket's receive callback if registered
    if (socket->receive_callback) {
        socket->receive_callback(socket, src_addr, src_port, data, data_len);
    }
    
    return 0;
}

/**
 * Send a UDP packet
 */
int udp_tx(udp_socket_t* socket, const void* data, size_t len,
          const ipv4_address_t* dest_addr, uint16_t dest_port) {
    if (socket == NULL || data == NULL || dest_addr == NULL) {
        log_error("NET", "Invalid parameters for udp_tx");
        return -1;
    }
    
    // Allocate a buffer for the UDP packet
    size_t udp_size = sizeof(udp_header_t) + len;
    net_buffer_t* buffer = ip_alloc_packet(udp_size);
    if (!buffer) {
        log_error("NET", "Failed to allocate UDP packet buffer");
        return -1;
    }
    
    // Fill the UDP header
    udp_header_t* udp = (udp_header_t*)buffer->data;
    udp->src_port = htons(socket->local_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(udp_size);
    udp->checksum = 0;
    
    // Copy data
    memcpy(buffer->data + sizeof(udp_header_t), data, len);
    
    // Set source address
    ipv4_address_t src_addr;
    if (ip_addr_is_any(&socket->local_addr)) {
        // Use the default interface's IP address if socket not bound to specific address
        net_device_t* dev = network_get_default_device();
        if (dev) {
            memcpy(&src_addr, &dev->ip, sizeof(ipv4_address_t));
        } else {
            // No default device, use any address
            memset(&src_addr, 0, sizeof(ipv4_address_t));
        }
    } else {
        // Use the socket's bound address
        memcpy(&src_addr, &socket->local_addr, sizeof(ipv4_address_t));
    }
    
    // Calculate checksum (optional for UDP in IPv4, but recommended)
    udp->checksum = udp_checksum(udp, data, len, &src_addr, dest_addr);
    
    // Set the buffer length
    buffer->len = udp_size;
    
    // Format addresses for logging
    char src_addr_str[16], dest_addr_str[16];
    ipv4_to_str(&src_addr, src_addr_str);
    ipv4_to_str(dest_addr, dest_addr_str);
    
    log_debug("NET", "Sending UDP packet from %s:%u to %s:%u, length %u", 
             src_addr_str, socket->local_port, dest_addr_str, dest_port, udp_size);
    
    // Send the UDP packet over IP
    int result = ip_tx(buffer, &src_addr, dest_addr, IP_PROTO_UDP, 64);
    
    // Free the buffer
    net_buffer_free(buffer);
    
    return result == 0 ? len : result;
}

/**
 * Get a free port for UDP
 */
uint16_t udp_get_free_port() {
    uint16_t start_port = last_dynamic_port;
    
    // Increment and wrap if needed
    if (++last_dynamic_port > UDP_DYNAMIC_PORT_END || last_dynamic_port < UDP_DYNAMIC_PORT_START) {
        last_dynamic_port = UDP_DYNAMIC_PORT_START;
    }
    
    // First attempt with the current last_dynamic_port
    uint16_t port = last_dynamic_port;
    do {
        // Check if this port is in use
        int in_use = 0;
        for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
            if (udp_sockets[i].state == UDP_SOCKET_OPEN && 
                udp_sockets[i].local_port == port) {
                in_use = 1;
                break;
            }
        }
        
        if (!in_use) {
            // Found a free port
            return port;
        }
        
        // Try next port
        if (++port > UDP_DYNAMIC_PORT_END) {
            port = UDP_DYNAMIC_PORT_START;
        }
    } while (port != start_port); // Stop if we've checked all ports
    
    // No free port found
    return 0;
}

/**
 * Create a UDP socket
 */
udp_socket_t* udp_socket_create(const ipv4_address_t* local_addr, uint16_t local_port) {
    // Find a free socket slot
    udp_socket_t* socket = NULL;
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].state == UDP_SOCKET_CLOSED) {
            socket = &udp_sockets[i];
            break;
        }
    }
    
    if (socket == NULL) {
        log_error("NET", "No free UDP socket slots");
        return NULL;
    }
    
    // Initialize the socket
    memset(socket, 0, sizeof(udp_socket_t));
    socket->state = UDP_SOCKET_OPEN;
    
    // Set address if provided, otherwise use any (0.0.0.0)
    if (local_addr) {
        memcpy(&socket->local_addr, local_addr, sizeof(ipv4_address_t));
    } else {
        memset(&socket->local_addr, 0, sizeof(ipv4_address_t));
    }
    
    // Set port or get a dynamic one
    if (local_port > 0) {
        // Check if the port is already in use
        int port_in_use = 0;
        for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
            if (i == (socket - udp_sockets)) {
                continue; // Skip the current socket
            }
            
            if (udp_sockets[i].state == UDP_SOCKET_OPEN && 
                udp_sockets[i].local_port == local_port) {
                
                // Only consider it in use if the address is the same or one of them is ANY
                if (ip_addr_is_any(&udp_sockets[i].local_addr) || 
                    ip_addr_is_any(&socket->local_addr) ||
                    ip_addr_cmp(&udp_sockets[i].local_addr, &socket->local_addr) == 0) {
                    port_in_use = 1;
                    break;
                }
            }
        }
        
        if (port_in_use) {
            log_error("NET", "UDP port %u already in use", local_port);
            socket->state = UDP_SOCKET_CLOSED;
            return NULL;
        }
        
        socket->local_port = local_port;
    } else {
        // Assign a dynamic port
        socket->local_port = udp_get_free_port();
        if (socket->local_port == 0) {
            log_error("NET", "Failed to allocate a dynamic UDP port");
            socket->state = UDP_SOCKET_CLOSED;
            return NULL;
        }
    }
    
    char addr_str[16];
    ipv4_to_str(&socket->local_addr, addr_str);
    log_info("NET", "Created UDP socket on %s:%u", addr_str, socket->local_port);
    
    return socket;
}

/**
 * Close and free a UDP socket
 */
int udp_socket_close(udp_socket_t* socket) {
    if (socket == NULL) {
        log_error("NET", "Cannot close NULL UDP socket");
        return -1;
    }
    
    // Check if it's one of our sockets
    if (socket < udp_sockets || socket >= (udp_sockets + UDP_MAX_SOCKETS)) {
        log_error("NET", "Invalid UDP socket pointer");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_warning("NET", "UDP socket is already closed");
        return 0;
    }
    
    char addr_str[16];
    ipv4_to_str(&socket->local_addr, addr_str);
    log_info("NET", "Closing UDP socket on %s:%u", addr_str, socket->local_port);
    
    socket->state = UDP_SOCKET_CLOSED;
    return 0;
}

/**
 * Bind a UDP socket to a specific address and port
 */
int udp_socket_bind(udp_socket_t* socket, const ipv4_address_t* addr, uint16_t port) {
    if (socket == NULL) {
        log_error("NET", "Cannot bind NULL UDP socket");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_error("NET", "Cannot bind closed UDP socket");
        return -1;
    }
    
    // Check if the port is already in use
    if (port > 0) {
        int port_in_use = 0;
        for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
            if (&udp_sockets[i] == socket) {
                continue; // Skip the current socket
            }
            
            if (udp_sockets[i].state == UDP_SOCKET_OPEN && 
                udp_sockets[i].local_port == port) {
                
                // Check address compatibility
                if (addr == NULL || ip_addr_is_any(&udp_sockets[i].local_addr) ||
                    ip_addr_cmp(addr, &udp_sockets[i].local_addr) == 0) {
                    port_in_use = 1;
                    break;
                }
            }
        }
        
        if (port_in_use) {
            log_error("NET", "UDP port %u already in use", port);
            return -1;
        }
        
        socket->local_port = port;
    } else if (socket->local_port == 0) {
        // Assign a dynamic port if none is set
        socket->local_port = udp_get_free_port();
        if (socket->local_port == 0) {
            log_error("NET", "Failed to allocate a dynamic UDP port");
            return -1;
        }
    }
    
    // Set address if provided
    if (addr != NULL) {
        memcpy(&socket->local_addr, addr, sizeof(ipv4_address_t));
    }
    
    char addr_str[16];
    ipv4_to_str(&socket->local_addr, addr_str);
    log_info("NET", "Bound UDP socket to %s:%u", addr_str, socket->local_port);
    
    return 0;
}

/**
 * Connect a UDP socket to a specific address and port
 */
int udp_socket_connect(udp_socket_t* socket, const ipv4_address_t* addr, uint16_t port) {
    if (socket == NULL || addr == NULL) {
        log_error("NET", "Invalid parameters for udp_socket_connect");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_error("NET", "Cannot connect closed UDP socket");
        return -1;
    }
    
    // Set the remote address and port
    memcpy(&socket->remote_addr, addr, sizeof(ipv4_address_t));
    socket->remote_port = port;
    
    char addr_str[16];
    ipv4_to_str(addr, addr_str);
    log_debug("NET", "Connected UDP socket to %s:%u", addr_str, port);
    
    return 0;
}

/**
 * Send data through a connected UDP socket
 */
int udp_socket_send(udp_socket_t* socket, const void* data, size_t len) {
    if (socket == NULL || data == NULL) {
        log_error("NET", "Invalid parameters for udp_socket_send");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_error("NET", "Cannot send on closed UDP socket");
        return -1;
    }
    
    if (socket->remote_port == 0) {
        log_error("NET", "UDP socket not connected, use sendto instead");
        return -1;
    }
    
    return udp_tx(socket, data, len, &socket->remote_addr, socket->remote_port);
}

/**
 * Send data to a specific destination through a UDP socket
 */
int udp_socket_sendto(udp_socket_t* socket, const void* data, size_t len,
                     const ipv4_address_t* addr, uint16_t port) {
    if (socket == NULL || data == NULL || addr == NULL) {
        log_error("NET", "Invalid parameters for udp_socket_sendto");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_error("NET", "Cannot send on closed UDP socket");
        return -1;
    }
    
    return udp_tx(socket, data, len, addr, port);
}

/**
 * Register a callback for data reception on a socket
 */
int udp_socket_register_recv_callback(udp_socket_t* socket, 
                                     void (*callback)(udp_socket_t* socket,
                                                   const ipv4_address_t* src_addr,
                                                   uint16_t src_port,
                                                   const void* data,
                                                   size_t len)) {
    if (socket == NULL) {
        log_error("NET", "Cannot register callback for NULL UDP socket");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_error("NET", "Cannot register callback for closed UDP socket");
        return -1;
    }
    
    socket->receive_callback = callback;
    return 0;
}

/**
 * Set options on a UDP socket
 */
int udp_socket_set_options(udp_socket_t* socket, uint8_t options) {
    if (socket == NULL) {
        log_error("NET", "Cannot set options for NULL UDP socket");
        return -1;
    }
    
    if (socket->state != UDP_SOCKET_OPEN) {
        log_error("NET", "Cannot set options for closed UDP socket");
        return -1;
    }
    
    socket->options = options;
    return 0;
}