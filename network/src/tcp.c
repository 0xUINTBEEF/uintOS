/**
 * @file tcp.c
 * @brief TCP protocol implementation for uintOS
 * 
 * This file implements the Transmission Control Protocol (TCP)
 * for the uintOS network stack.
 */

#include <string.h>
#include "../include/tcp.h"
#include "../include/ip.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"

// Array of TCP sockets
static tcp_socket_t tcp_sockets[TCP_MAX_SOCKETS];

// Dynamic port allocation starts from this number
#define TCP_DYNAMIC_PORT_START 49152
#define TCP_DYNAMIC_PORT_END   65535

// Last allocated dynamic port
static uint16_t last_dynamic_port = TCP_DYNAMIC_PORT_START;

// Initial sequence number
static uint32_t tcp_initial_seq = 0;

// Maximum segment lifetime in milliseconds (2 minutes)
#define TCP_MSL 120000

// TCP retransmission parameters
#define TCP_RETRANSMIT_TIMEOUT 500      // Initial retransmission timeout (ms)
#define TCP_MAX_RETRANSMITS 5           // Maximum retransmission attempts

#define TCP_DEFAULT_BUFFER_SIZE 8192  // Default buffer size (8KB)

// Structure for TCP receive buffer
typedef struct {
    uint8_t* data;           // Buffer data
    uint32_t size;           // Total buffer size
    uint32_t start;          // Start position for reading
    uint32_t end;            // End position for writing
    uint32_t bytes_available; // Number of bytes available to read
} tcp_buffer_t;

// Initialize a TCP receive buffer
static int tcp_buffer_init(tcp_socket_t* socket, uint32_t size) {
    if (!socket) return -1;
    
    // Allocate buffer memory
    socket->rx_buffer.data = malloc(size);
    if (!socket->rx_buffer.data) {
        log_error("NET", "Failed to allocate TCP receive buffer");
        return -1;
    }
    
    // Initialize buffer state
    socket->rx_buffer.size = size;
    socket->rx_buffer.start = 0;
    socket->rx_buffer.end = 0;
    socket->rx_buffer.bytes_available = 0;
    
    return 0;
}

// Write data to the TCP receive buffer
static int tcp_buffer_write(tcp_socket_t* socket, const uint8_t* data, uint32_t len) {
    if (!socket || !socket->rx_buffer.data || !data) return -1;
    
    // Check if there's enough space
    if (socket->rx_buffer.bytes_available + len > socket->rx_buffer.size) {
        log_warning("NET", "TCP receive buffer overflow, dropping data");
        return -1;
    }
    
    // Copy data to buffer, handling wrap-around
    for (uint32_t i = 0; i < len; i++) {
        socket->rx_buffer.data[socket->rx_buffer.end] = data[i];
        socket->rx_buffer.end = (socket->rx_buffer.end + 1) % socket->rx_buffer.size;
    }
    
    // Update available bytes count
    socket->rx_buffer.bytes_available += len;
    
    return len;
}

// Read data from the TCP receive buffer
static int tcp_buffer_read(tcp_socket_t* socket, uint8_t* data, uint32_t len) {
    if (!socket || !socket->rx_buffer.data || !data) return -1;
    
    // Determine how much data we can actually read
    uint32_t read_len = len;
    if (read_len > socket->rx_buffer.bytes_available) {
        read_len = socket->rx_buffer.bytes_available;
    }
    
    // No data available
    if (read_len == 0) return 0;
    
    // Copy data from buffer, handling wrap-around
    for (uint32_t i = 0; i < read_len; i++) {
        data[i] = socket->rx_buffer.data[socket->rx_buffer.start];
        socket->rx_buffer.start = (socket->rx_buffer.start + 1) % socket->rx_buffer.size;
    }
    
    // Update available bytes count
    socket->rx_buffer.bytes_available -= read_len;
    
    return read_len;
}

// Peek at data in the buffer without removing it
static int tcp_buffer_peek(tcp_socket_t* socket, uint8_t* data, uint32_t len) {
    if (!socket || !socket->rx_buffer.data || !data) return -1;
    
    // Determine how much data we can actually read
    uint32_t read_len = len;
    if (read_len > socket->rx_buffer.bytes_available) {
        read_len = socket->rx_buffer.bytes_available;
    }
    
    // No data available
    if (read_len == 0) return 0;
    
    // Copy data from buffer without updating pointers
    uint32_t pos = socket->rx_buffer.start;
    for (uint32_t i = 0; i < read_len; i++) {
        data[i] = socket->rx_buffer.data[pos];
        pos = (pos + 1) % socket->rx_buffer.size;
    }
    
    return read_len;
}

// Free the TCP receive buffer
static void tcp_buffer_free(tcp_socket_t* socket) {
    if (!socket || !socket->rx_buffer.data) return;
    
    free(socket->rx_buffer.data);
    socket->rx_buffer.data = NULL;
    socket->rx_buffer.size = 0;
    socket->rx_buffer.start = 0;
    socket->rx_buffer.end = 0;
    socket->rx_buffer.bytes_available = 0;
}

// Structure to track TIME_WAIT state
typedef struct {
    uint32_t start_time;     // When the timer started
    uint32_t duration;       // How long to stay in TIME_WAIT (2*MSL)
    tcp_socket_t* socket;    // The socket that is in TIME_WAIT
} tcp_time_wait_t;

