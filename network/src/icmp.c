/**
 * @file icmp.c
 * @brief ICMP protocol implementation for uintOS
 * 
 * This file implements the Internet Control Message Protocol (ICMP)
 * for the uintOS network stack.
 */

#include <string.h>
#include "../include/icmp.h"
#include "../include/ip.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"

// Echo reply callback function pointer
static void (*echo_reply_callback)(const ipv4_address_t* src, 
                                  uint16_t id, uint16_t seq, 
                                  const void* data, size_t data_len) = NULL;

/**
 * Initialize the ICMP protocol handler
 */
int icmp_init() {
    log_info("NET", "Initializing ICMP protocol handler");
    
    // Register ICMP as a protocol handler with IP
    int result = ip_register_protocol(IP_PROTO_ICMP, icmp_rx);
    if (result != 0) {
        log_error("NET", "Failed to register ICMP with IP protocol handler");
        return result;
    }
    
    log_info("NET", "ICMP protocol handler initialized successfully");
    return 0;
}

/**
 * Calculate ICMP checksum
 */
uint16_t icmp_checksum(const void* data, size_t len) {
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;
    
    // Sum up 16-bit words
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Add left-over byte if any
    if (len > 0) {
        sum += *(const uint8_t*)ptr;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)(~sum);
}

/**
 * Handle an ICMP Echo Request
 */
static int icmp_handle_echo_request(net_buffer_t* buffer, const ipv4_address_t* src, const ipv4_address_t* dest) {
    // Check buffer size
    if (buffer->len < sizeof(icmp_header_t)) {
        log_warning("NET", "ICMP echo request too short");
        return -1;
    }
    
    icmp_header_t* icmp = (icmp_header_t*)buffer->data;
    uint16_t id = icmp->un.echo.identifier;
    uint16_t seq = icmp->un.echo.sequence;
    size_t data_len = buffer->len - sizeof(icmp_header_t);
    
    // Log the echo request
    char src_str[16];
    ipv4_to_str(src, src_str);
    log_debug("NET", "Received ICMP Echo Request from %s, id=%u, seq=%u, data_len=%u",
             src_str, id, seq, data_len);
    
    // Allocate a reply buffer with the same size
    net_buffer_t* reply = net_buffer_alloc(buffer->len, 0);
    if (!reply) {
        log_error("NET", "Failed to allocate ICMP echo reply buffer");
        return -1;
    }
    
    // Copy the original packet
    memcpy(reply->data, buffer->data, buffer->len);
    reply->len = buffer->len;
    
    // Modify the header to make it a reply
    icmp_header_t* reply_icmp = (icmp_header_t*)reply->data;
    reply_icmp->type = ICMP_TYPE_ECHO_REPLY;
    reply_icmp->code = 0;
    reply_icmp->checksum = 0;
    
    // Calculate the checksum
    reply_icmp->checksum = icmp_checksum(reply_icmp, reply->len);
    
    // Send the echo reply
    int result = ip_tx(reply, NULL, src, IP_PROTO_ICMP, 64);
    
    // Free the buffer
    net_buffer_free(reply);
    
    return result;
}

/**
 * Process an incoming ICMP packet
 */
int icmp_rx(net_buffer_t* buffer) {
    if (buffer == NULL) {
        log_error("NET", "Cannot process NULL ICMP buffer");
        return -1;
    }
    
    if (buffer->len < sizeof(icmp_header_t)) {
        log_warning("NET", "ICMP packet too short");
        return -1;
    }
    
    icmp_header_t* icmp = (icmp_header_t*)buffer->data;
    
    // Verify the checksum
    uint16_t checksum = icmp->checksum;
    icmp->checksum = 0;
    
    uint16_t calculated = icmp_checksum(icmp, buffer->len);
    if (checksum != calculated) {
        log_warning("NET", "ICMP checksum verification failed");
        icmp->checksum = checksum; // Restore original
        return -1;
    }
    
    // Restore the original checksum
    icmp->checksum = checksum;
    
    // Get source and destination IP addresses from the buffer context
    ipv4_address_t* src_ip = (ipv4_address_t*)buffer->protocol_data;
    ipv4_address_t* dest_ip = src_ip + 1; // Assumes IP handler stores src and dest consecutively
    
    // Process the ICMP packet based on its type
    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REQUEST:
            return icmp_handle_echo_request(buffer, src_ip, dest_ip);
            
        case ICMP_TYPE_ECHO_REPLY:
            if (echo_reply_callback) {
                echo_reply_callback(src_ip, 
                                   icmp->un.echo.identifier, 
                                   icmp->un.echo.sequence,
                                   (buffer->data + sizeof(icmp_header_t)),
                                   buffer->len - sizeof(icmp_header_t));
            }
            return 0;
            
        case ICMP_TYPE_DEST_UNREACHABLE:
            log_warning("NET", "ICMP Destination Unreachable received: code=%u", icmp->code);
            return 0;
            
        case ICMP_TYPE_TIME_EXCEEDED:
            log_warning("NET", "ICMP Time Exceeded received: code=%u", icmp->code);
            return 0;
            
        default:
            log_debug("NET", "Unhandled ICMP type: %u, code: %u", icmp->type, icmp->code);
            return 0;
    }
}

