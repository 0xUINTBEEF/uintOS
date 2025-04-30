/**
 * @file udp.h
 * @brief UDP protocol implementation for uintOS
 * 
 * This file defines the User Datagram Protocol (UDP)
 * for the uintOS network stack.
 */

#ifndef UDP_H
#define UDP_H

#include "network.h"
#include "ip.h"

/**
 * UDP header size in bytes
 */
#define UDP_HEADER_SIZE 8

/**
 * Maximum number of UDP sockets
 */
#define UDP_MAX_SOCKETS 16

/**
 * UDP socket states
 */
#define UDP_SOCKET_CLOSED     0
#define UDP_SOCKET_OPEN       1

/**
 * UDP socket options
 */
#define UDP_OPT_BROADCAST     0x01    // Enable broadcast
#define UDP_OPT_REUSEADDR     0x02    // Allow reuse of local address
#define UDP_OPT_REUSEPORT     0x04    // Allow reuse of local port

/**
 * UDP header structure
 */
typedef struct udp_header {
    uint16_t src_port;    // Source port
    uint16_t dest_port;   // Destination port
    uint16_t length;      // Length of UDP header and data
    uint16_t checksum;    // Checksum for the UDP header, data, and pseudo-header
} __attribute__((packed)) udp_header_t;

/**
 * UDP pseudo-header for checksum calculation
 */
typedef struct udp_pseudo_header {
    ipv4_address_t src_addr;   // Source IP address
    ipv4_address_t dest_addr;  // Destination IP address
    uint8_t  zero;             // Always 0
    uint8_t  protocol;         // Protocol (17 for UDP)
    uint16_t udp_length;       // UDP length (header + data)
} __attribute__((packed)) udp_pseudo_header_t;

/**
 * UDP socket structure
 */
typedef struct udp_socket {
    uint8_t  state;            // Socket state (open, closed)
    uint16_t local_port;       // Local port
    uint16_t remote_port;      // Remote port (0 for any)
    ipv4_address_t local_addr; // Local address (0.0.0.0 for any)
    ipv4_address_t remote_addr;// Remote address (0.0.0.0 for any)
    uint8_t  options;          // Socket options
    
    // Callback for data reception
    void (*receive_callback)(struct udp_socket* socket, 
                            const ipv4_address_t* src_addr,
                            uint16_t src_port,
                            const void* data,
                            size_t len);
                            
    // User data pointer
    void* user_data;
} udp_socket_t;

/**
 * Initialize the UDP protocol handler
 * 
 * @return 0 on success, error code on failure
 */
int udp_init();

/**
 * Process an incoming UDP packet
 * 
 * @param buffer Network buffer containing the UDP packet
 * @return 0 on success, error code on failure
 */
int udp_rx(net_buffer_t* buffer);

/**
 * Send a UDP packet
 * 
 * @param socket UDP socket to send from
 * @param data Data to send
 * @param len Length of the data
 * @param dest_addr Destination IP address
 * @param dest_port Destination port
 * @return 0 on success, error code on failure
 */
int udp_tx(udp_socket_t* socket, const void* data, size_t len,
          const ipv4_address_t* dest_addr, uint16_t dest_port);

/**
 * Create a UDP socket
 * 
 * @param local_addr Local IP address (NULL for any)
 * @param local_port Local port (0 for random port)
 * @return Pointer to the new socket or NULL on failure
 */
udp_socket_t* udp_socket_create(const ipv4_address_t* local_addr, uint16_t local_port);

/**
 * Close and free a UDP socket
 * 
 * @param socket Socket to close
 * @return 0 on success, error code on failure
 */
int udp_socket_close(udp_socket_t* socket);

/**
 * Bind a UDP socket to a specific address and port
 * 
 * @param socket Socket to bind
 * @param addr IP address to bind to (NULL for any)
 * @param port Port to bind to (0 for random port)
 * @return 0 on success, error code on failure
 */
int udp_socket_bind(udp_socket_t* socket, const ipv4_address_t* addr, uint16_t port);

/**
 * Connect a UDP socket to a specific address and port
 * This doesn't establish a connection but sets the default destination
 * 
 * @param socket Socket to connect
 * @param addr IP address to connect to
 * @param port Port to connect to
 * @return 0 on success, error code on failure
 */
int udp_socket_connect(udp_socket_t* socket, const ipv4_address_t* addr, uint16_t port);

/**
 * Send data through a UDP socket
 * 
 * @param socket Socket to send from
 * @param data Data to send
 * @param len Length of the data
 * @return Number of bytes sent or error code on failure
 */
int udp_socket_send(udp_socket_t* socket, const void* data, size_t len);

/**
 * Send data through a UDP socket to a specific destination
 * 
 * @param socket Socket to send from
 * @param data Data to send
 * @param len Length of the data
 * @param addr Destination IP address
 * @param port Destination port
 * @return Number of bytes sent or error code on failure
 */
int udp_socket_sendto(udp_socket_t* socket, const void* data, size_t len,
                     const ipv4_address_t* addr, uint16_t port);

/**
 * Register a callback for data reception on a socket
 * 
 * @param socket Socket to register the callback for
 * @param callback Function to call when data is received
 * @return 0 on success, error code on failure
 */
int udp_socket_register_recv_callback(udp_socket_t* socket, 
                                     void (*callback)(udp_socket_t*,
                                                   const ipv4_address_t*,
                                                   uint16_t,
                                                   const void*,
                                                   size_t));

/**
 * Set options on a UDP socket
 * 
 * @param socket Socket to configure
 * @param options Options to set
 * @return 0 on success, error code on failure
 */
int udp_socket_set_options(udp_socket_t* socket, uint8_t options);

/**
 * Calculate the UDP checksum
 * 
 * @param header UDP header
 * @param data UDP data
 * @param data_len Length of the UDP data
 * @param src_addr Source IP address
 * @param dest_addr Destination IP address
 * @return The calculated checksum
 */
uint16_t udp_checksum(const udp_header_t* header, const void* data, size_t data_len,
                     const ipv4_address_t* src_addr, const ipv4_address_t* dest_addr);

/**
 * Get a free port for UDP
 * 
 * @return A free port number or 0 if none available
 */
uint16_t udp_get_free_port();

#endif /* UDP_H */