// Array of TIME_WAIT timers
#define TCP_MAX_TIME_WAIT_SOCKETS 32
static tcp_time_wait_t time_wait_sockets[TCP_MAX_TIME_WAIT_SOCKETS];

/**
 * Initialize the TCP protocol handler
 */
int tcp_init() {
    log_info("NET", "Initializing TCP protocol handler");
    
    // Initialize socket array
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_sockets[i].state = TCP_STATE_CLOSED;
    }
    
    // Initialize the TIME_WAIT tracker
    tcp_init_time_wait();
    
    // Register TCP as a protocol handler with IP
    int result = ip_register_protocol(IP_PROTO_TCP, tcp_rx);
    if (result != 0) {
        log_error("NET", "Failed to register TCP with IP protocol handler");
        return result;
    }
    
    // Initialize the initial sequence number
    // In a real implementation, this should be based on a timer
    // to ensure different initial sequence numbers on reboot
    tcp_initial_seq = 0x01020304;
    
    log_info("NET", "TCP protocol handler initialized successfully");
    return 0;
}

/**
 * Initialize the TIME_WAIT tracker
 */
static void tcp_init_time_wait() {
    for (int i = 0; i < TCP_MAX_TIME_WAIT_SOCKETS; i++) {
        time_wait_sockets[i].socket = NULL;
    }
}

/**
 * Start a TIME_WAIT timer for a socket
 */
static void tcp_start_time_wait(tcp_socket_t* socket) {
    if (socket == NULL) {
        return;
    }
    
    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < TCP_MAX_TIME_WAIT_SOCKETS; i++) {
        if (time_wait_sockets[i].socket == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        log_warning("NET", "No free TIME_WAIT slots, closing socket immediately");
        socket->state = TCP_STATE_CLOSED;
        return;
    }
    
    // Setup the TIME_WAIT timer
    time_wait_sockets[slot].socket = socket;
    time_wait_sockets[slot].start_time = network_get_time_ms();
    time_wait_sockets[slot].duration = 2 * TCP_MSL; // 2*MSL as required by TCP spec
    
    char addr_str[16];
    ipv4_to_str(&socket->remote_addr, addr_str);
    log_info("NET", "TCP connection to %s:%u entered TIME_WAIT state", 
             addr_str, socket->remote_port);
}

/**
 * Calculate TCP checksum
 */
uint16_t tcp_checksum(const tcp_header_t* header, const void* data, size_t data_len,
                     const ipv4_address_t* src_addr, const ipv4_address_t* dest_addr) {
    // Prepare pseudo-header for checksum calculation
    tcp_pseudo_header_t pseudo;
    memcpy(&pseudo.src_addr, src_addr, sizeof(ipv4_address_t));
    memcpy(&pseudo.dest_addr, dest_addr, sizeof(ipv4_address_t));
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_length = htons(TCP_HEADER_SIZE + data_len);
    
    // Calculate sum over pseudo-header
    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)&pseudo;
    size_t len = sizeof(tcp_pseudo_header_t);
    
    // Sum up 16-bit words of pseudo header
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Sum up 16-bit words of TCP header
    ptr = (const uint16_t*)header;
    len = TCP_HEADER_SIZE;
    
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
 * Find a socket matching the connection parameters
 */
static tcp_socket_t* tcp_find_socket(uint16_t local_port, const ipv4_address_t* local_addr,
                                   uint16_t remote_port, const ipv4_address_t* remote_addr) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t* socket = &tcp_sockets[i];
        
        // Skip closed sockets
        if (socket->state == TCP_STATE_CLOSED) {
            continue;
        }
        
        // Check for exact match on established connections
        if (socket->state != TCP_STATE_LISTEN &&
            socket->local_port == local_port &&
            socket->remote_port == remote_port &&
            ip_addr_cmp(&socket->local_addr, local_addr) == 0 &&
            ip_addr_cmp(&socket->remote_addr, remote_addr) == 0) {
            return socket;
        }
        
        // Check for listening socket that matches local port
        if (socket->state == TCP_STATE_LISTEN &&
            socket->local_port == local_port) {
            // If the socket is bound to a specific address, it must match
            if (!ip_addr_is_any(&socket->local_addr) &&
                ip_addr_cmp(&socket->local_addr, local_addr) != 0) {
                continue;
            }
            
            return socket;
        }
    }
    
    return NULL;
}

/**
 * Send a TCP segment
 */
