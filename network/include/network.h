/**
 * @file network.h
 * @brief Core networking definitions for uintOS
 * 
 * This file defines the core data structures and functions for the
 * uintOS network stack.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stddef.h>

/**
 * Maximum number of network devices supported
 */
#define NET_MAX_DEVICES 8

/**
 * MAC address type
 */
typedef struct mac_address {
    uint8_t addr[6];
} __attribute__((packed)) mac_address_t;

/**
 * IPv4 address type
 */
typedef struct ipv4_address {
    uint8_t addr[4];
} __attribute__((packed)) ipv4_address_t;

/**
 * Network error codes
 */
#define NET_ERR_OK       0      // No error
#define NET_ERR_INVALID -1      // Invalid parameter
#define NET_ERR_NOMEM   -2      // Out of memory
#define NET_ERR_NOPROTO -3      // Protocol not available
#define NET_ERR_BUSY    -4      // Resource busy
#define NET_ERR_TIMEOUT -5      // Operation timed out

/**
 * Network protocol identifiers
 */
#define NET_PROTO_NONE 0        // No protocol
#define NET_PROTO_ETH  1        // Ethernet
#define NET_PROTO_ARP  2        // ARP
#define NET_PROTO_IP   3        // IPv4
#define NET_PROTO_TCP  4        // TCP
#define NET_PROTO_UDP  5        // UDP
#define NET_PROTO_ICMP 6        // ICMP

/**
 * Network buffer flags
 */
#define NET_BUF_FLAG_NONE       0x00
#define NET_BUF_FLAG_ALLOC      0x01    // Buffer is allocated (free on release)
#define NET_BUF_FLAG_BROADCAST  0x02    // Broadcast packet
#define NET_BUF_FLAG_MULTICAST  0x04    // Multicast packet

/**
 * Network device flags
 */
#define NET_DEV_FLAG_NONE      0x00
#define NET_DEV_FLAG_UP        0x01    // Device is up
#define NET_DEV_FLAG_LOOPBACK  0x02    // Loopback device
#define NET_DEV_FLAG_BROADCAST 0x04    // Device supports broadcast
#define NET_DEV_FLAG_MULTICAST 0x08    // Device supports multicast
#define NET_DEV_FLAG_PROMISC   0x10    // Device in promiscuous mode

// Forward declarations
struct net_device;
struct net_buffer;

/**
 * Network buffer structure
 */
typedef struct net_buffer {
    uint8_t* data;              // Pointer to the current data position
    size_t len;                 // Length of data in the buffer
    size_t size;                // Total size of the buffer
    size_t offset;              // Offset from the start of allocation
    uint8_t flags;              // Buffer flags
    uint8_t protocol;           // Protocol identifier
    void* protocol_data;        // Protocol-specific data
    struct net_device* device;  // Associated network device
    struct net_buffer* next;    // Next buffer in a chain
} net_buffer_t;

/**
 * Network device statistics
 */
typedef struct net_device_stats {
    uint64_t rx_packets;        // Received packets
    uint64_t tx_packets;        // Transmitted packets
    uint64_t rx_bytes;          // Received bytes
    uint64_t tx_bytes;          // Transmitted bytes
    uint64_t rx_errors;         // Receive errors
    uint64_t tx_errors;         // Transmit errors
    uint64_t rx_dropped;        // Dropped incoming packets
    uint64_t tx_dropped;        // Dropped outgoing packets
    uint64_t collisions;        // Detected collisions
} net_device_stats_t;

/**
 * Network device operations
 */
typedef struct net_device_ops {
    int (*open)(struct net_device* dev);
    int (*close)(struct net_device* dev);
    int (*transmit)(struct net_device* dev, struct net_buffer* buffer);
    int (*set_mac)(struct net_device* dev, const mac_address_t* mac);
    int (*set_mtu)(struct net_device* dev, uint16_t mtu);
    int (*set_flags)(struct net_device* dev, uint32_t flags);
} net_device_ops_t;

/**
 * Network device structure
 */
