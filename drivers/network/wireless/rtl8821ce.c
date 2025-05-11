/**
 * @file rtl8821ce.c
 * @brief Realtek RTL8821CE 802.11ac WiFi driver implementation
 * 
 * Implementation of the driver for Realtek RTL8821CE WiFi chipset
 */

#include "rtl8821ce.h"
#include "../../../kernel/logging/log.h"
#include "../../../memory/heap.h"
#include "../../../hal/include/hal_io.h"
#include "../../../hal/include/hal_memory.h"
#include "../../../hal/include/hal_interrupt.h"
#include "../../../kernel/sync.h"
#include <string.h>

#define RTL8821CE_TAG "RTL8821CE"

// PCI class code and subclass for WiFi controllers
#define PCI_CLASS_NETWORK 0x02
#define PCI_SUBCLASS_WIRELESS 0x80

// Maximum number of controllers
#define RTL8821CE_MAX_CONTROLLERS 4

// Default buffer sizes
#define RTL8821CE_TX_BUFFER_SIZE 2048
#define RTL8821CE_RX_BUFFER_SIZE 2048

// Maximum packet size
#define RTL8821CE_MAX_PACKET_SIZE 1522  // Ethernet MTU (1500) + headers

// Descriptor flags
#define RTL8821CE_DESC_FLAG_OWN           0x80000000  // Ownership (1=device, 0=driver)
#define RTL8821CE_DESC_FLAG_EOR           0x40000000  // End of Ring
#define RTL8821CE_DESC_FLAG_FS            0x20000000  // First Segment
#define RTL8821CE_DESC_FLAG_LS            0x10000000  // Last Segment
#define RTL8821CE_DESC_FLAG_LENGTH_MASK   0x0000FFFF  // Packet length mask

// Storage for RTL8821CE controllers
static rtl8821ce_device_t controllers[RTL8821CE_MAX_CONTROLLERS];
static int num_controllers = 0;

// Forward declarations for internal functions
static int rtl8821ce_probe(pci_device_t* dev);
static int rtl8821ce_initialize(pci_device_t* dev);
static int rtl8821ce_remove(pci_device_t* dev);
static int rtl8821ce_suspend(pci_device_t* dev);
static int rtl8821ce_resume(pci_device_t* dev);

// Device operation functions
static int rtl8821ce_dev_open(device_t* dev, uint32_t flags);
static int rtl8821ce_dev_close(device_t* dev);
static int rtl8821ce_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset);
static int rtl8821ce_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset);
static int rtl8821ce_dev_ioctl(device_t* dev, int request, void* arg);

// Network interface operations
static int rtl8821ce_net_transmit(net_if_t* net_if, const void* buffer, size_t length);
static int rtl8821ce_net_set_hw_address(net_if_t* net_if, const uint8_t* mac_addr);
static int rtl8821ce_net_get_stats(net_if_t* net_if, net_if_stats_t* stats);

// WiFi operations
static int rtl8821ce_wifi_scan(wifi_device_t* wifi_dev);
static int rtl8821ce_wifi_connect(wifi_device_t* wifi_dev, const char* ssid, const char* password);
static int rtl8821ce_wifi_disconnect(wifi_device_t* wifi_dev);
static int rtl8821ce_wifi_get_network_info(wifi_device_t* wifi_dev, wifi_network_info_t* network);
static int rtl8821ce_wifi_get_networks(wifi_device_t* wifi_dev, wifi_network_info_t* networks, 
                                      uint32_t max_networks, uint32_t* num_networks);

// PCI driver structure
static pci_driver_t rtl8821ce_driver = {
    .name = "rtl8821ce",
    .vendor_ids = (uint16_t[]){ RTL8821CE_VENDOR_ID },
    .device_ids = (uint16_t[]){ RTL8821CE_DEVICE_ID },
    .num_supported_devices = 1,
    .ops = {
        .probe = rtl8821ce_probe,
        .init = rtl8821ce_initialize,
        .remove = rtl8821ce_remove,
        .suspend = rtl8821ce_suspend,
        .resume = rtl8821ce_resume
    }
};

// Device operations
static device_ops_t rtl8821ce_dev_ops = {
    .open = rtl8821ce_dev_open,
    .close = rtl8821ce_dev_close,
    .read = rtl8821ce_dev_read,
    .write = rtl8821ce_dev_write,
    .ioctl = rtl8821ce_dev_ioctl
};

// Network interface operations
static net_if_ops_t rtl8821ce_net_ops = {
    .transmit = rtl8821ce_net_transmit,
    .set_hw_address = rtl8821ce_net_set_hw_address,
    .get_stats = rtl8821ce_net_get_stats
};

// WiFi operations
static wifi_ops_t rtl8821ce_wifi_ops = {
    .scan = rtl8821ce_wifi_scan,
    .connect = rtl8821ce_wifi_connect,
    .disconnect = rtl8821ce_wifi_disconnect,
    .get_network_info = rtl8821ce_wifi_get_network_info,
    .get_networks = rtl8821ce_wifi_get_networks
};

// Helper functions
static inline uint32_t rtl8821ce_read_reg32(rtl8821ce_device_t* rtl, uint32_t reg) {
    return *(volatile uint32_t*)(rtl->mmio_base + reg);
}

static inline void rtl8821ce_write_reg32(rtl8821ce_device_t* rtl, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(rtl->mmio_base + reg) = val;
}