static int tcp_send_segment(tcp_socket_t* socket, uint8_t flags,
                          const void* data, size_t data_len) {
    if (socket == NULL) {
        log_error("NET", "Cannot send TCP segment on NULL socket");
        return -1;
    }
    
    // Log the segment being sent
    char dest_str[16];
    ipv4_to_str(&socket->remote_addr, dest_str);
    log_debug("NET", "Sending TCP segment to %s:%u, flags=0x%02x, len=%u",
             dest_str, socket->remote_port, flags, data_len);
    
    // Allocate a buffer for the TCP segment
    size_t tcp_size = TCP_HEADER_SIZE + data_len;
    net_buffer_t* buffer = ip_alloc_packet(tcp_size);
    if (!buffer) {
        log_error("NET", "Failed to allocate TCP segment buffer");
        return -1;
    }
    
    // Fill the TCP header
    tcp_header_t* tcp = (tcp_header_t*)buffer->data;
    tcp->src_port = htons(socket->local_port);
    tcp->dest_port = htons(socket->remote_port);
    
    // Set sequence and acknowledgment numbers
    tcp->seq_num = htonl(socket->conn.snd_nxt);
    if (flags & TCP_FLAG_ACK) {
        tcp->ack_num = htonl(socket->conn.rcv_nxt);
    } else {
        tcp->ack_num = 0;
    }
    
    // Set data offset (in 32-bit words) and flags
    tcp->data_offset = (TCP_HEADER_SIZE / 4) << 4;
    tcp->flags = flags;
    
    // Set window size
    tcp->window = htons(socket->conn.rcv_wnd);
    
    // Clear checksum and urgent pointer
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    // Copy data if provided
    if (data != NULL && data_len > 0) {
        memcpy(buffer->data + TCP_HEADER_SIZE, data, data_len);
    }
    
    // Calculate the checksum
    tcp->checksum = tcp_checksum(tcp, data, data_len,
                               &socket->local_addr, &socket->remote_addr);
    
    // Set the buffer length
    buffer->len = tcp_size;
    
    // Update sequence number if we're sending data or SYN/FIN
    if (data_len > 0 || (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))) {
        socket->conn.snd_nxt += data_len;
        
        // SYN and FIN each consume a sequence number
        if (flags & TCP_FLAG_SYN) socket->conn.snd_nxt++;
        if (flags & TCP_FLAG_FIN) socket->conn.snd_nxt++;
    }
    
    // Send the TCP segment over IP
    int result = ip_tx(buffer, &socket->local_addr, &socket->remote_addr, IP_PROTO_TCP, 64);
    
    // Free the buffer
    net_buffer_free(buffer);
    
    return result;
}

/**
 * Process a TCP SYN packet (connection request)
 */
static int tcp_process_syn(tcp_socket_t* listening_socket,
                         const ipv4_address_t* src_addr, uint16_t src_port,
                         const ipv4_address_t* dest_addr, uint16_t dest_port,
                         uint32_t seq_num, uint16_t window) {
    if (listening_socket->state != TCP_STATE_LISTEN) {
        log_error("NET", "TCP socket not in LISTEN state");
        return -1;
    }
    
    // Check if we have space in the backlog
    if (listening_socket->listener->pending_count >= listening_socket->listener->backlog) {
        log_warning("NET", "TCP listen backlog full, dropping connection");
        return -1;
    }
    
    // Find a free socket for the new connection
    tcp_socket_t* new_socket = NULL;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (tcp_sockets[i].state == TCP_STATE_CLOSED) {
            new_socket = &tcp_sockets[i];
            break;
        }
    }
    
    if (new_socket == NULL) {
        log_error("NET", "No free TCP sockets available");
        return -1;
    }
    
    // Initialize the new socket
    memset(new_socket, 0, sizeof(tcp_socket_t));
    new_socket->state = TCP_STATE_SYN_RECEIVED;
    
    // Set local and remote information
    new_socket->local_port = dest_port;
    new_socket->remote_port = src_port;
    memcpy(&new_socket->local_addr, dest_addr, sizeof(ipv4_address_t));
    memcpy(&new_socket->remote_addr, src_addr, sizeof(ipv4_address_t));
    
    // Initialize connection parameters
    new_socket->conn.snd_una = tcp_initial_seq;
    new_socket->conn.snd_nxt = tcp_initial_seq;
    new_socket->conn.rcv_nxt = seq_num + 1; // Increment to account for the SYN
    new_socket->conn.snd_wnd = window;
    new_socket->conn.rcv_wnd = TCP_DEFAULT_WINDOW;
    new_socket->conn.mss = TCP_DEFAULT_MSS;
    
    // Initialize retransmission parameters
    new_socket->conn.retransmit.rto = TCP_RETRANSMIT_TIMEOUT;
    new_socket->conn.retransmit.attempts = 0;
    
    // Initialize receive buffer
    if (tcp_buffer_init(new_socket, TCP_DEFAULT_BUFFER_SIZE) != 0) {
        log_error("NET", "Failed to initialize TCP receive buffer");
        return -1;
    }
    
    // Add to the pending connections list
    new_socket->next = listening_socket->listener->pending_connections;
    listening_socket->listener->pending_connections = new_socket;
    listening_socket->listener->pending_count++;
    
    char src_str[16];
    ipv4_to_str(src_addr, src_str);
    log_info("NET", "TCP connection request from %s:%u", src_str, src_port);
    
    // Send SYN+ACK
    return tcp_send_segment(new_socket, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
}

/**
 * Process a TCP ACK packet
 */
