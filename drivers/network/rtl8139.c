/**
 * @file rtl8139.c
 * @brief RTL8139 Network Card Driver for uintOS
 *
 * This driver provides support for the Realtek RTL8139 Fast Ethernet
 * network adapter using the PCI driver framework.
 */

#include "rtl8139.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include "../../hal/include/hal_io.h"
#include "../../hal/include/hal_interrupt.h"
#include "../../hal/include/hal_memory.h"
#include <string.h>

#define RTL8139_TAG "RTL8139"

// Array of supported PCI vendor IDs
static uint16_t rtl8139_vendor_ids[] = {
    RTL8139_VENDOR_ID
};

// Array of supported PCI device IDs
static uint16_t rtl8139_device_ids[] = {
    RTL8139_DEVICE_ID
};

// Forward declaration of driver operations
static int rtl8139_probe(pci_device_t* dev);
static int rtl8139_initialize(pci_device_t* dev);
static int rtl8139_remove(pci_device_t* dev);
static int rtl8139_suspend(pci_device_t* dev);
static int rtl8139_resume(pci_device_t* dev);

// PCI driver structure
static pci_driver_t rtl8139_driver = {
    .name = "rtl8139",
    .vendor_ids = rtl8139_vendor_ids,
    .device_ids = rtl8139_device_ids,
    .num_supported_devices = 1,
    .ops = {
        .probe = rtl8139_probe,
        .init = rtl8139_initialize,
        .remove = rtl8139_remove,
        .suspend = rtl8139_suspend,
        .resume = rtl8139_resume
    }
};

/**
 * Initialize RTL8139 driver
 */
int rtl8139_init(void) {
    log_info(RTL8139_TAG, "Initializing RTL8139 driver");
    return pci_register_driver(&rtl8139_driver);
}

/**
 * Cleanup RTL8139 driver
 */
void rtl8139_exit(void) {
    log_info(RTL8139_TAG, "Shutting down RTL8139 driver");
    pci_unregister_driver(&rtl8139_driver);
}

/**
 * Read a byte from RTL8139 I/O registers
 */
uint8_t rtl8139_read8(rtl8139_device_t* priv, uint8_t reg) {
    if (priv->mem_mapped) {
        return hal_io_memory_read8(priv->mem_base + reg);
    } else {
        return hal_io_port_in8(priv->io_base + reg);
    }
}

/**
 * Read a 16-bit word from RTL8139 I/O registers
 */
uint16_t rtl8139_read16(rtl8139_device_t* priv, uint8_t reg) {
    if (priv->mem_mapped) {
        return hal_io_memory_read16(priv->mem_base + reg);
    } else {
        return hal_io_port_in16(priv->io_base + reg);
    }
}

/**
 * Read a 32-bit word from RTL8139 I/O registers
 */
uint32_t rtl8139_read32(rtl8139_device_t* priv, uint8_t reg) {
    if (priv->mem_mapped) {
        return hal_io_memory_read32(priv->mem_base + reg);
    } else {
        return hal_io_port_in32(priv->io_base + reg);
    }
}

/**
 * Write a byte to RTL8139 I/O registers
 */
void rtl8139_write8(rtl8139_device_t* priv, uint8_t reg, uint8_t value) {
    if (priv->mem_mapped) {
        hal_io_memory_write8(priv->mem_base + reg, value);
    } else {
        hal_io_port_out8(priv->io_base + reg, value);
    }
}

/**
 * Write a 16-bit word to RTL8139 I/O registers
 */
void rtl8139_write16(rtl8139_device_t* priv, uint8_t reg, uint16_t value) {
    if (priv->mem_mapped) {
        hal_io_memory_write16(priv->mem_base + reg, value);
    } else {
        hal_io_port_out16(priv->io_base + reg, value);
    }
}

/**
 * Write a 32-bit word to RTL8139 I/O registers
 */