static inline uint8_t rtl8821ce_read_reg8(rtl8821ce_device_t* rtl, uint32_t reg) {
    return *(volatile uint8_t*)(rtl->mmio_base + reg);
}

static inline void rtl8821ce_write_reg8(rtl8821ce_device_t* rtl, uint32_t reg, uint8_t val) {
    *(volatile uint8_t*)(rtl->mmio_base + reg) = val;
}

// Initialize descriptor rings
static int rtl8821ce_init_descriptors(rtl8821ce_device_t* rtl) {
    // Allocate TX descriptor ring
    uint32_t tx_size = RTL8821CE_MAX_TX_DESC * sizeof(rtl8821ce_desc_t);
    uint64_t tx_phys_addr;
    
    rtl->tx_desc = hal_memory_allocate_physical(tx_size, 256, HAL_MEMORY_CACHEABLE, &tx_phys_addr);
    if (!rtl->tx_desc) {
        log_error(RTL8821CE_TAG, "Failed to allocate TX descriptor ring");
        return -1;
    }
    
    rtl->tx_desc_phys = (uint32_t)tx_phys_addr;
    
    // Allocate RX descriptor ring
    uint32_t rx_size = RTL8821CE_MAX_RX_DESC * sizeof(rtl8821ce_desc_t);
    uint64_t rx_phys_addr;
    
    rtl->rx_desc = hal_memory_allocate_physical(rx_size, 256, HAL_MEMORY_CACHEABLE, &rx_phys_addr);
    if (!rtl->rx_desc) {
        log_error(RTL8821CE_TAG, "Failed to allocate RX descriptor ring");
        hal_memory_free(rtl->tx_desc);
        rtl->tx_desc = NULL;
        return -1;
    }
    
    rtl->rx_desc_phys = (uint32_t)rx_phys_addr;
    
    // Initialize TX descriptors
    memset(rtl->tx_desc, 0, tx_size);
    
    for (int i = 0; i < RTL8821CE_MAX_TX_DESC; i++) {
        // Allocate TX buffer
        rtl->tx_buffers[i] = heap_alloc(RTL8821CE_TX_BUFFER_SIZE);
        if (!rtl->tx_buffers[i]) {
            // Free previously allocated buffers
            for (int j = 0; j < i; j++) {
                heap_free(rtl->tx_buffers[j]);
            }
            hal_memory_free(rtl->tx_desc);
            hal_memory_free(rtl->rx_desc);
            rtl->tx_desc = NULL;
            rtl->rx_desc = NULL;
            return -1;
        }
        
        // Setup descriptor
        uint64_t buf_phys;
        if (hal_memory_get_physical(rtl->tx_buffers[i], &buf_phys) != HAL_SUCCESS) {
            // Free resources on failure
            for (int j = 0; j <= i; j++) {
                heap_free(rtl->tx_buffers[j]);
            }
            hal_memory_free(rtl->tx_desc);
            hal_memory_free(rtl->rx_desc);
            rtl->tx_desc = NULL;
            rtl->rx_desc = NULL;
            return -1;
        }
        
        rtl->tx_desc[i].addr_lo = (uint32_t)buf_phys;
        rtl->tx_desc[i].addr_hi = (uint32_t)(buf_phys >> 32);
        rtl->tx_desc[i].buf_size = RTL8821CE_TX_BUFFER_SIZE;
        rtl->tx_desc[i].flags = 0; // Not owned by device yet
        
        // Set "End of Ring" for last descriptor
        if (i == RTL8821CE_MAX_TX_DESC - 1) {
            rtl->tx_desc[i].flags |= RTL8821CE_DESC_FLAG_EOR;
            // Point to start of ring
            rtl->tx_desc[i].next = rtl->tx_desc_phys;
        } else {
            // Point to next descriptor
            rtl->tx_desc[i].next = rtl->tx_desc_phys + ((i + 1) * sizeof(rtl8821ce_desc_t));
        }
    }
    
    // Initialize RX descriptors
    memset(rtl->rx_desc, 0, rx_size);
    
    for (int i = 0; i < RTL8821CE_MAX_RX_DESC; i++) {
        // Allocate RX buffer
        rtl->rx_buffers[i] = heap_alloc(RTL8821CE_RX_BUFFER_SIZE);
        if (!rtl->rx_buffers[i]) {
            // Free previously allocated buffers
            for (int j = 0; j < RTL8821CE_MAX_TX_DESC; j++) {
                heap_free(rtl->tx_buffers[j]);
            }
            for (int j = 0; j < i; j++) {
                heap_free(rtl->rx_buffers[j]);
            }
            hal_memory_free(rtl->tx_desc);
            hal_memory_free(rtl->rx_desc);
            rtl->tx_desc = NULL;
            rtl->rx_desc = NULL;
            return -1;
        }
        
        // Setup descriptor
        uint64_t buf_phys;
        if (hal_memory_get_physical(rtl->rx_buffers[i], &buf_phys) != HAL_SUCCESS) {
            // Free resources on failure
            for (int j = 0; j < RTL8821CE_MAX_TX_DESC; j++) {
                heap_free(rtl->tx_buffers[j]);
            }
            for (int j = 0; j <= i; j++) {
                heap_free(rtl->rx_buffers[j]);
            }
            hal_memory_free(rtl->tx_desc);
            hal_memory_free(rtl->rx_desc);
            rtl->tx_desc = NULL;
            rtl->rx_desc = NULL;
            return -1;
        }
        
        rtl->rx_desc[i].addr_lo = (uint32_t)buf_phys;
        rtl->rx_desc[i].addr_hi = (uint32_t)(buf_phys >> 32);
        rtl->rx_desc[i].buf_size = RTL8821CE_RX_BUFFER_SIZE;
        rtl->rx_desc[i].flags = RTL8821CE_DESC_FLAG_OWN; // Give to device
        
        // Set "End of Ring" for last descriptor
        if (i == RTL8821CE_MAX_RX_DESC - 1) {
            rtl->rx_desc[i].flags |= RTL8821CE_DESC_FLAG_EOR;
            // Point to start of ring
            rtl->rx_desc[i].next = rtl->rx_desc_phys;
        } else {
            // Point to next descriptor
            rtl->rx_desc[i].next = rtl->rx_desc_phys + ((i + 1) * sizeof(rtl8821ce_desc_t));
        }
    }
    
    // Reset indices
    rtl->tx_index = 0;
    rtl->rx_index = 0;
    
    return 0;
}