/**
 * Send an ICMP packet
 */
int icmp_tx(uint8_t type, uint8_t code, const void* data, size_t data_len, const ipv4_address_t* dest) {
    if (dest == NULL) {
        log_error("NET", "Cannot send ICMP packet to NULL destination");
        return -1;
    }
    
    // Allocate a buffer for the ICMP packet
    size_t icmp_size = sizeof(icmp_header_t) + data_len;
    net_buffer_t* buffer = ip_alloc_packet(icmp_size);
    if (!buffer) {
        log_error("NET", "Failed to allocate ICMP packet buffer");
        return -1;
    }
    
    // Fill the ICMP header
    icmp_header_t* icmp = (icmp_header_t*)buffer->data;
    icmp->type = type;
    icmp->code = code;
    icmp->checksum = 0;
    
    // Set common fields based on type
    switch (type) {
        case ICMP_TYPE_ECHO_REQUEST:
        case ICMP_TYPE_ECHO_REPLY:
        case ICMP_TYPE_TIMESTAMP_REQUEST:
        case ICMP_TYPE_TIMESTAMP_REPLY:
        case ICMP_TYPE_INFO_REQUEST:
        case ICMP_TYPE_INFO_REPLY:
            // For messages with identifier and sequence number
            icmp->un.echo.identifier = 0; // Will be set by caller
            icmp->un.echo.sequence = 0;   // Will be set by caller
            break;
            
        default:
            // For other types
            icmp->un.unused = 0;
    }
    
    // Copy additional data if provided
    if (data != NULL && data_len > 0) {
        memcpy(buffer->data + sizeof(icmp_header_t), data, data_len);
    }
    
    // Calculate the checksum
    icmp->checksum = icmp_checksum(icmp, icmp_size);
    
    // Set the buffer length
    buffer->len = icmp_size;
    
    // Send the ICMP packet
    int result = ip_tx(buffer, NULL, dest, IP_PROTO_ICMP, 64);
    
    // Free the buffer
    net_buffer_free(buffer);
    
    return result;
}

/**
 * Send an ICMP echo request (ping)
 */
int icmp_ping(const ipv4_address_t* dest, uint16_t id, uint16_t seq, const void* data, size_t data_len) {
    if (dest == NULL) {
        log_error("NET", "Cannot ping NULL destination");
        return -1;
    }
    
    // Allocate a buffer for the ICMP packet
    size_t icmp_size = sizeof(icmp_header_t) + data_len;
    net_buffer_t* buffer = ip_alloc_packet(icmp_size);
    if (!buffer) {
        log_error("NET", "Failed to allocate ICMP ping buffer");
        return -1;
    }
    
    // Fill the ICMP header
    icmp_header_t* icmp = (icmp_header_t*)buffer->data;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.identifier = id;
    icmp->un.echo.sequence = seq;
    
    // Copy additional data if provided
    if (data != NULL && data_len > 0) {
        memcpy(buffer->data + sizeof(icmp_header_t), data, data_len);
    }
    
    // Calculate the checksum
    icmp->checksum = icmp_checksum(icmp, icmp_size);
    
    // Set the buffer length
    buffer->len = icmp_size;
    
    // Log the ping
    char dest_str[16];
    ipv4_to_str(dest, dest_str);
    log_info("NET", "Sending ICMP Echo Request (ping) to %s, id=%u, seq=%u", dest_str, id, seq);
    
    // Send the ICMP packet
    int result = ip_tx(buffer, NULL, dest, IP_PROTO_ICMP, 64);
    
    // Free the buffer
    net_buffer_free(buffer);
    
    return result;
}

