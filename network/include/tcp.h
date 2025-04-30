/**
 * @file tcp.h
 * @brief TCP protocol implementation for uintOS
 * 
 * This file defines the Transmission Control Protocol (TCP)
 * for the uintOS network stack.
 */

#ifndef TCP_H
#define TCP_H

#include "network.h"
#include "ip.h"

/**
 * TCP header size in bytes (without options)
 */
#define TCP_HEADER_SIZE 20

/**
 * Maximum number of TCP sockets
 */
#define TCP_MAX_SOCKETS 16

/**
 * Maximum segment size (data payload size)
 */
#define TCP_DEFAULT_MSS 536

/**
 * Default window size
 */
#define TCP_DEFAULT_WINDOW 4096

/**
 * TCP socket states
 */
typedef enum {
    TCP_STATE_CLOSED,         // Connection is closed
    TCP_STATE_LISTEN,         // Listening for connections
    TCP_STATE_SYN_SENT,       // SYN sent, waiting for SYN+ACK
    TCP_STATE_SYN_RECEIVED,   // SYN received, waiting for ACK
    TCP_STATE_ESTABLISHED,    // Connection established
    TCP_STATE_FIN_WAIT_1,     // FIN sent, waiting for ACK
    TCP_STATE_FIN_WAIT_2,     // FIN+ACK received, waiting for FIN
    TCP_STATE_CLOSING,        // FIN sent and received, waiting for ACK
    TCP_STATE_TIME_WAIT,      // Waiting for potential retransmissions
    TCP_STATE_CLOSE_WAIT,     // FIN received, waiting for application close
    TCP_STATE_LAST_ACK        // FIN sent, waiting for ACK
} tcp_state_t;

/**
 * TCP flags
 */
#define TCP_FLAG_FIN          0x01    // Finalize connection
#define TCP_FLAG_SYN          0x02    // Synchronize sequence numbers
#define TCP_FLAG_RST          0x04    // Reset connection
#define TCP_FLAG_PSH          0x08    // Push data immediately
#define TCP_FLAG_ACK          0x10    // Acknowledgment
#define TCP_FLAG_URG          0x20    // Urgent data

/**
 * TCP socket options
 */
#define TCP_OPT_NODELAY       0x01    // Disable Nagle algorithm
#define TCP_OPT_KEEPALIVE     0x02    // Enable keep-alive packets
#define TCP_OPT_REUSEADDR     0x04    // Allow reuse of local address

/**
 * TCP header structure
 */
typedef struct tcp_header {
    uint16_t src_port;         // Source port
    uint16_t dest_port;        // Destination port
    uint32_t seq_num;          // Sequence number
    uint32_t ack_num;          // Acknowledgment number
    uint8_t  data_offset;      // Data offset (4 bits) and reserved (4 bits)
    uint8_t  flags;            // TCP flags
    uint16_t window;           // Window size
    uint16_t checksum;         // Checksum
    uint16_t urgent_ptr;       // Urgent pointer
    // Options may follow
} __attribute__((packed)) tcp_header_t;

/**
 * TCP pseudo-header for checksum calculation
 */
typedef struct tcp_pseudo_header {
    ipv4_address_t src_addr;   // Source IP address
    ipv4_address_t dest_addr;  // Destination IP address
    uint8_t  zero;             // Always 0
    uint8_t  protocol;         // Protocol (6 for TCP)
    uint16_t tcp_length;       // TCP segment length (header + data)
} __attribute__((packed)) tcp_pseudo_header_t;

/**
 * TCP retransmission parameters
 */
typedef struct tcp_retransmit {
    uint32_t rto;              // Retransmission timeout (milliseconds)
    uint32_t srtt;             // Smoothed round-trip time (milliseconds)
    uint32_t rttvar;           // Round-trip time variation (milliseconds)
    uint8_t  attempts;         // Number of retransmission attempts
} tcp_retransmit_t;

/**
 * TCP connection information
 */
typedef struct tcp_connection {
    uint32_t snd_una;          // Send unacknowledged
    uint32_t snd_nxt;          // Send next
    uint32_t snd_wnd;          // Send window
    uint32_t rcv_nxt;          // Receive next
    uint32_t rcv_wnd;          // Receive window
    uint16_t mss;              // Maximum segment size
    tcp_retransmit_t retransmit; // Retransmission parameters
    net_buffer_t* retransmit_queue; // Queue of segments to retransmit
} tcp_connection_t;

/**
 * TCP listening socket
 */
typedef struct tcp_listener {
    uint16_t backlog;          // Maximum number of pending connections
    uint16_t pending_count;    // Current number of pending connections
    struct tcp_socket* pending_connections; // Pending connections list
} tcp_listener_t;

/**
 * TCP socket structure
 */