// Free descriptor rings
static void rtl8821ce_free_descriptors(rtl8821ce_device_t* rtl) {
    if (rtl->tx_desc) {
        for (int i = 0; i < RTL8821CE_MAX_TX_DESC; i++) {
            if (rtl->tx_buffers[i]) {
                heap_free(rtl->tx_buffers[i]);
                rtl->tx_buffers[i] = NULL;
            }
        }
        
        hal_memory_free(rtl->tx_desc);
        rtl->tx_desc = NULL;
    }
    
    if (rtl->rx_desc) {
        for (int i = 0; i < RTL8821CE_MAX_RX_DESC; i++) {
            if (rtl->rx_buffers[i]) {
                heap_free(rtl->rx_buffers[i]);
                rtl->rx_buffers[i] = NULL;
            }
        }
        
        hal_memory_free(rtl->rx_desc);
        rtl->rx_desc = NULL;
    }
}

// Reset hardware
static int rtl8821ce_hw_reset(rtl8821ce_device_t* rtl) {
    // Read system config register
    uint32_t sys_cfg = rtl8821ce_read_reg32(rtl, RTL8821CE_REG_SYS_CFG);
    
    // Set reset bit
    sys_cfg |= 0x00000001;
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_SYS_CFG, sys_cfg);
    
    // Wait for reset to complete (max 100ms)
    int timeout = 100;
    while (timeout > 0) {
        sys_cfg = rtl8821ce_read_reg32(rtl, RTL8821CE_REG_SYS_CFG);
        
        // Check if reset bit is cleared
        if ((sys_cfg & 0x00000001) == 0) {
            // Reset complete
            break;
        }
        
        hal_timer_sleep(1);
        timeout--;
    }
    
    if (timeout == 0) {
        log_error(RTL8821CE_TAG, "Hardware reset timeout");
        return -1;
    }
    
    return 0;
}

// Initialize hardware
static int rtl8821ce_hw_init(rtl8821ce_device_t* rtl) {
    // Perform hardware reset
    if (rtl8821ce_hw_reset(rtl) != 0) {
        return -1;
    }
    
    // Get MAC address
    for (int i = 0; i < 6; i++) {
        rtl->mac_addr[i] = rtl8821ce_read_reg8(rtl, RTL8821CE_REG_MAC_ADDR + i);
    }
    
    log_info(RTL8821CE_TAG, "MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             rtl->mac_addr[0], rtl->mac_addr[1], rtl->mac_addr[2],
             rtl->mac_addr[3], rtl->mac_addr[4], rtl->mac_addr[5]);
    
    // Set TX descriptor base address
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_TX_DESC_BASE, rtl->tx_desc_phys);
    
    // Set RX descriptor base address
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_RX_DESC_BASE, rtl->rx_desc_phys);
    
    // Enable TX and RX
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_TX_STATUS, 0x00000001);
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_RX_STATUS, 0x00000001);
    
    // Enable interrupts
    uint32_t int_mask = RTL8821CE_INT_TX_OK | RTL8821CE_INT_RX_OK |
                        RTL8821CE_INT_TX_ERR | RTL8821CE_INT_RX_ERR |
                        RTL8821CE_INT_LINK_CHG;
    
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_INT_MASK, int_mask);
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_INT_ENABLE, 0x00000001);
    
    return 0;
}

