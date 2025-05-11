/**
 * @file rtl8139.h
 * @brief RTL8139 Network Card Driver for uintOS
 *
 * This driver provides support for the Realtek RTL8139 Fast Ethernet
 * network adapter using the PCI driver framework.
 */

#ifndef UINTOS_RTL8139_H
#define UINTOS_RTL8139_H

#include <stdint.h>
#include <stdbool.h>
#include "../pci/pci.h"
#include "../../kernel/device_manager.h"

// RTL8139 PCI vendor and device IDs
#define RTL8139_VENDOR_ID            0x10EC    // Realtek
#define RTL8139_DEVICE_ID            0x8139    // RTL8139

// RTL8139 registers
#define RTL8139_REG_MAC             0x00      // MAC address (0-5)
#define RTL8139_REG_MAR             0x08      // Multicast address (8-15)
#define RTL8139_REG_TSD0            0x10      // Transmit Status of Descriptor 0
#define RTL8139_REG_TSD1            0x14      // Transmit Status of Descriptor 1
#define RTL8139_REG_TSD2            0x18      // Transmit Status of Descriptor 2
#define RTL8139_REG_TSD3            0x1C      // Transmit Status of Descriptor 3
#define RTL8139_REG_TSAD0           0x20      // Transmit Start Address of Descriptor 0
#define RTL8139_REG_TSAD1           0x24      // Transmit Start Address of Descriptor 1
#define RTL8139_REG_TSAD2           0x28      // Transmit Start Address of Descriptor 2
#define RTL8139_REG_TSAD3           0x2C      // Transmit Start Address of Descriptor 3
#define RTL8139_REG_RBSTART         0x30      // Receive Buffer Start Address
#define RTL8139_REG_CMD             0x37      // Command Register
#define RTL8139_REG_CAPR            0x38      // Current Address of Packet Read
#define RTL8139_REG_IMR             0x3C      // Interrupt Mask Register
#define RTL8139_REG_ISR             0x3E      // Interrupt Status Register
#define RTL8139_REG_RCR             0x44      // Receive Configuration Register
#define RTL8139_REG_CONFIG1         0x52      // Configuration Register 1

// RTL8139 command register values
#define RTL8139_CMD_RESET           0x10      // Reset the controller
#define RTL8139_CMD_RX_ENABLE       0x08      // Enable reception
#define RTL8139_CMD_TX_ENABLE       0x04      // Enable transmission
#define RTL8139_CMD_RX_BUF_EMPTY    0x01      // Receive buffer empty

// RTL8139 interrupt mask/status bits
#define RTL8139_INT_RXOK            0x0001    // Receive OK
#define RTL8139_INT_RXERR           0x0002    // Receive Error
#define RTL8139_INT_TXOK            0x0004    // Transmit OK
#define RTL8139_INT_TXERR           0x0008    // Transmit Error
#define RTL8139_INT_RX_BUFFER_OVERFLOW 0x0010 // Rx buffer overflow
#define RTL8139_INT_LINK_CHANGE     0x0020    // Link change
#define RTL8139_INT_RX_FIFO_OVERFLOW 0x0040   // Rx FIFO overflow
#define RTL8139_INT_CABLE_LEN_CHNG  0x2000    // Cable length change
#define RTL8139_INT_TIMEOUT         0x4000    // Timeout
#define RTL8139_INT_SYSTEM_ERR      0x8000    // System error

// RTL8139 receive configuration register bits
#define RTL8139_RCR_AAP             0x00000001 // Accept All Packets
#define RTL8139_RCR_APM             0x00000002 // Accept Physical Match
#define RTL8139_RCR_AM              0x00000004 // Accept Multicast
#define RTL8139_RCR_AB              0x00000008 // Accept Broadcast
#define RTL8139_RCR_WRAP            0x00000080 // Wrap
#define RTL8139_RCR_RBLEN_8K        0x00000000 // 8K + 16 bytes receive buffer
#define RTL8139_RCR_RBLEN_16K       0x00000100 // 16K + 16 bytes receive buffer
#define RTL8139_RCR_RBLEN_32K       0x00000200 // 32K + 16 bytes receive buffer
#define RTL8139_RCR_RBLEN_64K       0x00000300 // 64K + 16 bytes receive buffer
#define RTL8139_RCR_MXDMA_16        0x00000000 // Max DMA burst size 16 bytes
#define RTL8139_RCR_MXDMA_32        0x00000100 // Max DMA burst size 32 bytes
#define RTL8139_RCR_MXDMA_64        0x00000200 // Max DMA burst size 64 bytes
#define RTL8139_RCR_MXDMA_128       0x00000300 // Max DMA burst size 128 bytes
#define RTL8139_RCR_MXDMA_256       0x00000400 // Max DMA burst size 256 bytes
#define RTL8139_RCR_MXDMA_512       0x00000500 // Max DMA burst size 512 bytes
#define RTL8139_RCR_MXDMA_1K        0x00000600 // Max DMA burst size 1K bytes
#define RTL8139_RCR_MXDMA_UNLIMITED 0x00000700 // Unlimited DMA burst size

