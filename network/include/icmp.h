/**
 * @file icmp.h
 * @brief ICMP protocol implementation for uintOS
 * 
 * This file defines the Internet Control Message Protocol (ICMP)
 * for the uintOS network stack.
 */

#ifndef ICMP_H
#define ICMP_H

#include "network.h"
#include "ip.h"

/**
 * ICMP message types
 */
#define ICMP_TYPE_ECHO_REPLY          0   // Echo Reply
#define ICMP_TYPE_DEST_UNREACHABLE    3   // Destination Unreachable
#define ICMP_TYPE_SOURCE_QUENCH       4   // Source Quench
#define ICMP_TYPE_REDIRECT            5   // Redirect
#define ICMP_TYPE_ECHO_REQUEST        8   // Echo Request
#define ICMP_TYPE_TIME_EXCEEDED       11  // Time Exceeded
#define ICMP_TYPE_PARAM_PROBLEM       12  // Parameter Problem
#define ICMP_TYPE_TIMESTAMP_REQUEST   13  // Timestamp Request
#define ICMP_TYPE_TIMESTAMP_REPLY     14  // Timestamp Reply
#define ICMP_TYPE_INFO_REQUEST        15  // Information Request
#define ICMP_TYPE_INFO_REPLY          16  // Information Reply

/**
 * ICMP destination unreachable codes
 */
#define ICMP_CODE_NET_UNREACHABLE     0   // Network Unreachable
#define ICMP_CODE_HOST_UNREACHABLE    1   // Host Unreachable
#define ICMP_CODE_PROTO_UNREACHABLE   2   // Protocol Unreachable
#define ICMP_CODE_PORT_UNREACHABLE    3   // Port Unreachable
#define ICMP_CODE_FRAG_NEEDED         4   // Fragmentation Needed and Don't Fragment was Set
#define ICMP_CODE_SOURCE_ROUTE_FAILED 5   // Source Route Failed
#define ICMP_CODE_DEST_NET_UNKNOWN    6   // Destination Network Unknown
#define ICMP_CODE_DEST_HOST_UNKNOWN   7   // Destination Host Unknown
#define ICMP_CODE_SOURCE_HOST_ISOLATED 8  // Source Host Isolated
#define ICMP_CODE_NET_PROHIBITED      9   // Network Administratively Prohibited
#define ICMP_CODE_HOST_PROHIBITED     10  // Host Administratively Prohibited
#define ICMP_CODE_NET_TOS             11  // Network Unreachable for TOS
#define ICMP_CODE_HOST_TOS            12  // Host Unreachable for TOS
#define ICMP_CODE_COMM_PROHIBITED     13  // Communication Administratively Prohibited
#define ICMP_CODE_HOST_PRECEDENCE     14  // Host Precedence Violation
#define ICMP_CODE_PRECEDENCE_CUTOFF   15  // Precedence Cutoff in Effect

/**
 * ICMP header structure
 */
typedef struct icmp_header {
    uint8_t  type;         // ICMP message type
    uint8_t  code;         // ICMP message code
    uint16_t checksum;     // Checksum for the ICMP header and data
    union {
        uint32_t unused;   // Unused in most message types
        
        // Echo or Echo Reply
        struct {
            uint16_t identifier;    // Identifier
            uint16_t sequence;      // Sequence number
        } echo;
        
        // Redirect
        struct {
            uint32_t gateway;       // Gateway address
        } redirect;
        
        // Destination Unreachable, Source Quench, Time Exceeded, Parameter Problem
        struct {
            uint32_t unused;        // Unused
            // Following the header is the IP header plus 8 bytes of original datagram
        } fail;
        
        // Parameter Problem
        struct {
            uint8_t  pointer;       // Pointer to the error
            uint8_t  unused[3];     // Unused
        } param;
        
        // Timestamp or Timestamp Reply
        struct {
            uint16_t identifier;    // Identifier
            uint16_t sequence;      // Sequence number
            uint32_t originate;     // Originate timestamp
            uint32_t receive;       // Receive timestamp
            uint32_t transmit;      // Transmit timestamp
        } timestamp;
    } un;
} __attribute__((packed)) icmp_header_t;

/**
 * Initialize the ICMP protocol handler
 * 
 * @return 0 on success, error code on failure
 */
int icmp_init();

/**
 * Process an incoming ICMP packet
 * 
 * @param buffer Network buffer containing the ICMP packet
 * @return 0 on success, error code on failure
 */
int icmp_rx(net_buffer_t* buffer);

/**
 * Send an ICMP packet
 * 
 * @param type ICMP message type
 * @param code ICMP message code
 * @param data Additional data to include in the message
 * @param data_len Length of the additional data
 * @param dest Destination IP address
 * @return 0 on success, error code on failure
 */
int icmp_tx(uint8_t type, uint8_t code, const void* data, 
            size_t data_len, const ipv4_address_t* dest);

/**
 * Send an ICMP echo request (ping)
 * 
 * @param dest Destination IP address
 * @param id Identifier for the echo request
 * @param seq Sequence number for the echo request
 * @param data Optional data to include in the echo request
 * @param data_len Length of the optional data
 * @return 0 on success, error code on failure
 */
int icmp_ping(const ipv4_address_t* dest, uint16_t id, uint16_t seq,
             const void* data, size_t data_len);

/**
 * Calculate the ICMP checksum
 * 
 * @param data Pointer to the data (including ICMP header)
 * @param len Length of the data
 * @return The calculated checksum
 */
uint16_t icmp_checksum(const void* data, size_t len);

/**
 * Register a callback for ICMP echo reply reception (ping response)
 * 
 * @param callback Function to call when an echo reply is received
 * @return 0 on success, error code on failure
 */
int icmp_register_echo_reply_callback(void (*callback)(const ipv4_address_t* src, 
                                    uint16_t id, uint16_t seq, 
                                    const void* data, size_t data_len));

/**
 * Get a string representation of an ICMP message type
 * 
 * @param type ICMP message type
 * @return String representation or "UNKNOWN" if not recognized
 */
const char* icmp_type_to_str(uint8_t type);

/**
 * Get a string representation of an ICMP code for a given message type
 * 
 * @param type ICMP message type
 * @param code ICMP message code
 * @return String representation or "UNKNOWN" if not recognized
 */
const char* icmp_code_to_str(uint8_t type, uint8_t code);

#endif /* ICMP_H */