// IRQ handler
static void rtl8821ce_irq_handler(registers_t* regs) {
    // Find the controller that generated the interrupt
    rtl8821ce_device_t* rtl = NULL;
    
    for (int i = 0; i < num_controllers; i++) {
        if (controllers[i].initialized && controllers[i].irq == regs->int_no) {
            rtl = &controllers[i];
            break;
        }
    }
    
    if (!rtl) {
        return; // Not our interrupt
    }
    
    // Read interrupt status register
    uint32_t status = rtl8821ce_read_reg32(rtl, RTL8821CE_REG_INT_STATUS);
    
    // Acknowledge interrupts
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_INT_STATUS, status);
    
    // Handle TX complete
    if (status & RTL8821CE_INT_TX_OK) {
        // TX packet has been sent
        log_debug(RTL8821CE_TAG, "TX complete");
        
        // Process TX descriptors (mark as available)
        for (int i = 0; i < RTL8821CE_MAX_TX_DESC; i++) {
            if (!(rtl->tx_desc[i].flags & RTL8821CE_DESC_FLAG_OWN)) {
                // This descriptor is not owned by the hardware, so it's complete
                // No specific action needed here, just making it available for reuse
            }
        }
    }
    
    // Handle RX complete
    if (status & RTL8821CE_INT_RX_OK) {
        // New packet received
        log_debug(RTL8821CE_TAG, "RX complete");
        
        // Process RX descriptors
        uint32_t processed = 0;
        
        for (int i = 0; i < RTL8821CE_MAX_RX_DESC; i++) {
            uint32_t idx = (rtl->rx_index + i) % RTL8821CE_MAX_RX_DESC;
            
            // Check if descriptor is owned by driver (i.e., packet received)
            if (!(rtl->rx_desc[idx].flags & RTL8821CE_DESC_FLAG_OWN)) {
                // Get packet length
                uint32_t length = rtl->rx_desc[idx].flags & RTL8821CE_DESC_FLAG_LENGTH_MASK;
                
                if (length > 0 && length <= RTL8821CE_MAX_PACKET_SIZE) {
                    // Valid packet received
                    
                    // If there's a network interface, pass the packet up the stack
                    if (rtl->net_if) {
                        net_if_receive(rtl->net_if, rtl->rx_buffers[idx], length);
                    }
                }
                
                // Reset descriptor for reuse
                rtl->rx_desc[idx].flags = RTL8821CE_DESC_FLAG_OWN;
                processed++;
            }
        }
        
        // Update RX index
        rtl->rx_index = (rtl->rx_index + processed) % RTL8821CE_MAX_RX_DESC;
    }
    
    // Handle link change
    if (status & RTL8821CE_INT_LINK_CHG) {
        // Link status has changed
        // In a real driver, we would read PHY registers to determine new status
        bool old_link = rtl->link_up;
        rtl->link_up = true; // Simplification - assume link is up
        rtl->link_speed = 54; // Simplification - assume 54 Mbps
        
        if (old_link != rtl->link_up) {
            log_info(RTL8821CE_TAG, "Link status changed: %s", 
                     rtl->link_up ? "connected" : "disconnected");
            
            if (rtl->net_if) {
                net_if_update_link(rtl->net_if, rtl->link_up, rtl->link_speed);
            }
        }
    }
    
    // Handle errors
    if (status & (RTL8821CE_INT_TX_ERR | RTL8821CE_INT_RX_ERR)) {
        log_warning(RTL8821CE_TAG, "Hardware error: %08X", status);
        
        // Reset TX/RX if needed
        if (status & RTL8821CE_INT_TX_ERR) {
            rtl8821ce_write_reg32(rtl, RTL8821CE_REG_TX_STATUS, 0x00000001);
        }
        
        if (status & RTL8821CE_INT_RX_ERR) {
            rtl8821ce_write_reg32(rtl, RTL8821CE_REG_RX_STATUS, 0x00000001);
        }
    }
}

/**
 * Initialize RTL8821CE driver
 */
int rtl8821ce_init(void) {
    log_info(RTL8821CE_TAG, "Initializing RTL8821CE driver");
    
    // Initialize controller array
    memset(controllers, 0, sizeof(controllers));
    num_controllers = 0;
    
    // Register with PCI subsystem
    int result = pci_register_driver(&rtl8821ce_driver);
    if (result != 0) {
        log_error(RTL8821CE_TAG, "Failed to register RTL8821CE PCI driver: %d", result);
        return -1;
    }
    
    log_info(RTL8821CE_TAG, "RTL8821CE driver initialized");
    return 0;
}

/**
 * Probe for RTL8821CE devices
 */
static int rtl8821ce_probe(pci_device_t* dev) {
    // Check if this is an RTL8821CE device
    if (dev->id.vendor_id == RTL8821CE_VENDOR_ID && dev->id.device_id == RTL8821CE_DEVICE_ID) {
        log_info(RTL8821CE_TAG, "Found RTL8821CE WiFi controller");
        return 0;  // Match found
    }
    
    return -1;  // Not an RTL8821CE device
}

/**
 * Initialize RTL8821CE device
 */