static int tcp_process_ack(tcp_socket_t* socket, uint32_t ack_num, uint16_t window) {
    // Update send window
    socket->conn.snd_wnd = window;
    
    // Check if the ACK is valid
    if (ack_num <= socket->conn.snd_una || ack_num > socket->conn.snd_nxt) {
        // Ignore invalid ACKs
        return 0;
    }
    
    // Update the send unacknowledged pointer
    socket->conn.snd_una = ack_num;
    
    // Handle state transitions based on ACK
    switch (socket->state) {
        case TCP_STATE_SYN_SENT:
            // Received ACK for our SYN, but no SYN from remote
            // This is not a normal transition, reset the connection
            socket->state = TCP_STATE_CLOSED;
            tcp_send_segment(socket, TCP_FLAG_RST, NULL, 0);
            return -1;
            
        case TCP_STATE_SYN_RECEIVED:
            // Connection established!
            socket->state = TCP_STATE_ESTABLISHED;
            log_info("NET", "TCP connection established");
            
            // Notify the application
            if (socket->connected_callback) {
                socket->connected_callback(socket);
            }
            return 0;
            
        case TCP_STATE_ESTABLISHED:
            // Regular data ACK
            return 0;
            
        case TCP_STATE_FIN_WAIT_1:
            // FIN acknowledged
            socket->state = TCP_STATE_FIN_WAIT_2;
            return 0;
            
        case TCP_STATE_CLOSING:
            // FIN acknowledged
            socket->state = TCP_STATE_TIME_WAIT;
            tcp_start_time_wait(socket);
            return 0;
            
        case TCP_STATE_LAST_ACK:
            // FIN acknowledged, connection closed
            socket->state = TCP_STATE_CLOSED;
            
            // Notify the application
            if (socket->closed_callback) {
                socket->closed_callback(socket);
            }
            return 0;
            
        default:
            // Other states don't have special handling for ACKs
            return 0;
    }
}

/**
 * Process a TCP packet with data
 */
static int tcp_process_data(tcp_socket_t* socket, const uint8_t* data, 
                          size_t data_len, uint32_t seq_num) {
    // Check if the data is in sequence
    if (seq_num != socket->conn.rcv_nxt) {
        // Out of sequence, send duplicate ACK
        // In a full implementation, we should buffer out-of-order segments
        tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
        return -1;
    }
    
    // Update the next expected sequence number
    socket->conn.rcv_nxt += data_len;
    
    // Write data to the receive buffer
    if (tcp_buffer_write(socket, data, data_len) != data_len) {
        log_warning("NET", "Failed to write data to TCP receive buffer");
        return -1;
    }
    
    // Notify the application that data is ready
    if (socket->data_ready_callback) {
        socket->data_ready_callback(socket, data_len);
    }
    
    // Send an ACK
    tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
    
    return 0;
}

/**
 * Process a TCP FIN packet (connection termination)
 */
static int tcp_process_fin(tcp_socket_t* socket, uint32_t seq_num) {
    // Update the next expected sequence number
    socket->conn.rcv_nxt = seq_num + 1; // +1 for the FIN
    
    // Update socket state based on current state
    switch (socket->state) {
        case TCP_STATE_ESTABLISHED:
            socket->state = TCP_STATE_CLOSE_WAIT;
            
            // Send ACK for the FIN
            tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
            
            // Notify the application that peer has closed
            if (socket->closed_callback) {
                socket->closed_callback(socket);
            }
            return 0;
            
        case TCP_STATE_FIN_WAIT_1:
            // Simultaneous close
            socket->state = TCP_STATE_CLOSING;
            
            // Send ACK for the FIN
            tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
            return 0;
            
        case TCP_STATE_FIN_WAIT_2:
            socket->state = TCP_STATE_TIME_WAIT;
            
            // Send ACK for the FIN
            tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
            
            // Start the TIME_WAIT timer
            tcp_start_time_wait(socket);
            return 0;
            
        default:
            // Unexpected FIN in other states, still send ACK
            tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
            return -1;
    }
}

/**
 * Process an incoming TCP packet
 */
