/**
 * @file rtl8821ce.h
 * @brief Realtek RTL8821CE 802.11ac WiFi driver for uintOS
 * 
 * This file provides driver support for Realtek RTL8821CE WiFi chipsets,
 * commonly found in laptops and desktop PCIe WiFi cards.
 */

#ifndef RTL8821CE_H
#define RTL8821CE_H

#include <stdint.h>
#include <stdbool.h>
#include "../../../kernel/device_manager.h"
#include "../../../drivers/pci/pci.h"
#include "../include/net_if.h"
#include "../include/wifi.h"

// RTL8821CE driver version
#define RTL8821CE_DRV_VERSION 0x00010000  // 1.0.0.0

// Realtek RTL8821CE PCI vendor and device IDs
#define RTL8821CE_VENDOR_ID 0x10EC  // Realtek vendor ID
#define RTL8821CE_DEVICE_ID 0xC821  // RTL8821CE device ID

// Register offsets
#define RTL8821CE_REG_SYS_CFG       0x0000
#define RTL8821CE_REG_INT_MASK      0x0038
#define RTL8821CE_REG_INT_STATUS    0x003C
#define RTL8821CE_REG_INT_ENABLE    0x0040
#define RTL8821CE_REG_MAC_ADDR      0x0050  // MAC address (6 bytes)
#define RTL8821CE_REG_TX_DESC_BASE  0x0100
#define RTL8821CE_REG_RX_DESC_BASE  0x0200
#define RTL8821CE_REG_TX_STATUS     0x0300
#define RTL8821CE_REG_RX_STATUS     0x0400

// Register bits
#define RTL8821CE_INT_TX_OK        0x00000001
#define RTL8821CE_INT_RX_OK        0x00000002
#define RTL8821CE_INT_TX_ERR       0x00000004
#define RTL8821CE_INT_RX_ERR       0x00000008
#define RTL8821CE_INT_LINK_CHG     0x00000010
#define RTL8821CE_INT_MAC_RESET    0x00000020

// Hardware capabilities
#define RTL8821CE_MAX_TX_DESC 256
#define RTL8821CE_MAX_RX_DESC 256

// Maximum wireless networks to track
#define RTL8821CE_MAX_NETWORKS 32

// RTL8821CE descriptor structure (shared by TX and RX)
typedef struct {
    uint32_t flags;      // Descriptor flags
    uint32_t addr_lo;    // Buffer address low 32 bits
    uint32_t addr_hi;    // Buffer address high 32 bits (for 64-bit systems)
    uint16_t buf_size;   // Buffer size
    uint16_t reserved;   // Reserved/padding
    uint32_t next;       // Next descriptor address
} rtl8821ce_desc_t;

// WiFi network structure
typedef struct {
    uint8_t bssid[6];            // BSSID (MAC address of AP)
    char ssid[33];               // SSID (network name, null-terminated)
    uint8_t ssid_len;            // Length of SSID
    uint8_t channel;             // Channel number
    uint8_t signal_strength;     // Signal strength (RSSI)
    wifi_security_type_t security; // Security type
    bool is_connected;           // Whether we're connected to this network
} rtl8821ce_network_t;

// RTL8821CE device context structure
typedef struct {
    uint32_t mmio_base;          // Memory mapped I/O base address
    
    // TX/RX descriptor rings
    rtl8821ce_desc_t* tx_desc;   // TX descriptor ring (virtual address)
    rtl8821ce_desc_t* rx_desc;   // RX descriptor ring (virtual address)
    uint32_t tx_desc_phys;       // TX descriptor ring (physical address)
    uint32_t rx_desc_phys;       // RX descriptor ring (physical address)
    uint32_t tx_index;           // Current TX descriptor index
    uint32_t rx_index;           // Current RX descriptor index
    
    // TX/RX buffers
    void* tx_buffers[RTL8821CE_MAX_TX_DESC]; // TX buffer pointers
    void* rx_buffers[RTL8821CE_MAX_RX_DESC]; // RX buffer pointers
    
    // Network interface
    net_if_t* net_if;            // Network interface structure
    
    // MAC address
    uint8_t mac_addr[6];         // Device MAC address
    
    // PHY status
    bool link_up;                // Link status
    uint32_t link_speed;         // Link speed in Mbps
    
    // Wireless networks
    rtl8821ce_network_t networks[RTL8821CE_MAX_NETWORKS]; // Scanned networks
    uint32_t num_networks;       // Number of networks found
    
    // Interrupt handling
    uint32_t irq;                // IRQ number
    mutex_t tx_mutex;            // Transmit mutex
    
    // Device status
    bool initialized;            // Whether device is initialized
} rtl8821ce_device_t;

/**
 * Initialize RTL8821CE driver
 * 
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_init(void);

/**
 * Transmit a packet
 * 
 * @param dev Device pointer
 * @param buffer Buffer containing packet data
 * @param length Length of packet
 * @return Number of bytes sent or negative error code
 */
int rtl8821ce_transmit(device_t* dev, const void* buffer, size_t length);

/**
 * Receive a packet
 * 
 * @param dev Device pointer
 * @param buffer Buffer to store packet data
 * @param max_len Maximum length of buffer
 * @return Number of bytes received or negative error code
 */
int rtl8821ce_receive(device_t* dev, void* buffer, size_t max_len);

/**
 * Set MAC address for device
 * 
 * @param dev Device pointer
 * @param mac_addr MAC address (6 bytes)
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_set_mac_address(device_t* dev, const uint8_t* mac_addr);

/**
 * Get link status
 * 
 * @param dev Device pointer
 * @param status Pointer to store status (0 = down, 1 = up)
 * @param speed Pointer to store speed in Mbps
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_get_link_status(device_t* dev, uint32_t* status, uint32_t* speed);

/**
 * WiFi-specific functions
 */

/**
 * Scan for wireless networks
 * 
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_scan(device_t* dev);

/**
 * Connect to a wireless network
 * 
 * @param dev Device pointer
 * @param ssid SSID of network to connect to
 * @param password Password for network (NULL for open networks)
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_connect(device_t* dev, const char* ssid, const char* password);

/**
 * Disconnect from current wireless network
 * 
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_disconnect(device_t* dev);

/**
 * Get current connected network information
 * 
 * @param dev Device pointer
 * @param network Pointer to structure to store network information
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_get_network_info(device_t* dev, wifi_network_info_t* network);

/**
 * Get list of available wireless networks
 * 
 * @param dev Device pointer
 * @param networks Array to store network information
 * @param max_networks Maximum number of networks to return
 * @param num_networks Pointer to store actual number of networks
 * @return 0 on success, negative error code on failure
 */
int rtl8821ce_get_networks(device_t* dev, wifi_network_info_t* networks, 
                          uint32_t max_networks, uint32_t* num_networks);

#endif /* RTL8821CE_H */