static int rtl8821ce_initialize(pci_device_t* dev) {
    log_info(RTL8821CE_TAG, "Initializing RTL8821CE WiFi controller");
    
    // Check if we have room for another controller
    if (num_controllers >= RTL8821CE_MAX_CONTROLLERS) {
        log_error(RTL8821CE_TAG, "Maximum number of RTL8821CE controllers reached");
        return -1;
    }
    
    // Allocate a controller
    rtl8821ce_device_t* rtl = &controllers[num_controllers];
    memset(rtl, 0, sizeof(rtl8821ce_device_t));
    
    // Store private data in PCI device structure
    dev->private_data = rtl;
    
    // Enable PCI bus mastering and memory space
    pci_enable_bus_mastering(dev);
    pci_enable_memory_space(dev);
    
    // Map BAR 0 (MMIO registers)
    uint32_t mmio_base;
    uint32_t mmio_size;
    bool is_io;
    
    if (pci_get_bar_info(dev, 0, &mmio_base, &mmio_size, &is_io) != 0 || is_io) {
        log_error(RTL8821CE_TAG, "Failed to get MMIO BAR information");
        return -1;
    }
    
    // Map the MMIO registers into virtual memory
    void* mmio_virt;
    if (hal_memory_map_physical(mmio_base, mmio_size, HAL_MEMORY_UNCACHEABLE, &mmio_virt) != HAL_SUCCESS) {
        log_error(RTL8821CE_TAG, "Failed to map MMIO registers");
        return -1;
    }
    
    rtl->mmio_base = (uint32_t)mmio_virt;
    
    // Initialize command mutex
    mutex_init(&rtl->tx_mutex);
    
    // Initialize descriptors
    if (rtl8821ce_init_descriptors(rtl) != 0) {
        log_error(RTL8821CE_TAG, "Failed to initialize descriptors");
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Setup IRQ
    rtl->irq = dev->id.interrupt_line;
    
    if (hal_interrupt_register_handler(rtl->irq, rtl8821ce_irq_handler) != HAL_SUCCESS) {
        log_error(RTL8821CE_TAG, "Failed to register IRQ handler");
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Initialize hardware
    if (rtl8821ce_hw_init(rtl) != 0) {
        log_error(RTL8821CE_TAG, "Failed to initialize hardware");
        hal_interrupt_unregister_handler(rtl->irq);
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Create network interface
    rtl->net_if = net_if_create("wlan0", NET_IF_TYPE_WIFI, rtl->mac_addr, &rtl8821ce_net_ops);
    if (!rtl->net_if) {
        log_error(RTL8821CE_TAG, "Failed to create network interface");
        hal_interrupt_unregister_handler(rtl->irq);
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Associate device with interface
    rtl->net_if->dev = rtl;
    
    // Setup WiFi device
    wifi_device_t* wifi_dev = (wifi_device_t*)heap_alloc(sizeof(wifi_device_t));
    if (!wifi_dev) {
        log_error(RTL8821CE_TAG, "Failed to allocate WiFi device structure");
        net_if_destroy(rtl->net_if);
        hal_interrupt_unregister_handler(rtl->irq);
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Initialize WiFi device
    memset(wifi_dev, 0, sizeof(wifi_device_t));
    strncpy(wifi_dev->name, "wlan0", sizeof(wifi_dev->name));
    wifi_dev->ops = &rtl8821ce_wifi_ops;
    wifi_dev->net_if = rtl->net_if;
    wifi_dev->private_data = rtl;
    wifi_dev->type = WIFI_TYPE_80211AC;
    
    // Register WiFi device
    if (wifi_register_device(wifi_dev) != 0) {
        log_error(RTL8821CE_TAG, "Failed to register WiFi device");
        heap_free(wifi_dev);
        net_if_destroy(rtl->net_if);
        hal_interrupt_unregister_handler(rtl->irq);
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Create device in the device manager
    device_t* device = (device_t*)heap_alloc(sizeof(device_t));
    if (!device) {
        log_error(RTL8821CE_TAG, "Failed to allocate device structure");
        wifi_unregister_device(wifi_dev);
        heap_free(wifi_dev);
        net_if_destroy(rtl->net_if);
        hal_interrupt_unregister_handler(rtl->irq);
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Initialize device structure
    memset(device, 0, sizeof(device_t));
    
    strncpy(device->name, "rtl8821ce", sizeof(device->name));
    device->type = DEVICE_TYPE_NETWORK;
    device->subtype = DEVICE_SUBTYPE_WIFI;
    device->status = DEVICE_STATUS_ENABLED;
    device->irq = rtl->irq;
    device->vendor_id = dev->id.vendor_id;
    device->device_id = dev->id.device_id;
    device->private_data = rtl;
    device->ops = &rtl8821ce_dev_ops;
    
    // Register the device with the device manager
    if (device_register(device) != DEVICE_OK) {
        log_error(RTL8821CE_TAG, "Failed to register device");
        heap_free(device);
        wifi_unregister_device(wifi_dev);
        heap_free(wifi_dev);
        net_if_destroy(rtl->net_if);
        hal_interrupt_unregister_handler(rtl->irq);
        rtl8821ce_free_descriptors(rtl);
        hal_memory_unmap(mmio_virt, mmio_size);
        return -1;
    }
    
    // Mark controller as initialized
    rtl->initialized = true;
    num_controllers++;
    
    log_info(RTL8821CE_TAG, "RTL8821CE WiFi controller initialized");
    return 0;
}

/**
 * Remove RTL8821CE device
 */
static int rtl8821ce_remove(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    log_info(RTL8821CE_TAG, "Removing RTL8821CE controller");
    
    // Disable interrupts
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_INT_ENABLE, 0x00000000);
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_INT_MASK, 0x00000000);
    
    // Disable TX/RX
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_TX_STATUS, 0x00000000);
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_RX_STATUS, 0x00000000);
    
    // Unregister IRQ handler
    hal_interrupt_unregister_handler(rtl->irq);
    
    // Free descriptor rings
    rtl8821ce_free_descriptors(rtl);
    
    // Unmap MMIO region
    if (rtl->mmio_base) {
        hal_memory_unmap((void*)rtl->mmio_base, 0);  // Size not known at this point
    }
    
    // Cleanup network interface
    if (rtl->net_if) {
        net_if_destroy(rtl->net_if);
        rtl->net_if = NULL;
    }
    
    // Clear controller data
    dev->private_data = NULL;
    memset(rtl, 0, sizeof(rtl8821ce_device_t));
    
    // Decrease controller count
    if (num_controllers > 0) {
        num_controllers--;
    }
    
    return 0;
}

/**
 * Suspend RTL8821CE device
 */
static int rtl8821ce_suspend(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    log_info(RTL8821CE_TAG, "Suspending RTL8821CE controller");
    
    // Disable interrupts
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_INT_ENABLE, 0x00000000);
    
    // Disable TX/RX
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_TX_STATUS, 0x00000000);
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_RX_STATUS, 0x00000000);
    
    return 0;
}