int tcp_rx(net_buffer_t* buffer) {
    if (buffer == NULL) {
        log_error("NET", "Cannot process NULL TCP buffer");
        return -1;
    }
    
    if (buffer->len < TCP_HEADER_SIZE) {
        log_warning("NET", "TCP packet too short");
        return -1;
    }
    
    // Get TCP header
    tcp_header_t* tcp = (tcp_header_t*)buffer->data;
    
    // Extract and convert ports to host byte order
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dest_port = ntohs(tcp->dest_port);
    
    // Extract and convert sequence and acknowledgment numbers
    uint32_t seq_num = ntohl(tcp->seq_num);
    uint32_t ack_num = ntohl(tcp->ack_num);
    
    // Extract data offset and flags
    uint8_t data_offset = (tcp->data_offset >> 4) & 0x0F;
    uint8_t flags = tcp->flags;
    
    // Extract window size
    uint16_t window = ntohs(tcp->window);
    
    // Calculate header size in bytes
    uint16_t hdr_size = data_offset * 4;
    
    // Check header size validity
    if (hdr_size < TCP_HEADER_SIZE || hdr_size > buffer->len) {
        log_warning("NET", "Invalid TCP header size: %u", hdr_size);
        return -1;
    }
    
    // Get IP addresses from the buffer context
    ipv4_address_t* src_addr = (ipv4_address_t*)buffer->protocol_data;
    ipv4_address_t* dest_addr = src_addr + 1;
    
    // Format addresses for logging
    char src_str[16], dest_str[16];
    ipv4_to_str(src_addr, src_str);
    ipv4_to_str(dest_addr, dest_str);
    
    log_debug("NET", "TCP packet from %s:%u to %s:%u, seq=%u, ack=%u, flags=0x%02x, win=%u, len=%u",
             src_str, src_port, dest_str, dest_port,
             seq_num, ack_num, flags, window, buffer->len - hdr_size);
    
    // Verify checksum
    uint16_t checksum = tcp->checksum;
    tcp->checksum = 0;
    
    uint16_t calculated = tcp_checksum(tcp, 
                                     buffer->data + hdr_size,
                                     buffer->len - hdr_size,
                                     src_addr,
                                     dest_addr);
    
    if (checksum != calculated) {
        log_warning("NET", "TCP checksum verification failed");
        tcp->checksum = checksum; // Restore original
        return -1;
    }
    
    // Restore original checksum
    tcp->checksum = checksum;
    
    // Find the socket for this packet
    tcp_socket_t* socket = tcp_find_socket(dest_port, dest_addr, src_port, src_addr);
    
    // Handle RST packets (connection reset)
    if (flags & TCP_FLAG_RST) {
        if (socket != NULL) {
            log_warning("NET", "Connection reset by peer");
            
            // Move the socket to CLOSED state
            socket->state = TCP_STATE_CLOSED;
            
            // Notify the application
            if (socket->closed_callback) {
                socket->closed_callback(socket);
            }
        }
        return 0;
    }
    
    // Handle SYN packets (connection establishment)
    if (flags & TCP_FLAG_SYN) {
        // Check if this is a SYN+ACK for a connection in progress
        if ((flags & TCP_FLAG_ACK) && socket != NULL && socket->state == TCP_STATE_SYN_SENT) {
            // Validate the ACK
            if (ack_num != socket->conn.snd_nxt) {
                log_warning("NET", "Unexpected ACK in SYN+ACK: %u, expected: %u",
                           ack_num, socket->conn.snd_nxt);
                return -1;
            }
            
            // Update socket state
            socket->state = TCP_STATE_ESTABLISHED;
            socket->conn.snd_una = ack_num;
            socket->conn.rcv_nxt = seq_num + 1;  // +1 for the SYN
            socket->conn.snd_wnd = window;
            
            // Send an ACK for the SYN+ACK
            tcp_send_segment(socket, TCP_FLAG_ACK, NULL, 0);
            
            log_info("NET", "TCP connection established with %s:%u", src_str, src_port);
            
            // Notify the application
            if (socket->connected_callback) {
                socket->connected_callback(socket);
            }
            
            return 0;
        }
        
        // Check if we have a listening socket for this destination
        if (socket != NULL && socket->state == TCP_STATE_LISTEN) {
            // Process a new connection request
            return tcp_process_syn(socket, src_addr, src_port, dest_addr, dest_port,
                                 seq_num, window);
        }
        
        // No matching socket or not in appropriate state
        log_warning("NET", "TCP SYN received but no listening socket on port %u", dest_port);
        
        // Send RST+ACK
        // In a full implementation, we'd send an RST to reject the connection
        return -1;
    }
    
    // For all other packets, we need an existing socket
    if (socket == NULL) {
        log_warning("NET", "TCP packet for non-existent socket: %s:%u -> %s:%u",
                  src_str, src_port, dest_str, dest_port);
        
        // Send RST to reject the packet
        // In a full implementation, we'd send an RST
        return -1;
    }
    
    // Handle ACK packets
    if (flags & TCP_FLAG_ACK) {
        tcp_process_ack(socket, ack_num, window);
    }
    
    // Handle data
    size_t data_len = buffer->len - hdr_size;
    if (data_len > 0) {
        tcp_process_data(socket, buffer->data + hdr_size, data_len, seq_num);
    }
    
    // Handle FIN packets (connection termination)
    if (flags & TCP_FLAG_FIN) {
        tcp_process_fin(socket, seq_num + data_len);
    }
    
    return 0;
}

/**
 * Get a free port for TCP
 */
uint16_t tcp_get_free_port() {
    uint16_t start_port = last_dynamic_port;
    
    // Increment and wrap if needed
    if (++last_dynamic_port > TCP_DYNAMIC_PORT_END || last_dynamic_port < TCP_DYNAMIC_PORT_START) {
        last_dynamic_port = TCP_DYNAMIC_PORT_START;
    }
    
    // First attempt with the current last_dynamic_port
    uint16_t port = last_dynamic_port;
    do {
        // Check if this port is in use
        int in_use = 0;
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (tcp_sockets[i].state != TCP_STATE_CLOSED && 
                tcp_sockets[i].local_port == port) {
                in_use = 1;
                break;
            }
        }
        
        if (!in_use) {
            // Found a free port
            return port;
        }
        
        // Try next port
        if (++port > TCP_DYNAMIC_PORT_END) {
            port = TCP_DYNAMIC_PORT_START;
        }
    } while (port != start_port); // Stop if we've checked all ports
    
    // No free port found
    return 0;
}

/**
 * Create a TCP socket
 */