// RTL8139 receive buffer size (must be aligned to 32)
#define RTL8139_RX_BUFFER_SIZE      8192     // 8K buffer size
#define RTL8139_TX_BUFFER_SIZE      1536     // 1536 bytes buffer size per descriptor
#define RTL8139_NUM_TX_DESCRIPTORS  4        // 4 transmit descriptors

// RTL8139 private device structure
typedef struct {
    uint32_t io_base;                 // I/O base address
    uint32_t mem_base;                // Memory-mapped base address
    uint8_t irq;                      // IRQ number
    bool mem_mapped;                  // Whether using memory-mapped I/O
    
    uint8_t mac_address[6];           // MAC address
    
    uint8_t* rx_buffer;               // Receive buffer
    uint32_t rx_buffer_phys;          // Physical address of receive buffer
    uint32_t cur_rx;                  // Current receive pointer
    
    uint8_t* tx_buffer[RTL8139_NUM_TX_DESCRIPTORS]; // Transmit buffers
    uint32_t tx_buffer_phys[RTL8139_NUM_TX_DESCRIPTORS]; // Physical addresses of transmit buffers
    uint8_t tx_next;                  // Next transmit descriptor to use
    
    uint32_t packet_counter;          // Statistics: Packets received
    uint32_t bytes_counter;           // Statistics: Bytes received
} rtl8139_device_t;

/**
 * Initialize RTL8139 driver
 * 
 * @return 0 on success, negative error code on failure
 */
int rtl8139_init(void);

/**
 * Cleanup RTL8139 driver
 */
void rtl8139_exit(void);

/**
 * Reset and initialize an RTL8139 device
 * 
 * @param dev The PCI device structure
 * @return 0 on success, negative error code on failure
 */
int rtl8139_reset(pci_device_t* dev);

/**
 * Read a byte from RTL8139 I/O registers
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read
 */
uint8_t rtl8139_read8(rtl8139_device_t* priv, uint8_t reg);

/**
 * Read a 16-bit word from RTL8139 I/O registers
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read
 */
uint16_t rtl8139_read16(rtl8139_device_t* priv, uint8_t reg);

/**
 * Read a 32-bit word from RTL8139 I/O registers
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read
 */
uint32_t rtl8139_read32(rtl8139_device_t* priv, uint8_t reg);

/**
 * Write a byte to RTL8139 I/O registers
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write
 */
void rtl8139_write8(rtl8139_device_t* priv, uint8_t reg, uint8_t value);

/**
 * Write a 16-bit word to RTL8139 I/O registers
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write
 */
void rtl8139_write16(rtl8139_device_t* priv, uint8_t reg, uint16_t value);

/**
 * Write a 32-bit word to RTL8139 I/O registers
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write
 */
void rtl8139_write32(rtl8139_device_t* priv, uint8_t reg, uint32_t value);

/**
 * Process received packets
 * 
 * @param priv Device private data
 * @return Number of packets processed
 */
int rtl8139_process_rx(rtl8139_device_t* priv);

/**
 * Transmit a packet
 * 
 * @param priv Device private data
 * @param data Packet data
 * @param length Packet length
 * @return 0 on success, negative error code on failure
 */
int rtl8139_transmit(rtl8139_device_t* priv, const void* data, size_t length);

/**
 * RTL8139 interrupt handler
 * 
 * @param dev The PCI device structure
 */
void rtl8139_interrupt(pci_device_t* dev);

#endif /* UINTOS_RTL8139_H */