/**
 * Resume RTL8821CE device
 */
static int rtl8821ce_resume(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    log_info(RTL8821CE_TAG, "Resuming RTL8821CE controller");
    
    // Re-initialize hardware
    return rtl8821ce_hw_init(rtl);
}

/**
 * Transmit a packet
 */
int rtl8821ce_transmit(device_t* dev, const void* buffer, size_t length) {
    if (!dev || !dev->private_data || !buffer) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Check length
    if (length == 0 || length > RTL8821CE_MAX_PACKET_SIZE) {
        return -1;
    }
    
    // Lock TX mutex
    mutex_lock(&rtl->tx_mutex);
    
    // Find a free TX descriptor
    int idx = -1;
    for (int i = 0; i < RTL8821CE_MAX_TX_DESC; i++) {
        uint32_t curr_idx = (rtl->tx_index + i) % RTL8821CE_MAX_TX_DESC;
        
        if (!(rtl->tx_desc[curr_idx].flags & RTL8821CE_DESC_FLAG_OWN)) {
            idx = curr_idx;
            break;
        }
    }
    
    if (idx == -1) {
        mutex_unlock(&rtl->tx_mutex);
        return 0;  // No free descriptors
    }
    
    // Copy data to TX buffer
    memcpy(rtl->tx_buffers[idx], buffer, length);
    
    // Setup descriptor
    rtl->tx_desc[idx].buf_size = length;
    rtl->tx_desc[idx].flags = RTL8821CE_DESC_FLAG_FS | RTL8821CE_DESC_FLAG_LS | 
                             RTL8821CE_DESC_FLAG_OWN | length;
    
    // Update TX index
    rtl->tx_index = (idx + 1) % RTL8821CE_MAX_TX_DESC;
    
    // Notify hardware of new TX descriptor
    rtl8821ce_write_reg32(rtl, RTL8821CE_REG_TX_STATUS, 0x00000001);
    
    mutex_unlock(&rtl->tx_mutex);
    
    return length;
}

/**
 * Receive a packet
 */
int rtl8821ce_receive(device_t* dev, void* buffer, size_t max_len) {
    if (!dev || !dev->private_data || !buffer) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Find a completed RX descriptor
    int idx = -1;
    int length = 0;
    
    for (int i = 0; i < RTL8821CE_MAX_RX_DESC; i++) {
        uint32_t curr_idx = (rtl->rx_index + i) % RTL8821CE_MAX_RX_DESC;
        
        if (!(rtl->rx_desc[curr_idx].flags & RTL8821CE_DESC_FLAG_OWN)) {
            idx = curr_idx;
            length = rtl->rx_desc[curr_idx].flags & RTL8821CE_DESC_FLAG_LENGTH_MASK;
            break;
        }
    }
    
    if (idx == -1 || length == 0 || length > max_len) {
        return 0;  // No packets or buffer too small
    }
    
    // Copy data to user buffer
    memcpy(buffer, rtl->rx_buffers[idx], length);
    
    // Reset descriptor for reuse
    rtl->rx_desc[idx].flags = RTL8821CE_DESC_FLAG_OWN;
    
    // Update RX index
    rtl->rx_index = (idx + 1) % RTL8821CE_MAX_RX_DESC;
    
    return length;
}

/**
 * Set MAC address
 */
int rtl8821ce_set_mac_address(device_t* dev, const uint8_t* mac_addr) {
    if (!dev || !dev->private_data || !mac_addr) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Write MAC address to hardware
    for (int i = 0; i < 6; i++) {
        rtl8821ce_write_reg8(rtl, RTL8821CE_REG_MAC_ADDR + i, mac_addr[i]);
    }
    
    // Store MAC address in context
    memcpy(rtl->mac_addr, mac_addr, 6);
    
    return 0;
}

/**
 * Get link status
 */
int rtl8821ce_get_link_status(device_t* dev, uint32_t* status, uint32_t* speed) {
    if (!dev || !dev->private_data || !status || !speed) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    *status = rtl->link_up ? 1 : 0;
    *speed = rtl->link_speed;
    
    return 0;
}

/**
 * Scan for wireless networks
 */