tcp_socket_t* tcp_socket_create(const ipv4_address_t* local_addr, uint16_t local_port) {
    // Find a free socket slot
    tcp_socket_t* socket = NULL;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (tcp_sockets[i].state == TCP_STATE_CLOSED) {
            socket = &tcp_sockets[i];
            break;
        }
    }
    
    if (socket == NULL) {
        log_error("NET", "No free TCP socket slots");
        return NULL;
    }
    
    // Initialize the socket
    memset(socket, 0, sizeof(tcp_socket_t));
    socket->state = TCP_STATE_CLOSED;  // Will be changed by connect or listen
    
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
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (&tcp_sockets[i] == socket) {
                continue; // Skip the current socket
            }
            
            if (tcp_sockets[i].state != TCP_STATE_CLOSED && 
                tcp_sockets[i].local_port == local_port) {
                
                // Only consider it in use if the address is the same or one of them is ANY
                if (ip_addr_is_any(&tcp_sockets[i].local_addr) || 
                    ip_addr_is_any(&socket->local_addr) ||
                    ip_addr_cmp(&tcp_sockets[i].local_addr, &socket->local_addr) == 0) {
                    port_in_use = 1;
                    break;
                }
            }
        }
        
        if (port_in_use) {
            log_error("NET", "TCP port %u already in use", local_port);
            return NULL;
        }
        
        socket->local_port = local_port;
    } else {
        // Assign a dynamic port
        socket->local_port = tcp_get_free_port();
        if (socket->local_port == 0) {
            log_error("NET", "Failed to allocate a dynamic TCP port");
            return NULL;
        }
    }
    
    // Initialize connection parameters with defaults
    socket->conn.rcv_wnd = TCP_DEFAULT_WINDOW;
    socket->conn.mss = TCP_DEFAULT_MSS;
    
    // Initialize receive buffer
    if (tcp_buffer_init(socket, TCP_DEFAULT_BUFFER_SIZE) != 0) {
        log_error("NET", "Failed to initialize TCP receive buffer");
        return NULL;
    }
    
    return socket;
}

/**
 * Bind a TCP socket to a specific address and port
 */
int tcp_socket_bind(tcp_socket_t* socket, const ipv4_address_t* addr, uint16_t port) {
    if (socket == NULL) {
        log_error("NET", "Cannot bind NULL TCP socket");
        return -1;
    }
    
    if (socket->state != TCP_STATE_CLOSED) {
        log_error("NET", "Cannot bind TCP socket in non-CLOSED state");
        return -1;
    }
    
    // Check if the port is already in use
    if (port > 0) {
        int port_in_use = 0;
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (&tcp_sockets[i] == socket) {
                continue; // Skip the current socket
            }
            
            if (tcp_sockets[i].state != TCP_STATE_CLOSED && 
                tcp_sockets[i].local_port == port) {
                
                // Check address compatibility
                if (addr == NULL || ip_addr_is_any(&tcp_sockets[i].local_addr) ||
                    ip_addr_cmp(addr, &tcp_sockets[i].local_addr) == 0) {
                    port_in_use = 1;
                    break;
                }
            }
        }
        
        if (port_in_use) {
            log_error("NET", "TCP port %u already in use", port);
            return -1;
        }
        
        socket->local_port = port;
    } else if (socket->local_port == 0) {
        // Assign a dynamic port if none is set
        socket->local_port = tcp_get_free_port();
        if (socket->local_port == 0) {
            log_error("NET", "Failed to allocate a dynamic TCP port");
            return -1;
        }
    }
    
    // Set address if provided
    if (addr != NULL) {
        memcpy(&socket->local_addr, addr, sizeof(ipv4_address_t));
    }
    
    char addr_str[16];
    ipv4_to_str(&socket->local_addr, addr_str);
    log_info("NET", "Bound TCP socket to %s:%u", addr_str, socket->local_port);
    
    return 0;
}

/**
 * Start listening for connections on a socket
 */
int tcp_socket_listen(tcp_socket_t* socket, uint16_t backlog) {
    if (socket == NULL) {
        log_error("NET", "Cannot listen on NULL TCP socket");
        return -1;
    }
    
    if (socket->state != TCP_STATE_CLOSED) {
        log_error("NET", "Cannot listen on TCP socket in non-CLOSED state");
        return -1;
    }
    
    // Allocate listener structure
    tcp_listener_t* listener = (tcp_listener_t*)malloc(sizeof(tcp_listener_t));
    if (listener == NULL) {
        log_error("NET", "Failed to allocate TCP listener");
        return -1;
    }
    
    // Initialize listener
    listener->backlog = backlog > 0 ? backlog : 1;
    listener->pending_count = 0;
    listener->pending_connections = NULL;
    
    // Set the socket to listening state
    socket->state = TCP_STATE_LISTEN;
    socket->listener = listener;
    
    char addr_str[16];
    ipv4_to_str(&socket->local_addr, addr_str);
    log_info("NET", "TCP socket listening on %s:%u with backlog %u",
            addr_str, socket->local_port, backlog);
    
    return 0;
}

/**
 * Accept a connection on a listening socket
 */
tcp_socket_t* tcp_socket_accept(tcp_socket_t* socket) {
    if (socket == NULL) {
        log_error("NET", "Cannot accept on NULL TCP socket");
        return NULL;
    }
    
    if (socket->state != TCP_STATE_LISTEN || socket->listener == NULL) {
        log_error("NET", "Cannot accept on non-listening TCP socket");
        return NULL;
    }
    
    // Check if there are any pending connections
    if (socket->listener->pending_connections == NULL) {
        return NULL; // No pending connections
    }
    
    // Get the first pending connection
    tcp_socket_t* accepted = socket->listener->pending_connections;
    socket->listener->pending_connections = accepted->next;
    accepted->next = NULL;
    socket->listener->pending_count--;
    
    // Move the socket to ESTABLISHED state if it was in SYN_RECEIVED
    if (accepted->state == TCP_STATE_SYN_RECEIVED) {
        accepted->state = TCP_STATE_ESTABLISHED;
    }
    
    char addr_str[16];
    ipv4_to_str(&accepted->remote_addr, addr_str);
    log_info("NET", "Accepted TCP connection from %s:%u",
            addr_str, accepted->remote_port);
    
    return accepted;
}