typedef struct net_device {
    char name[16];              // Device name (e.g., "eth0")
    uint32_t flags;             // Device flags
    mac_address_t mac;          // MAC address
    ipv4_address_t ip;          // IPv4 address
    ipv4_address_t netmask;     // Subnet mask
    ipv4_address_t gateway;     // Default gateway
    uint16_t mtu;               // Maximum Transmission Unit
    uint16_t type;              // Hardware type
    net_device_ops_t ops;       // Device operations
    net_device_stats_t stats;   // Device statistics
    void* priv;                 // Device private data
} net_device_t;

/**
 * Initialize the network stack
 * 
 * @return 0 on success, error code on failure
 */
int network_init();

/**
 * Register a network device with the stack
 * 
 * @param dev Network device to register
 * @return 0 on success, error code on failure
 */
int network_register_device(net_device_t* dev);

/**
 * Find a network device by name
 * 
 * @param name Device name to find
 * @return Pointer to the device or NULL if not found
 */
net_device_t* network_find_device_by_name(const char* name);

/**
 * Find a network device by its IP address
 * 
 * @param ip IP address to find
 * @return Pointer to the device or NULL if not found
 */
net_device_t* network_find_device_by_ip(const ipv4_address_t* ip);

/**
 * Get the default network device
 * 
 * @return Pointer to the default device or NULL if none
 */
net_device_t* network_get_default_device();

/**
 * Set the default network device
 * 
 * @param dev Network device to set as default
 */
void network_set_default_device(net_device_t* dev);

/**
 * Process incoming network packet
 * 
 * @param dev Device that received the packet
 * @param data Packet data
 * @param len Packet length
 * @return 0 on success, error code on failure
 */
int network_receive_packet(net_device_t* dev, const void* data, size_t len);

/**
 * Allocate a network buffer
 * 
 * @param size Size of the buffer to allocate
 * @param reserve_header Space to reserve for headers
 * @return Pointer to the new buffer or NULL on failure
 */
net_buffer_t* net_buffer_alloc(size_t size, size_t reserve_header);

/**
 * Free a network buffer
 * 
 * @param buffer Buffer to free
 */
void net_buffer_free(net_buffer_t* buffer);

/**
 * Add data to the start of a network buffer (prepend)
 * 
 * @param buffer Network buffer
 * @param len Length of data to add
 * @return Pointer to the new data or NULL on failure
 */
void* net_buffer_push(net_buffer_t* buffer, size_t len);

/**
 * Remove data from the start of a network buffer
 * 
 * @param buffer Network buffer
 * @param len Length of data to remove
 * @return Pointer to the new data or NULL on failure
 */
void* net_buffer_pull(net_buffer_t* buffer, size_t len);

/**
 * Reserve headroom in a network buffer
 * 
 * @param buffer Network buffer
 * @param len Length of headroom to reserve
 * @return 0 on success, error code on failure
 */
int net_buffer_reserve(net_buffer_t* buffer, size_t len);

/**
 * Add data to the end of a network buffer (append)
 * 
 * @param buffer Network buffer
 * @param data Data to append
 * @param len Length of data
 * @return 0 on success, error code on failure
 */
int net_buffer_append(net_buffer_t* buffer, const void* data, size_t len);

/**
 * Convert an IPv4 address to string format
 * 
 * @param ip IP address to convert
 * @param buffer Buffer to store the formatted string (needs at least 16 bytes)
 * @return Pointer to the buffer
 */
char* ipv4_to_str(const ipv4_address_t* ip, char* buffer);

/**
 * Parse an IPv4 address from a string
 * 
 * @param str String containing the IP address in format "xxx.xxx.xxx.xxx"
 * @param ip Pointer to store the parsed IP address
 * @return 0 on success, error code on failure
 */
int str_to_ipv4(const char* str, ipv4_address_t* ip);

/**
 * Get device count
 * 
 * @return Number of registered network devices
 */
int network_get_device_count();

/**
 * Get a device by index
 * 
 * @param index Device index
 * @return Pointer to the device or NULL if not found
 */
net_device_t* network_get_device(int index);

/**
 * Enhanced network initialization - sets up loopback device
 * 
 * @return 0 on success, error code on failure
 */
int network_init_enhanced();

#endif /* NETWORK_H */