int rtl8821ce_scan(device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Clear existing network list
    rtl->num_networks = 0;
    
    // In a real driver, we would send a scan command to the hardware
    // and wait for results. For this example, we'll simulate finding networks.
    
    // Simulate finding an open network
    rtl8821ce_network_t* network = &rtl->networks[rtl->num_networks++];
    memset(network, 0, sizeof(rtl8821ce_network_t));
    strcpy(network->ssid, "OpenNetwork");
    network->ssid_len = strlen(network->ssid);
    network->channel = 6;
    network->signal_strength = 70;
    network->security = WIFI_SECURITY_NONE;
    network->is_connected = false;
    
    // Generate a random BSSID
    for (int i = 0; i < 6; i++) {
        network->bssid[i] = (uint8_t)(0x00 + i);
    }
    
    // Simulate finding a WPA2 secured network
    if (rtl->num_networks < RTL8821CE_MAX_NETWORKS) {
        network = &rtl->networks[rtl->num_networks++];
        memset(network, 0, sizeof(rtl8821ce_network_t));
        strcpy(network->ssid, "SecureNetwork");
        network->ssid_len = strlen(network->ssid);
        network->channel = 11;
        network->signal_strength = 85;
        network->security = WIFI_SECURITY_WPA2;
        network->is_connected = false;
        
        // Generate a random BSSID
        for (int i = 0; i < 6; i++) {
            network->bssid[i] = (uint8_t)(0x10 + i);
        }
    }
    
    // Simulate finding a WEP network
    if (rtl->num_networks < RTL8821CE_MAX_NETWORKS) {
        network = &rtl->networks[rtl->num_networks++];
        memset(network, 0, sizeof(rtl8821ce_network_t));
        strcpy(network->ssid, "LegacyNetwork");
        network->ssid_len = strlen(network->ssid);
        network->channel = 1;
        network->signal_strength = 30;
        network->security = WIFI_SECURITY_WEP;
        network->is_connected = false;
        
        // Generate a random BSSID
        for (int i = 0; i < 6; i++) {
            network->bssid[i] = (uint8_t)(0x20 + i);
        }
    }
    
    log_info(RTL8821CE_TAG, "Scan complete, found %u networks", rtl->num_networks);
    
    return 0;
}

/**
 * Connect to a wireless network
 */
int rtl8821ce_connect(device_t* dev, const char* ssid, const char* password) {
    if (!dev || !dev->private_data || !ssid) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // In a real driver, we would send connection parameters to hardware
    // and wait for connection status. For this example, we'll simulate.
    
    // Find the network in our scan list
    int idx = -1;
    for (uint32_t i = 0; i < rtl->num_networks; i++) {
        if (strcmp(rtl->networks[i].ssid, ssid) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx == -1) {
        log_error(RTL8821CE_TAG, "Network '%s' not found in scan results", ssid);
        return -1;
    }
    
    // Check if password is provided for secured networks
    if ((rtl->networks[idx].security != WIFI_SECURITY_NONE) && !password) {
        log_error(RTL8821CE_TAG, "Password required for secured network");
        return -1;
    }
    
    // Disconnect from any currently connected network
    for (uint32_t i = 0; i < rtl->num_networks; i++) {
        rtl->networks[i].is_connected = false;
    }
    
    // Mark new network as connected
    rtl->networks[idx].is_connected = true;
    
    // Update link status
    rtl->link_up = true;
    
    // Set speed based on signal strength
    if (rtl->networks[idx].signal_strength > 70) {
        rtl->link_speed = 54;  // Good signal, full speed
    } else if (rtl->networks[idx].signal_strength > 40) {
        rtl->link_speed = 24;  // Medium signal
    } else {
        rtl->link_speed = 11;  // Poor signal
    }
    
    // Notify network layer of link change
    if (rtl->net_if) {
        net_if_update_link(rtl->net_if, rtl->link_up, rtl->link_speed);
    }
    
    log_info(RTL8821CE_TAG, "Connected to network '%s' with signal strength %u%%",
             ssid, rtl->networks[idx].signal_strength);
    
    return 0;
}

/**
 * Disconnect from wireless network
 */
int rtl8821ce_disconnect(device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Clear connected flag for all networks
    for (uint32_t i = 0; i < rtl->num_networks; i++) {
        rtl->networks[i].is_connected = false;
    }
    
    // Update link status
    rtl->link_up = false;
    rtl->link_speed = 0;
    
    // Notify network layer of link change
    if (rtl->net_if) {
        net_if_update_link(rtl->net_if, rtl->link_up, rtl->link_speed);
    }
    
    log_info(RTL8821CE_TAG, "Disconnected from wireless network");
    
    return 0;
}

/**
 * Get current network information
 */
int rtl8821ce_get_network_info(device_t* dev, wifi_network_info_t* network) {
    if (!dev || !dev->private_data || !network) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Find the connected network
    int idx = -1;
    for (uint32_t i = 0; i < rtl->num_networks; i++) {
        if (rtl->networks[i].is_connected) {
            idx = i;
            break;
        }
    }
    
    if (idx == -1) {
        return -1;  // Not connected
    }
    
    // Copy network information
    rtl8821ce_network_t* rtl_net = &rtl->networks[idx];
    
    memcpy(network->bssid, rtl_net->bssid, 6);
    strncpy(network->ssid, rtl_net->ssid, sizeof(network->ssid));
    network->ssid[sizeof(network->ssid) - 1] = '\0';
    network->channel = rtl_net->channel;
    network->signal_strength = rtl_net->signal_strength;
    network->security = rtl_net->security;
    network->is_connected = true;
    
    return 0;
}

/**
 * Get list of available networks
 */
int rtl8821ce_get_networks(device_t* dev, wifi_network_info_t* networks, 
                          uint32_t max_networks, uint32_t* num_networks) {
    if (!dev || !dev->private_data || !networks || !num_networks) {
        return -1;
    }
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)dev->private_data;
    
    // Determine how many networks to copy
    uint32_t count = (rtl->num_networks < max_networks) ? rtl->num_networks : max_networks;
    
    // Copy network information
    for (uint32_t i = 0; i < count; i++) {
        rtl8821ce_network_t* rtl_net = &rtl->networks[i];
        wifi_network_info_t* net = &networks[i];
        
        memcpy(net->bssid, rtl_net->bssid, 6);
        strncpy(net->ssid, rtl_net->ssid, sizeof(net->ssid));
        net->ssid[sizeof(net->ssid) - 1] = '\0';
        net->channel = rtl_net->channel;
        net->signal_strength = rtl_net->signal_strength;
        net->security = rtl_net->security;
        net->is_connected = rtl_net->is_connected;
    }
    
    *num_networks = count;
    
    return 0;
}