void rtl8139_write32(rtl8139_device_t* priv, uint8_t reg, uint32_t value) {
    if (priv->mem_mapped) {
        hal_io_memory_write32(priv->mem_base + reg, value);
    } else {
        hal_io_port_out32(priv->io_base + reg, value);
    }
}

/**
 * Reset the RTL8139 network adapter
 */
int rtl8139_reset(pci_device_t* dev) {
    rtl8139_device_t* priv = (rtl8139_device_t*)dev->private_data;
    
    log_info(RTL8139_TAG, "Resetting RTL8139 adapter");
    
    // Issue software reset
    rtl8139_write8(priv, RTL8139_REG_CMD, RTL8139_CMD_RESET);
    
    // Wait for reset to complete
    int timeout = 1000;
    while (rtl8139_read8(priv, RTL8139_REG_CMD) & RTL8139_CMD_RESET) {
        if (--timeout == 0) {
            log_error(RTL8139_TAG, "Reset timeout");
            return -1;
        }
        
        // Small delay
        hal_io_wait_us(10);
    }
    
    // Read MAC address
    for (int i = 0; i < 6; i++) {
        priv->mac_address[i] = rtl8139_read8(priv, RTL8139_REG_MAC + i);
    }
    
    log_info(RTL8139_TAG, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
             priv->mac_address[0], priv->mac_address[1], 
             priv->mac_address[2], priv->mac_address[3],
             priv->mac_address[4], priv->mac_address[5]);
    
    // Set receive buffer
    rtl8139_write32(priv, RTL8139_REG_RBSTART, priv->rx_buffer_phys);
    
    // Configure receive buffer
    uint32_t rcr = RTL8139_RCR_AAP |    // Accept All Packets (promiscuous mode)
                   RTL8139_RCR_APM |    // Accept Physical Match
                   RTL8139_RCR_AM |     // Accept Multicast
                   RTL8139_RCR_AB |     // Accept Broadcast
                   RTL8139_RCR_RBLEN_8K | // 8K Rx buffer
                   RTL8139_RCR_MXDMA_256; // 256 byte max DMA burst
                   
    rtl8139_write32(priv, RTL8139_REG_RCR, rcr);
    
    // Enable receive and transmit
    rtl8139_write8(priv, RTL8139_REG_CMD, RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);
    
    // Set up interrupts
    uint16_t imr = RTL8139_INT_RXOK | RTL8139_INT_RXERR |
                  RTL8139_INT_TXOK | RTL8139_INT_TXERR |
                  RTL8139_INT_RX_BUFFER_OVERFLOW | RTL8139_INT_LINK_CHANGE |
                  RTL8139_INT_RX_FIFO_OVERFLOW | RTL8139_INT_SYSTEM_ERR;
                  
    rtl8139_write16(priv, RTL8139_REG_IMR, imr);
    
    // Reset Rx pointer
    priv->cur_rx = 0;
    rtl8139_write16(priv, RTL8139_REG_CAPR, 0);
    
    // Initialize tx descriptor index
    priv->tx_next = 0;
    
    log_info(RTL8139_TAG, "RTL8139 initialized successfully");
    return 0;
}

/**
 * Process received packets
 */