/**
 * Connect to a remote host
 */
int tcp_socket_connect(tcp_socket_t* socket, const ipv4_address_t* addr, uint16_t port) {
    if (socket == NULL || addr == NULL) {
        log_error("NET", "Invalid parameters for tcp_socket_connect");
        return -1;
    }
    
    if (socket->state != TCP_STATE_CLOSED) {
        log_error("NET", "Cannot connect TCP socket in non-CLOSED state");
        return -1;
    }
    
    // Set remote address and port
    memcpy(&socket->remote_addr, addr, sizeof(ipv4_address_t));
    socket->remote_port = port;
    
    // If local address is not set, use default
    if (ip_addr_is_any(&socket->local_addr)) {
        // In a full implementation, we would look up the appropriate
        // local address based on routing information
        net_device_t* dev = network_get_default_device();
        if (dev != NULL) {
            memcpy(&socket->local_addr, &dev->ip, sizeof(ipv4_address_t));
        } else {
            log_error("NET", "No default network device");
            return -1;
        }
    }
    
    // If local port is not set, use a dynamic one
    if (socket->local_port == 0) {
        socket->local_port = tcp_get_free_port();
        if (socket->local_port == 0) {
            log_error("NET", "Failed to allocate a dynamic TCP port");
            return -1;
        }
    }
    
    // Initialize sequence numbers
    socket->conn.snd_una = tcp_initial_seq;
    socket->conn.snd_nxt = tcp_initial_seq;
    socket->conn.rcv_nxt = 0; // Will be set when we receive a SYN
    
    // Move to SYN_SENT state
    socket->state = TCP_STATE_SYN_SENT;
    
    // Initialize retransmission parameters
    socket->conn.retransmit.rto = TCP_RETRANSMIT_TIMEOUT;
    socket->conn.retransmit.attempts = 0;
    
    // Initialize receive buffer
    if (tcp_buffer_init(socket, TCP_DEFAULT_BUFFER_SIZE) != 0) {
        log_error("NET", "Failed to initialize TCP receive buffer");
        return -1;
    }
    
    char addr_str[16];
    ipv4_to_str(addr, addr_str);
    log_info("NET", "Connecting to %s:%u", addr_str, port);
    
    // Send SYN
    return tcp_send_segment(socket, TCP_FLAG_SYN, NULL, 0);
}

/**
 * Close a TCP socket
 */