/*
 * Device operations
 */

static int rtl8821ce_dev_open(device_t* dev, uint32_t flags) {
    return DEVICE_OK;
}

static int rtl8821ce_dev_close(device_t* dev) {
    return DEVICE_OK;
}

static int rtl8821ce_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset) {
    return rtl8821ce_receive(dev, buffer, size);
}

static int rtl8821ce_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset) {
    return rtl8821ce_transmit(dev, buffer, size);
}

static int rtl8821ce_dev_ioctl(device_t* dev, int request, void* arg) {
    switch (request) {
        case 0x8001:  // Scan
            return rtl8821ce_scan(dev);
            
        case 0x8002:  // Connect
            {
                struct {
                    char ssid[33];
                    char password[65];
                } *conn = (void*)arg;
                
                if (!conn) {
                    return DEVICE_ERROR_INVALID;
                }
                
                return rtl8821ce_connect(dev, conn->ssid, conn->password);
            }
            
        case 0x8003:  // Disconnect
            return rtl8821ce_disconnect(dev);
            
        case 0x8004:  // Get network info
            return rtl8821ce_get_network_info(dev, (wifi_network_info_t*)arg);
            
        case 0x8005:  // Get networks
            {
                struct {
                    wifi_network_info_t* networks;
                    uint32_t max_networks;
                    uint32_t* num_networks;
                } *req = (void*)arg;
                
                if (!req) {
                    return DEVICE_ERROR_INVALID;
                }
                
                return rtl8821ce_get_networks(dev, req->networks, req->max_networks, req->num_networks);
            }
            
        case 0x8006:  // Set MAC address
            return rtl8821ce_set_mac_address(dev, (uint8_t*)arg);
            
        case 0x8007:  // Get link status
            {
                struct {
                    uint32_t status;
                    uint32_t speed;
                } *link = (void*)arg;
                
                if (!link) {
                    return DEVICE_ERROR_INVALID;
                }
                
                return rtl8821ce_get_link_status(dev, &link->status, &link->speed);
            }
            
        default:
            return DEVICE_ERROR_UNSUPPORTED;
    }
}

/*
 * Network interface operations
 */

static int rtl8821ce_net_transmit(net_if_t* net_if, const void* buffer, size_t length) {
    if (!net_if || !net_if->dev || !buffer) {
        return -1;
    }
    
    return rtl8821ce_transmit((device_t*)net_if->dev, buffer, length);
}

static int rtl8821ce_net_set_hw_address(net_if_t* net_if, const uint8_t* mac_addr) {
    if (!net_if || !net_if->dev || !mac_addr) {
        return -1;
    }
    
    return rtl8821ce_set_mac_address((device_t*)net_if->dev, mac_addr);
}

static int rtl8821ce_net_get_stats(net_if_t* net_if, net_if_stats_t* stats) {
    if (!net_if || !net_if->dev || !stats) {
        return -1;
    }
    
    // In a real driver, we would read statistics from hardware registers
    memset(stats, 0, sizeof(net_if_stats_t));
    
    rtl8821ce_device_t* rtl = (rtl8821ce_device_t*)net_if->dev;
    
    // Set link status
    stats->link_up = rtl->link_up;
    stats->link_speed = rtl->link_speed;
    
    return 0;
}

/*
 * WiFi operations
 */

static int rtl8821ce_wifi_scan(wifi_device_t* wifi_dev) {
    if (!wifi_dev || !wifi_dev->private_data) {
        return -1;
    }
    
    return rtl8821ce_scan((device_t*)wifi_dev->private_data);
}

static int rtl8821ce_wifi_connect(wifi_device_t* wifi_dev, const char* ssid, const char* password) {
    if (!wifi_dev || !wifi_dev->private_data || !ssid) {
        return -1;
    }
    
    return rtl8821ce_connect((device_t*)wifi_dev->private_data, ssid, password);
}

static int rtl8821ce_wifi_disconnect(wifi_device_t* wifi_dev) {
    if (!wifi_dev || !wifi_dev->private_data) {
        return -1;
    }
    
    return rtl8821ce_disconnect((device_t*)wifi_dev->private_data);
}

static int rtl8821ce_wifi_get_network_info(wifi_device_t* wifi_dev, wifi_network_info_t* network) {
    if (!wifi_dev || !wifi_dev->private_data || !network) {
        return -1;
    }
    
    return rtl8821ce_get_network_info((device_t*)wifi_dev->private_data, network);
}

static int rtl8821ce_wifi_get_networks(wifi_device_t* wifi_dev, wifi_network_info_t* networks, 
                                      uint32_t max_networks, uint32_t* num_networks) {
    if (!wifi_dev || !wifi_dev->private_data || !networks || !num_networks) {
        return -1;
    }
    
    return rtl8821ce_get_networks((device_t*)wifi_dev->private_data, networks, max_networks, num_networks);
}