int rtl8139_process_rx(rtl8139_device_t* priv) {
    int packets_processed = 0;
    uint8_t cmd = rtl8139_read8(priv, RTL8139_REG_CMD);
    
    // Check if there are packets to read
    while (!(cmd & RTL8139_CMD_RX_BUF_EMPTY)) {
        uint16_t rx_status = *(uint16_t*)(priv->rx_buffer + priv->cur_rx);
        uint16_t rx_length = *(uint16_t*)(priv->rx_buffer + priv->cur_rx + 2);
        
        // Skip packet header (4 bytes)
        uint8_t* packet = priv->rx_buffer + priv->cur_rx + 4;
        
        log_debug(RTL8139_TAG, "Received packet: status=0x%04X, length=%u", rx_status, rx_length);
        
        // Check if packet is valid
        if (rx_status & 0x1) { // ROK (Receive OK)
            priv->packet_counter++;
            priv->bytes_counter += rx_length;
            
            // Process packet (in a real driver, we would pass this to the network stack)
            log_debug(RTL8139_TAG, "Packet dst MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
                     
            log_debug(RTL8139_TAG, "Packet src MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]);
            
            packets_processed++;
        } else {
            log_warning(RTL8139_TAG, "Received invalid packet: status=0x%04X", rx_status);
        }
        
        // Update Rx pointer (align to 4 bytes)
        priv->cur_rx = (priv->cur_rx + rx_length + 4 + 3) & ~3;
        
        // Handle wrap
        if (priv->cur_rx > RTL8139_RX_BUFFER_SIZE) {
            priv->cur_rx = priv->cur_rx - RTL8139_RX_BUFFER_SIZE;
        }
        
        // Update Rx pointer in the device
        rtl8139_write16(priv, RTL8139_REG_CAPR, priv->cur_rx - 16);
        
        // Check if there are more packets
        cmd = rtl8139_read8(priv, RTL8139_REG_CMD);
    }
    
    return packets_processed;
}

/**
 * Transmit a packet
 */
int rtl8139_transmit(rtl8139_device_t* priv, const void* data, size_t length) {
    if (length > RTL8139_TX_BUFFER_SIZE) {
        log_error(RTL8139_TAG, "Packet too large (%u bytes)", length);
        return -1;
    }
    
    // Find next available transmit descriptor
    uint32_t tsd_reg = RTL8139_REG_TSD0 + (priv->tx_next * 4);
    
    // Check if the descriptor is available (OWN bit cleared)
    uint32_t tsd = rtl8139_read32(priv, tsd_reg);
    if (!(tsd & (1 << 13))) {
        // Copy data to transmit buffer
        memcpy(priv->tx_buffer[priv->tx_next], data, length);
        
        // Get the transmit address register
        uint32_t tsad_reg = RTL8139_REG_TSAD0 + (priv->tx_next * 4);
        
        // Write the physical address to the device
        rtl8139_write32(priv, tsad_reg, priv->tx_buffer_phys[priv->tx_next]);
        
        // Write the transmit status register to start transmission
        rtl8139_write32(priv, tsd_reg, length);
        
        // Move to next descriptor
        priv->tx_next = (priv->tx_next + 1) % RTL8139_NUM_TX_DESCRIPTORS;
        
        return 0;
    }
    
    log_warning(RTL8139_TAG, "No available TX descriptor");
    return -1;
}

/**
 * RTL8139 interrupt handler
 */
void rtl8139_interrupt(pci_device_t* dev) {
    rtl8139_device_t* priv = (rtl8139_device_t*)dev->private_data;
    uint16_t isr = rtl8139_read16(priv, RTL8139_REG_ISR);
    
    // Clear interrupts by writing the value back
    rtl8139_write16(priv, RTL8139_REG_ISR, isr);
    
    // Log interrupt status
    log_debug(RTL8139_TAG, "Interrupt: ISR=0x%04X", isr);
    
    // Handle received packets
    if (isr & (RTL8139_INT_RXOK | RTL8139_INT_RX_BUFFER_OVERFLOW)) {
        int packets = rtl8139_process_rx(priv);
        if (packets > 0) {
            log_debug(RTL8139_TAG, "Processed %d packets", packets);
        }
    }
    
    // Handle transmit completion
    if (isr & RTL8139_INT_TXOK) {
        log_debug(RTL8139_TAG, "Transmit complete");
    }
    
    // Handle transmit error
    if (isr & RTL8139_INT_TXERR) {
        log_error(RTL8139_TAG, "Transmit error");
    }
    
    // Handle link change
    if (isr & RTL8139_INT_LINK_CHANGE) {
        log_info(RTL8139_TAG, "Link state changed");
    }
    
    // Handle system error
    if (isr & RTL8139_INT_SYSTEM_ERR) {
        log_error(RTL8139_TAG, "System error, resetting device");
        rtl8139_reset(dev);
    }
}

/**
 * RTL8139 interrupt callback for HAL
 */
static void rtl8139_interrupt_handler(void* context) {
    pci_device_t* dev = (pci_device_t*)context;
    rtl8139_interrupt(dev);
}

/**
 * Check if this driver can handle the device
 */
static int rtl8139_probe(pci_device_t* dev) {
    log_info(RTL8139_TAG, "Probing device %04X:%04X", 
            dev->id.vendor_id, dev->id.device_id);
    
    // Extra validation if needed
    return 0; // 0 indicates we can handle this device
}

/**
 * Initialize the RTL8139 device after being claimed by the driver
 */
static int rtl8139_initialize(pci_device_t* dev) {
    log_info(RTL8139_TAG, "Initializing RTL8139 network adapter");
    
    // Allocate private device data
    rtl8139_device_t* priv = (rtl8139_device_t*)heap_alloc(sizeof(rtl8139_device_t));
    if (!priv) {
        log_error(RTL8139_TAG, "Failed to allocate device structure");
        return -1;
    }
    
    // Clear the structure
    memset(priv, 0, sizeof(rtl8139_device_t));
    
    // Store the private data in the PCI device structure
    dev->private_data = priv;
    
    // Enable PCI bus mastering and I/O space
    pci_enable_bus_mastering(dev);
    
    // Find the I/O or memory base address
    for (int i = 0; i < 6; i++) {
        if (dev->id.bar[i] != 0) {
            uint32_t base;
            uint32_t size;
            bool is_io;
            
            if (pci_get_bar_info(dev, i, &base, &size, &is_io) == 0) {
                if (is_io) {
                    priv->io_base = base;
                    priv->mem_mapped = false;
                    log_info(RTL8139_TAG, "Using I/O ports at 0x%X", base);
                    break;
                } else {
                    priv->mem_base = base;
                    priv->mem_mapped = true;
                    log_info(RTL8139_TAG, "Using memory-mapped I/O at 0x%X", base);
                    break;
                }
            }
        }
    }
    
    // Check if we found a base address
    if (!priv->mem_mapped && priv->io_base == 0) {
        log_error(RTL8139_TAG, "No I/O or memory-mapped address found");
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Store IRQ number
    priv->irq = dev->id.interrupt_line;
    
    log_info(RTL8139_TAG, "Using IRQ %d", priv->irq);
    
    // Allocate receive buffer (aligned to 32 bytes)
    priv->rx_buffer = (uint8_t*)hal_memory_allocate(RTL8139_RX_BUFFER_SIZE + 16, 32);
    if (!priv->rx_buffer) {
        log_error(RTL8139_TAG, "Failed to allocate receive buffer");
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Get physical address of receive buffer
    priv->rx_buffer_phys = (uint32_t)hal_memory_get_physical(priv->rx_buffer);
    
    // Allocate transmit buffers
    for (int i = 0; i < RTL8139_NUM_TX_DESCRIPTORS; i++) {
        priv->tx_buffer[i] = (uint8_t*)hal_memory_allocate(RTL8139_TX_BUFFER_SIZE, 32);
        if (!priv->tx_buffer[i]) {
            log_error(RTL8139_TAG, "Failed to allocate transmit buffer %d", i);
            
            // Free already allocated buffers
            for (int j = 0; j < i; j++) {
                hal_memory_free(priv->tx_buffer[j]);
            }
            
            hal_memory_free(priv->rx_buffer);
            heap_free(priv);
            dev->private_data = NULL;
            return -1;
        }
        
        // Get physical address of transmit buffer
        priv->tx_buffer_phys[i] = (uint32_t)hal_memory_get_physical(priv->tx_buffer[i]);
    }
    
    // Register interrupt handler
    int irq_result = hal_interrupt_register_handler(priv->irq, rtl8139_interrupt_handler, dev);
    if (irq_result != 0) {
        log_error(RTL8139_TAG, "Failed to register interrupt handler");
        
        // Free buffers
        for (int i = 0; i < RTL8139_NUM_TX_DESCRIPTORS; i++) {
            hal_memory_free(priv->tx_buffer[i]);
        }
        
        hal_memory_free(priv->rx_buffer);
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Reset and configure the device
    int reset_result = rtl8139_reset(dev);
    if (reset_result != 0) {
        log_error(RTL8139_TAG, "Failed to reset device");
        
        // Unregister interrupt handler
        hal_interrupt_unregister_handler(priv->irq);
        
        // Free buffers
        for (int i = 0; i < RTL8139_NUM_TX_DESCRIPTORS; i++) {
            hal_memory_free(priv->tx_buffer[i]);
        }
        
        hal_memory_free(priv->rx_buffer);
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Create a device in the device manager
    device_t* net_device = (device_t*)heap_alloc(sizeof(device_t));
    if (net_device) {
        memset(net_device, 0, sizeof(device_t));
        
        snprintf(net_device->name, sizeof(net_device->name), "eth%d", 0); // eth0
        net_device->type = DEVICE_TYPE_NETWORK;
        net_device->status = DEVICE_STATUS_ENABLED;
        net_device->vendor_id = dev->id.vendor_id;
        net_device->device_id = dev->id.device_id;
        net_device->irq = priv->irq;
        net_device->private_data = dev;
        
        // Register the device with the device manager
        device_register(net_device);
        
        // Store OS device in PCI device structure
        dev->os_device = net_device;
        
        log_info(RTL8139_TAG, "Registered network device '%s'", net_device->name);
    } else {
        log_warning(RTL8139_TAG, "Failed to create device manager entry");
    }
    
    log_info(RTL8139_TAG, "RTL8139 initialization complete");
    return 0;
}

/**
 * Remove the RTL8139 device
 */
static int rtl8139_remove(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8139_device_t* priv = (rtl8139_device_t*)dev->private_data;
    
    log_info(RTL8139_TAG, "Removing RTL8139 network adapter");
    
    // Disable all interrupts
    rtl8139_write16(priv, RTL8139_REG_IMR, 0);
    
    // Disable Rx and Tx
    rtl8139_write8(priv, RTL8139_REG_CMD, 0);
    
    // Unregister interrupt handler
    hal_interrupt_unregister_handler(priv->irq);
    
    // Free transmit buffers
    for (int i = 0; i < RTL8139_NUM_TX_DESCRIPTORS; i++) {
        if (priv->tx_buffer[i]) {
            hal_memory_free(priv->tx_buffer[i]);
        }
    }
    
    // Free receive buffer
    if (priv->rx_buffer) {
        hal_memory_free(priv->rx_buffer);
    }
    
    // Unregister from device manager
    if (dev->os_device) {
        device_unregister(dev->os_device);
        heap_free(dev->os_device);
        dev->os_device = NULL;
    }
    
    // Free private data
    heap_free(priv);
    dev->private_data = NULL;
    
    log_info(RTL8139_TAG, "RTL8139 removed successfully");
    return 0;
}

/**
 * Suspend the RTL8139 device
 */
static int rtl8139_suspend(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8139_device_t* priv = (rtl8139_device_t*)dev->private_data;
    
    log_info(RTL8139_TAG, "Suspending RTL8139 network adapter");
    
    // Disable all interrupts
    rtl8139_write16(priv, RTL8139_REG_IMR, 0);
    
    // Disable Rx and Tx
    rtl8139_write8(priv, RTL8139_REG_CMD, 0);
    
    return 0;
}

/**
 * Resume the RTL8139 device
 */
static int rtl8139_resume(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    log_info(RTL8139_TAG, "Resuming RTL8139 network adapter");
    
    // Reset and reconfigure the device
    return rtl8139_reset(dev);
}