int tcp_socket_close(tcp_socket_t* socket) {
    if (socket == NULL) {
        log_error("NET", "Cannot close NULL TCP socket");
        return -1;
    }
    
    // Check current state and perform appropriate action
    switch (socket->state) {
        case TCP_STATE_CLOSED:
            // Already closed
            return 0;
            
        case TCP_STATE_LISTEN:
            // Free listener data
            if (socket->listener != NULL) {
                // We should properly clean up any pending connections as well
                free(socket->listener);
                socket->listener = NULL;
            }
            socket->state = TCP_STATE_CLOSED;
            return 0;
            
        case TCP_STATE_SYN_SENT:
            // Connection attempt not completed, just close
            socket->state = TCP_STATE_CLOSED;
            return 0;
            
        case TCP_STATE_SYN_RECEIVED:
            // Send FIN and move to FIN_WAIT_1
            socket->state = TCP_STATE_FIN_WAIT_1;
            return tcp_send_segment(socket, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            
        case TCP_STATE_ESTABLISHED:
            // Send FIN and move to FIN_WAIT_1
            socket->state = TCP_STATE_FIN_WAIT_1;
            return tcp_send_segment(socket, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            
        case TCP_STATE_CLOSE_WAIT:
            // Send FIN and move to LAST_ACK
            socket->state = TCP_STATE_LAST_ACK;
            return tcp_send_segment(socket, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            
        default:
            // Other states shouldn't call close
            log_warning("NET", "tcp_socket_close called in invalid state %s",
                      tcp_state_to_str(socket->state));
            return -1;
    }
}

/**
 * Send data through a TCP socket
 */
int tcp_socket_send(tcp_socket_t* socket, const void* data, size_t len) {
    if (socket == NULL || data == NULL) {
        log_error("NET", "Invalid parameters for tcp_socket_send");
        return -1;
    }
    
    if (socket->state != TCP_STATE_ESTABLISHED) {
        log_error("NET", "Cannot send on TCP socket in non-ESTABLISHED state");
        return -1;
    }
    
    // In a full implementation, we'd buffer the data and send based on window size
    // For this implementation, we'll just send it directly
    
    // Send data with the PSH and ACK flags
    int result = tcp_send_segment(socket, TCP_FLAG_PSH | TCP_FLAG_ACK, data, len);
    
    // Notify sent callback if registered
    if (result == 0 && socket->sent_callback) {
        socket->sent_callback(socket, len);
    }
    
    return result == 0 ? len : result;
}

/**
 * Receive data from a TCP socket
 */
int tcp_socket_recv(tcp_socket_t* socket, void* buffer, size_t len) {
    if (socket == NULL || buffer == NULL) {
        log_error("NET", "Invalid parameters for tcp_socket_recv");
        return -1;
    }
    
    if (socket->state != TCP_STATE_ESTABLISHED && 
        socket->state != TCP_STATE_FIN_WAIT_1 &&
        socket->state != TCP_STATE_FIN_WAIT_2 &&
        socket->state != TCP_STATE_CLOSE_WAIT) {
        log_error("NET", "Cannot receive on TCP socket in state %s",
                 tcp_state_to_str(socket->state));
        return -1;
    }
    
    // Try to read data from the receive buffer
    int bytes_read = tcp_buffer_read(socket, buffer, len);
    
    // Update the receive window if needed
    if (bytes_read > 0) {
        // In a more sophisticated implementation, we would adjust the receive window
        // based on buffer space and send a window update if necessary
    }
    
    return bytes_read;
}

/**
 * Check if there's data available to read from a TCP socket
 */
int tcp_socket_available(tcp_socket_t* socket) {
    if (socket == NULL) {
        return -1;
    }
    
    return socket->rx_buffer.bytes_available;
}

/**
 * Get a string representation of a TCP state
 */
const char* tcp_state_to_str(tcp_state_t state) {
    switch (state) {
        case TCP_STATE_CLOSED:       return "CLOSED";
        case TCP_STATE_LISTEN:       return "LISTEN";
        case TCP_STATE_SYN_SENT:     return "SYN_SENT";
        case TCP_STATE_SYN_RECEIVED: return "SYN_RECEIVED";
        case TCP_STATE_ESTABLISHED:  return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT_1:   return "FIN_WAIT_1";
        case TCP_STATE_FIN_WAIT_2:   return "FIN_WAIT_2";
        case TCP_STATE_CLOSING:      return "CLOSING";
        case TCP_STATE_TIME_WAIT:    return "TIME_WAIT";
        case TCP_STATE_CLOSE_WAIT:   return "CLOSE_WAIT";
        case TCP_STATE_LAST_ACK:     return "LAST_ACK";
        default:                    return "UNKNOWN";
    }
}

/**
 * Process TCP timers
 */
void tcp_timer(uint32_t msec) {
    uint32_t current_time = network_get_time_ms();

    // Process TIME_WAIT timers
    for (int i = 0; i < TCP_MAX_TIME_WAIT_SOCKETS; i++) {
        if (time_wait_sockets[i].socket != NULL) {
            // Check if TIME_WAIT period has expired
            if (current_time - time_wait_sockets[i].start_time >= time_wait_sockets[i].duration) {
                char addr_str[16];
                ipv4_to_str(&time_wait_sockets[i].socket->remote_addr, addr_str);
                log_info("NET", "TCP TIME_WAIT expired for connection to %s:%u",
                      addr_str, time_wait_sockets[i].socket->remote_port);
                
                // Close the socket
                time_wait_sockets[i].socket->state = TCP_STATE_CLOSED;
                
                // Free the slot
                time_wait_sockets[i].socket = NULL;
            }
        }
    }
    
    // Iterate through all TCP sockets
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t* socket = &tcp_sockets[i];
        
        // Skip closed sockets
        if (socket->state == TCP_STATE_CLOSED) {
            continue;
        }
        
        // Handle retransmissions for SYN_SENT, SYN_RECEIVED, and ESTABLISHED states
        if (socket->state == TCP_STATE_SYN_SENT || 
            socket->state == TCP_STATE_SYN_RECEIVED ||
            socket->state == TCP_STATE_ESTABLISHED) {
            
            // Check if retransmission timer expired
            if (socket->conn.retransmit.timer > 0) {
                socket->conn.retransmit.timer -= msec;
                
                if (socket->conn.retransmit.timer <= 0) {
                    // Timer expired, retransmit
                    socket->conn.retransmit.attempts++;
                    socket->conn.retransmit.rto *= 2; // Exponential backoff
                    
                    if (socket->conn.retransmit.attempts > TCP_MAX_RETRANSMITS) {
                        log_warning("NET", "TCP connection timed out after %d attempts",
                                  socket->conn.retransmit.attempts);
                        
                        // Reset the connection
                        socket->state = TCP_STATE_CLOSED;
                        
                        // Notify the application
                        if (socket->closed_callback) {
                            socket->closed_callback(socket);
                        }
                    } else {
                        // Retransmit the last segment
                        // In a full implementation, we would retransmit from a queue
                        log_debug("NET", "TCP retransmitting (attempt %d/%d)",
                                socket->conn.retransmit.attempts, TCP_MAX_RETRANSMITS);
                        
                        // Reset the timer for next attempt
                        socket->conn.retransmit.timer = socket->conn.retransmit.rto;
                    }
                }
            }
        }
    }
}

/**
 * Register callbacks for a TCP socket
 */
int tcp_socket_register_callbacks(tcp_socket_t* socket,
                                void (*connected_callback)(tcp_socket_t*),
                                void (*data_ready_callback)(tcp_socket_t*, size_t),
                                void (*sent_callback)(tcp_socket_t*, size_t),
                                void (*closed_callback)(tcp_socket_t*)) {
    if (socket == NULL) {
        log_error("NET", "Cannot register callbacks for NULL TCP socket");
        return -1;
    }
    
    socket->connected_callback = connected_callback;
    socket->data_ready_callback = data_ready_callback;
    socket->sent_callback = sent_callback;
    socket->closed_callback = closed_callback;
    
    return 0;
}

/**
 * Set options on a TCP socket
 */
int tcp_socket_set_options(tcp_socket_t* socket, uint8_t options) {
    if (socket == NULL) {
        log_error("NET", "Cannot set options for NULL TCP socket");
        return -1;
    }
    
    socket->options = options;
    return 0;
}