/**
 * Register a callback for ICMP echo reply reception
 */
int icmp_register_echo_reply_callback(void (*callback)(const ipv4_address_t* src, 
                                    uint16_t id, uint16_t seq, 
                                    const void* data, size_t data_len)) {
    echo_reply_callback = callback;
    return 0;
}

/**
 * Get a string representation of an ICMP message type
 */
const char* icmp_type_to_str(uint8_t type) {
    switch (type) {
        case ICMP_TYPE_ECHO_REPLY:          return "Echo Reply";
        case ICMP_TYPE_DEST_UNREACHABLE:    return "Destination Unreachable";
        case ICMP_TYPE_SOURCE_QUENCH:       return "Source Quench";
        case ICMP_TYPE_REDIRECT:            return "Redirect";
        case ICMP_TYPE_ECHO_REQUEST:        return "Echo Request";
        case ICMP_TYPE_TIME_EXCEEDED:       return "Time Exceeded";
        case ICMP_TYPE_PARAM_PROBLEM:       return "Parameter Problem";
        case ICMP_TYPE_TIMESTAMP_REQUEST:   return "Timestamp Request";
        case ICMP_TYPE_TIMESTAMP_REPLY:     return "Timestamp Reply";
        case ICMP_TYPE_INFO_REQUEST:        return "Information Request";
        case ICMP_TYPE_INFO_REPLY:          return "Information Reply";
        default:                            return "Unknown";
    }
}

/**
 * Get a string representation of an ICMP code for a given message type
 */
const char* icmp_code_to_str(uint8_t type, uint8_t code) {
    switch (type) {
        case ICMP_TYPE_DEST_UNREACHABLE:
            switch (code) {
                case ICMP_CODE_NET_UNREACHABLE:      return "Network Unreachable";
                case ICMP_CODE_HOST_UNREACHABLE:     return "Host Unreachable";
                case ICMP_CODE_PROTO_UNREACHABLE:    return "Protocol Unreachable";
                case ICMP_CODE_PORT_UNREACHABLE:     return "Port Unreachable";
                case ICMP_CODE_FRAG_NEEDED:          return "Fragmentation Needed";
                case ICMP_CODE_SOURCE_ROUTE_FAILED:  return "Source Route Failed";
                case ICMP_CODE_DEST_NET_UNKNOWN:     return "Destination Network Unknown";
                case ICMP_CODE_DEST_HOST_UNKNOWN:    return "Destination Host Unknown";
                case ICMP_CODE_SOURCE_HOST_ISOLATED: return "Source Host Isolated";
                case ICMP_CODE_NET_PROHIBITED:       return "Network Administratively Prohibited";
                case ICMP_CODE_HOST_PROHIBITED:      return "Host Administratively Prohibited";
                case ICMP_CODE_NET_TOS:              return "Network Unreachable for TOS";
                case ICMP_CODE_HOST_TOS:             return "Host Unreachable for TOS";
                case ICMP_CODE_COMM_PROHIBITED:      return "Communication Administratively Prohibited";
                case ICMP_CODE_HOST_PRECEDENCE:      return "Host Precedence Violation";
                case ICMP_CODE_PRECEDENCE_CUTOFF:    return "Precedence Cutoff in Effect";
                default:                             return "Unknown";
            }
            
        case ICMP_TYPE_REDIRECT:
            switch (code) {
                case 0: return "Redirect for Network";
                case 1: return "Redirect for Host";
                case 2: return "Redirect for TOS and Network";
                case 3: return "Redirect for TOS and Host";
                default: return "Unknown";
            }
            
        case ICMP_TYPE_TIME_EXCEEDED:
            switch (code) {
                case 0: return "TTL Expired in Transit";
                case 1: return "Fragment Reassembly Time Exceeded";
                default: return "Unknown";
            }
            
        case ICMP_TYPE_PARAM_PROBLEM:
            switch (code) {
                case 0: return "Pointer Indicates Error";
                case 1: return "Missing Required Option";
                case 2: return "Bad Length";
                default: return "Unknown";
            }
            
        default:
            return "N/A";
    }
}