typedef struct tcp_socket {
    tcp_state_t state;         // Socket state
    uint16_t local_port;       // Local port
    uint16_t remote_port;      // Remote port
    ipv4_address_t local_addr;  // Local address
    ipv4_address_t remote_addr; // Remote address
    uint8_t options;           // Socket options
    
    tcp_connection_t conn;     // Connection parameters
    
    // For listening sockets
    tcp_listener_t* listener;  // Listener data if this is a listening socket
    
    // Socket queue (linked list)
    struct tcp_socket* next;   // Next socket in list
    
    // Data buffers
    net_buffer_t* send_buffer; // Data waiting to be sent
    net_buffer_t* recv_buffer; // Received data waiting to be read
    
    // Callbacks
    void (*connected_callback)(struct tcp_socket* socket);
    void (*data_ready_callback)(struct tcp_socket* socket, size_t len);
    void (*sent_callback)(struct tcp_socket* socket, size_t len);
    void (*closed_callback)(struct tcp_socket* socket);
    
    // User data pointer
    void* user_data;
} tcp_socket_t;

/**
 * Initialize the TCP protocol handler
 * 
 * @return 0 on success, error code on failure
 */
int tcp_init();

/**
 * Process an incoming TCP packet
 * 
 * @param buffer Network buffer containing the TCP packet
 * @return 0 on success, error code on failure
 */
int tcp_rx(net_buffer_t* buffer);

/**
 * Create a TCP socket
 * 
 * @param local_addr Local IP address (NULL for any)
 * @param local_port Local port (0 for random port)
 * @return Pointer to the new socket or NULL on failure
 */
tcp_socket_t* tcp_socket_create(const ipv4_address_t* local_addr, uint16_t local_port);

/**
 * Close and free a TCP socket
 * 
 * @param socket Socket to close
 * @return 0 on success, error code on failure
 */
int tcp_socket_close(tcp_socket_t* socket);

/**
 * Bind a TCP socket to a specific address and port
 * 
 * @param socket Socket to bind
 * @param addr IP address to bind to (NULL for any)
 * @param port Port to bind to (0 for random port)
 * @return 0 on success, error code on failure
 */
int tcp_socket_bind(tcp_socket_t* socket, const ipv4_address_t* addr, uint16_t port);

/**
 * Start listening for connections on a socket
 * 
 * @param socket Socket to listen on
 * @param backlog Maximum number of pending connections
 * @return 0 on success, error code on failure
 */
int tcp_socket_listen(tcp_socket_t* socket, uint16_t backlog);

/**
 * Accept a connection on a listening socket
 * 
 * @param socket Listening socket
 * @return New socket for the accepted connection or NULL if none pending
 */
tcp_socket_t* tcp_socket_accept(tcp_socket_t* socket);

/**
 * Connect to a remote host
 * 
 * @param socket Socket to connect with
 * @param addr Remote IP address
 * @param port Remote port
 * @return 0 on success, error code on failure
 */
int tcp_socket_connect(tcp_socket_t* socket, const ipv4_address_t* addr, uint16_t port);

/**
 * Send data through a TCP socket
 * 
 * @param socket Socket to send through
 * @param data Data to send
 * @param len Length of the data
 * @return Number of bytes queued for sending or error code on failure
 */
int tcp_socket_send(tcp_socket_t* socket, const void* data, size_t len);

/**
 * Receive data from a TCP socket
 * 
 * @param socket Socket to receive from
 * @param buffer Buffer to store the data
 * @param len Maximum length of data to receive
 * @return Number of bytes received or error code on failure
 */
int tcp_socket_recv(tcp_socket_t* socket, void* buffer, size_t len);

/**
 * Check if there is data available to read from a TCP socket
 * 
 * @param socket Socket to check
 * @return Number of bytes available to read or 0 if none
 */
size_t tcp_socket_available(tcp_socket_t* socket);

/**
 * Register callbacks for a TCP socket
 * 
 * @param socket Socket to register callbacks for
 * @param connected_callback Called when connection is established
 * @param data_ready_callback Called when data is available to read
 * @param sent_callback Called when data has been sent
 * @param closed_callback Called when connection is closed
 * @return 0 on success, error code on failure
 */
int tcp_socket_register_callbacks(tcp_socket_t* socket,
                                void (*connected_callback)(tcp_socket_t*),
                                void (*data_ready_callback)(tcp_socket_t*, size_t),
                                void (*sent_callback)(tcp_socket_t*, size_t),
                                void (*closed_callback)(tcp_socket_t*));

/**
 * Set options on a TCP socket
 * 
 * @param socket Socket to configure
 * @param options Options to set
 * @return 0 on success, error code on failure
 */
int tcp_socket_set_options(tcp_socket_t* socket, uint8_t options);

/**
 * Calculate the TCP checksum
 * 
 * @param header TCP header
 * @param data TCP data
 * @param data_len Length of the TCP data
 * @param src_addr Source IP address
 * @param dest_addr Destination IP address
 * @return The calculated checksum
 */
uint16_t tcp_checksum(const tcp_header_t* header, const void* data, size_t data_len,
                     const ipv4_address_t* src_addr, const ipv4_address_t* dest_addr);

/**
 * Process TCP timers (retransmission, keep-alive, etc.)
 * This should be called periodically by the network stack
 * 
 * @param msec Milliseconds since last call
 */
void tcp_timer(uint32_t msec);

/**
 * Get a free port for TCP
 * 
 * @return A free port number or 0 if none available
 */
uint16_t tcp_get_free_port();

/**
 * Get a string representation of a TCP state
 * 
 * @param state TCP state
 * @return String representation of the state
 */
const char* tcp_state_to_str(tcp_state_t state);

#endif /* TCP_H */