/**
 * @file intel_gpu.c
 * @brief Intel GPU driver implementation
 * 
 * This file implements the driver for Intel integrated graphics chipsets
 */

#include "intel_gpu.h"
#include "../../../kernel/logging/log.h"
#include "../../../memory/heap.h"
#include "../../../hal/include/hal_io.h"
#include "../../../hal/include/hal_memory.h"
#include <string.h>

#define INTEL_GPU_TAG "INTEL_GPU"

// Supported Intel GPU device IDs
static uint16_t intel_gpu_device_ids[] = {
    INTEL_HD_2000_3000,   // Sandy Bridge HD Graphics
    INTEL_HD_2500_4000,   // Ivy Bridge HD Graphics
    INTEL_HD_4200_5200,   // Haswell HD Graphics
    INTEL_HD_510_580,     // Skylake HD Graphics
    INTEL_UHD_610_655,    // Coffee Lake UHD Graphics
    INTEL_UHD_710_770,    // Alder Lake UHD Graphics
    INTEL_GMA_900,        // GMA 900
    INTEL_GMA_950,        // GMA 950
    INTEL_GMA_X3100       // GMA X3100
};

// Forward declarations for internal functions
static int intel_gpu_probe(pci_device_t* dev);
static int intel_gpu_initialize(pci_device_t* dev);
static int intel_gpu_remove(pci_device_t* dev);
static int intel_gpu_suspend(pci_device_t* dev);
static int intel_gpu_resume(pci_device_t* dev);

// Driver operation functions
static int intel_gpu_dev_open(device_t* dev, uint32_t flags);
static int intel_gpu_dev_close(device_t* dev);
static int intel_gpu_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset);
static int intel_gpu_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset);
static int intel_gpu_dev_ioctl(device_t* dev, int request, void* arg);

// PCI driver structure
static pci_driver_t intel_gpu_driver = {
    .name = "intel_gpu",
    .vendor_ids = &INTEL_VID,
    .device_ids = intel_gpu_device_ids,
    .num_supported_devices = sizeof(intel_gpu_device_ids) / sizeof(intel_gpu_device_ids[0]),
    .ops = {
        .probe = intel_gpu_probe,
        .init = intel_gpu_initialize,
        .remove = intel_gpu_remove,
        .suspend = intel_gpu_suspend,
        .resume = intel_gpu_resume
    }
};

// Device operations
static device_ops_t intel_gpu_dev_ops = {
    .open = intel_gpu_dev_open,
    .close = intel_gpu_dev_close,
    .read = intel_gpu_dev_read,
    .write = intel_gpu_dev_write,
    .ioctl = intel_gpu_dev_ioctl
};

/**
 * Register with MMIO address
 */
static inline uint32_t read_mmio(intel_gpu_device_t* gpu, uint32_t reg) {
    volatile uint32_t* addr = (volatile uint32_t*)(gpu->mmio_base + reg);
    return *addr;
}

/**
 * Write to MMIO address
 */
static inline void write_mmio(intel_gpu_device_t* gpu, uint32_t reg, uint32_t val) {
    volatile uint32_t* addr = (volatile uint32_t*)(gpu->mmio_base + reg);
    *addr = val;
}

/**
 * Initialize Intel GPU driver
 */
int intel_gpu_init(void) {
    log_info(INTEL_GPU_TAG, "Initializing Intel GPU driver");
    
    // Register with PCI subsystem
    int result = pci_register_driver(&intel_gpu_driver);
    if (result != 0) {
        log_error(INTEL_GPU_TAG, "Failed to register Intel GPU PCI driver: %d", result);
        return -1;
    }
    
    log_info(INTEL_GPU_TAG, "Intel GPU driver initialized");
    return 0;
}

/**
 * Probe for Intel GPU devices
 */
static int intel_gpu_probe(pci_device_t* dev) {
    log_info(INTEL_GPU_TAG, "Probing Intel GPU device: VID=%04X, DID=%04X", 
             dev->id.vendor_id, dev->id.device_id);
    
    // Check if this is a display controller
    if (dev->id.class_code == PCI_CLASS_DISPLAY && 
        dev->id.subclass == 0x00) {  // VGA compatible controller
        return 0;  // This is a supported Intel GPU
    }
    
    return -1;  // Not a supported GPU
}

/**
 * Initialize Intel GPU device
 */
static int intel_gpu_initialize(pci_device_t* dev) {
    log_info(INTEL_GPU_TAG, "Initializing Intel GPU device: VID=%04X, DID=%04X", 
             dev->id.vendor_id, dev->id.device_id);
    
    // Allocate private device data
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)heap_alloc(sizeof(intel_gpu_device_t));
    if (!gpu) {
        log_error(INTEL_GPU_TAG, "Failed to allocate device structure");
        return -1;
    }
    
    // Clear the structure
    memset(gpu, 0, sizeof(intel_gpu_device_t));
    
    // Store the private data in the PCI device structure
    dev->private_data = gpu;
    
    // Enable PCI memory space
    pci_enable_memory_space(dev);
    
    // Map BAR 0 (MMIO registers)
    uint32_t mmio_base;
    uint32_t mmio_size;
    bool is_io;
    
    if (pci_get_bar_info(dev, 0, &mmio_base, &mmio_size, &is_io) != 0 || is_io) {
        log_error(INTEL_GPU_TAG, "Failed to get MMIO BAR information");
        heap_free(gpu);
        dev->private_data = NULL;
        return -1;
    }
    
    // Map the MMIO registers into virtual memory
    void* mmio_virt;
    if (hal_memory_map_physical(mmio_base, mmio_size, HAL_MEMORY_UNCACHEABLE, &mmio_virt) != HAL_SUCCESS) {
        log_error(INTEL_GPU_TAG, "Failed to map MMIO registers");
        heap_free(gpu);
        dev->private_data = NULL;
        return -1;
    }
    
    gpu->mmio_base = (uint32_t)mmio_virt;
    
    // Map BAR 1 or BAR 2 (framebuffer) - depends on the specific Intel GPU model
    uint32_t fb_base = 0;
    uint32_t fb_size = 0;
    
    // Try BAR 1 first
    if (pci_get_bar_info(dev, 1, &fb_base, &fb_size, &is_io) == 0 && !is_io && fb_base != 0) {
        // BAR 1 is framebuffer
        log_info(INTEL_GPU_TAG, "Using framebuffer from BAR 1: address 0x%X, size %d MB", 
                 fb_base, fb_size / (1024 * 1024));
    } 
    // If BAR 1 doesn't work, try BAR 2
    else if (pci_get_bar_info(dev, 2, &fb_base, &fb_size, &is_io) == 0 && !is_io && fb_base != 0) {
        // BAR 2 is framebuffer
        log_info(INTEL_GPU_TAG, "Using framebuffer from BAR 2: address 0x%X, size %d MB", 
                 fb_base, fb_size / (1024 * 1024));
    } else {
        // Could not find framebuffer
        log_error(INTEL_GPU_TAG, "Failed to find framebuffer BAR");
        hal_memory_unmap(mmio_virt, mmio_size);
        heap_free(gpu);
        dev->private_data = NULL;
        return -1;
    }
    
    // Map the framebuffer into virtual memory (use cacheable memory for performance)
    void* fb_virt;
    if (hal_memory_map_physical(fb_base, fb_size, HAL_MEMORY_CACHEABLE, &fb_virt) != HAL_SUCCESS) {
        log_error(INTEL_GPU_TAG, "Failed to map framebuffer");
        hal_memory_unmap(mmio_virt, mmio_size);
        heap_free(gpu);
        dev->private_data = NULL;
        return -1;
    }
    
    gpu->fb_base = fb_base;
    gpu->fb_size = fb_size;
    gpu->fb_virt = fb_virt;
    
    // Set default mode (depends on GPU capabilities)
    // For simplicity, we'll go with 1024x768x32bpp as default
    gpu->current_mode = DISPLAY_MODE_GRAPHICS;
    gpu->current_resolution = GRAPHICS_MODE_1024x768;
    gpu->current_color_depth = COLOR_DEPTH_32BPP;
    gpu->width = 1024;
    gpu->height = 768;
    gpu->bpp = 32;
    gpu->pitch = gpu->width * (gpu->bpp / 8);
    
    // Configure display (this is highly hardware-specific and simplified here)
    // In a real driver, we would read EDID from the monitor and set appropriate mode
    
    // Clear screen to black
    memset(gpu->fb_virt, 0, gpu->width * gpu->height * (gpu->bpp / 8));
    
    // Create a device in the device manager
    device_t* display_device = (device_t*)heap_alloc(sizeof(device_t));
    if (display_device) {
        memset(display_device, 0, sizeof(device_t));
        
        // Set device properties
        snprintf(display_device->name, sizeof(display_device->name), "intel_gpu_%d", 0);
        display_device->type = DEVICE_TYPE_DISPLAY;
        display_device->status = DEVICE_STATUS_ENABLED;
        display_device->vendor_id = dev->id.vendor_id;
        display_device->device_id = dev->id.device_id;
        display_device->private_data = dev;
        display_device->ops = &intel_gpu_dev_ops;
        
        // Register the device with the device manager
        device_register(display_device);
        
        // Store OS device in PCI device structure
        dev->os_device = display_device;
    }
    
    gpu->initialized = true;
    log_info(INTEL_GPU_TAG, "Intel GPU initialization complete");
    
    return 0;
}

/**
 * Remove Intel GPU device
 */
static int intel_gpu_remove(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)dev->private_data;
    
    log_info(INTEL_GPU_TAG, "Removing Intel GPU device");
    
    // Unmap framebuffer
    if (gpu->fb_virt) {
        hal_memory_unmap(gpu->fb_virt, gpu->fb_size);
    }
    
    // Unmap MMIO registers
    if (gpu->mmio_base) {
        hal_memory_unmap((void*)gpu->mmio_base, 0); // Size unknown at this point, using 0
    }
    
    // Free device structure
    heap_free(gpu);
    dev->private_data = NULL;
    
    // Unregister device from device manager (if registered)
    if (dev->os_device) {
        device_unregister(dev->os_device);
        heap_free(dev->os_device);
        dev->os_device = NULL;
    }
    
    return 0;
}

/**
 * Suspend Intel GPU device
 */
static int intel_gpu_suspend(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)dev->private_data;
    
    log_info(INTEL_GPU_TAG, "Suspending Intel GPU device");
    
    // Save current state if needed
    // Power down display if needed
    
    return 0;
}

/**
 * Resume Intel GPU device
 */
static int intel_gpu_resume(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)dev->private_data;
    
    log_info(INTEL_GPU_TAG, "Resuming Intel GPU device");
    
    // Restore saved state if needed
    // Power up display if needed
    
    return 0;
}

/**
 * Open Intel GPU device
 */
static int intel_gpu_dev_open(device_t* dev, uint32_t flags) {
    log_debug(INTEL_GPU_TAG, "Opening Intel GPU device");
    
    // Nothing special needed for open
    return DEVICE_OK;
}

/**
 * Close Intel GPU device
 */
static int intel_gpu_dev_close(device_t* dev) {
    log_debug(INTEL_GPU_TAG, "Closing Intel GPU device");
    
    // Nothing special needed for close
    return DEVICE_OK;
}

/**
 * Read from Intel GPU device (typically reading from framebuffer)
 */
static int intel_gpu_dev_read(device_t* dev, void* buffer, size_t size, uint64_t offset) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Check if offset and size are within framebuffer bounds
    if (offset >= gpu->fb_size || offset + size > gpu->fb_size) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Copy data from framebuffer to user buffer
    memcpy(buffer, (uint8_t*)gpu->fb_virt + offset, size);
    
    return size;
}

/**
 * Write to Intel GPU device (typically writing to framebuffer)
 */
static int intel_gpu_dev_write(device_t* dev, const void* buffer, size_t size, uint64_t offset) {
    if (!dev || !dev->private_data || !buffer) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Check if offset and size are within framebuffer bounds
    if (offset >= gpu->fb_size || offset + size > gpu->fb_size) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Copy data from user buffer to framebuffer
    memcpy((uint8_t*)gpu->fb_virt + offset, buffer, size);
    
    return size;
}

/**
 * IOCTL for Intel GPU device
 */
static int intel_gpu_dev_ioctl(device_t* dev, int request, void* arg) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Handle different IOCTL requests
    switch (request) {
        case 0x1001:  // Set display mode
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                uint32_t* params = (uint32_t*)arg;
                uint8_t mode = params[0];
                uint8_t resolution = params[1];
                uint8_t color_depth = params[2];
                
                return intel_gpu_set_mode(dev, mode, resolution, color_depth);
            }
            
        case 0x1002:  // Clear screen
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                uint32_t color = *(uint32_t*)arg;
                return intel_gpu_clear_screen(dev, color);
            }
            
        case 0x1003:  // Get display info
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                uint32_t* params = (uint32_t*)arg;
                uint32_t* width = (uint32_t*)params[0];
                uint32_t* height = (uint32_t*)params[1];
                uint8_t* bpp = (uint8_t*)params[2];
                
                return intel_gpu_get_display_info(dev, width, height, bpp);
            }
            
        case 0x1004:  // Get framebuffer
            {
                if (!arg) {
                    return DEVICE_ERROR_INVALID;
                }
                
                uint32_t* params = (uint32_t*)arg;
                void** fb_addr = (void**)params[0];
                uint32_t* fb_size = (uint32_t*)params[1];
                
                return intel_gpu_get_framebuffer(dev, fb_addr, fb_size);
            }
            
        default:
            return DEVICE_ERROR_UNSUPPORTED;
    }
}

/**
 * Set display mode on Intel GPU
 */
int intel_gpu_set_mode(device_t* dev, uint8_t mode, uint8_t resolution, uint8_t color_depth) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Store current mode settings
    gpu->current_mode = mode;
    gpu->current_resolution = resolution;
    gpu->current_color_depth = color_depth;
    
    // Set resolution based on mode
    switch (resolution) {
        case GRAPHICS_MODE_640x480:
            gpu->width = 640;
            gpu->height = 480;
            break;
            
        case GRAPHICS_MODE_800x600:
            gpu->width = 800;
            gpu->height = 600;
            break;
            
        case GRAPHICS_MODE_1024x768:
            gpu->width = 1024;
            gpu->height = 768;
            break;
            
        case GRAPHICS_MODE_1280x720:
            gpu->width = 1280;
            gpu->height = 720;
            break;
            
        case GRAPHICS_MODE_1280x1024:
            gpu->width = 1280;
            gpu->height = 1024;
            break;
            
        case GRAPHICS_MODE_1920x1080:
            gpu->width = 1920;
            gpu->height = 1080;
            break;
            
        default:
            // Invalid resolution
            return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Set color depth
    switch (color_depth) {
        case COLOR_DEPTH_8BPP:
            gpu->bpp = 8;
            break;
            
        case COLOR_DEPTH_16BPP:
            gpu->bpp = 16;
            break;
            
        case COLOR_DEPTH_24BPP:
            gpu->bpp = 24;
            break;
            
        case COLOR_DEPTH_32BPP:
            gpu->bpp = 32;
            break;
            
        default:
            // Invalid color depth
            return DEVICE_ERROR_UNSUPPORTED;
    }
    
    // Update pitch
    gpu->pitch = gpu->width * (gpu->bpp / 8);
    
    // Configure display pipeline (this is highly hardware-specific)
    // This is a simplified approach; a real driver would do much more
    
    // Update display surface address and stride
    write_mmio(gpu, INTEL_REG_DSPAADDR, gpu->fb_base);
    write_mmio(gpu, INTEL_REG_DSPASTRIDE, gpu->pitch);
    
    // Update display size
    write_mmio(gpu, INTEL_REG_DSPASIZE, ((gpu->height - 1) << 16) | (gpu->width - 1));
    
    // Enable the pipe
    uint32_t pipe_conf = read_mmio(gpu, INTEL_REG_PIPEACONF);
    write_mmio(gpu, INTEL_REG_PIPEACONF, pipe_conf | 0x1); // Enable bit
    
    // Clear screen
    intel_gpu_clear_screen(dev, 0);
    
    return DEVICE_OK;
}

/**
 * Draw pixel at the specified position
 */
int intel_gpu_draw_pixel(device_t* dev, uint32_t x, uint32_t y, uint32_t color) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Check coordinates are within bounds
    if (x >= gpu->width || y >= gpu->height) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Calculate offset in framebuffer
    uint32_t offset = y * gpu->pitch + x * (gpu->bpp / 8);
    
    // Draw pixel based on color depth
    switch (gpu->bpp) {
        case 8:
            *((uint8_t*)gpu->fb_virt + offset) = (uint8_t)color;
            break;
            
        case 16:
            *((uint16_t*)((uint8_t*)gpu->fb_virt + offset)) = (uint16_t)color;
            break;
            
        case 24:
            // 24bpp is stored as 3 bytes
            ((uint8_t*)gpu->fb_virt)[offset] = (uint8_t)(color & 0xFF);
            ((uint8_t*)gpu->fb_virt)[offset+1] = (uint8_t)((color >> 8) & 0xFF);
            ((uint8_t*)gpu->fb_virt)[offset+2] = (uint8_t)((color >> 16) & 0xFF);
            break;
            
        case 32:
            *((uint32_t*)((uint8_t*)gpu->fb_virt + offset)) = color;
            break;
            
        default:
            return DEVICE_ERROR_UNSUPPORTED;
    }
    
    return DEVICE_OK;
}

/**
 * Clear screen with specified color
 */
int intel_gpu_clear_screen(device_t* dev, uint32_t color) {
    if (!dev || !dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Clear screen based on color depth
    switch (gpu->bpp) {
        case 8:
            memset(gpu->fb_virt, color & 0xFF, gpu->width * gpu->height);
            break;
            
        case 16:
            {
                uint16_t* fb16 = (uint16_t*)gpu->fb_virt;
                uint16_t color16 = (uint16_t)color;
                for (uint32_t i = 0; i < gpu->width * gpu->height; i++) {
                    fb16[i] = color16;
                }
            }
            break;
            
        case 24:
            {
                // 24bpp requires special handling
                uint8_t* fb24 = (uint8_t*)gpu->fb_virt;
                uint8_t r = (uint8_t)(color & 0xFF);
                uint8_t g = (uint8_t)((color >> 8) & 0xFF);
                uint8_t b = (uint8_t)((color >> 16) & 0xFF);
                
                for (uint32_t i = 0; i < gpu->width * gpu->height; i++) {
                    fb24[i*3] = r;
                    fb24[i*3+1] = g;
                    fb24[i*3+2] = b;
                }
            }
            break;
            
        case 32:
            {
                uint32_t* fb32 = (uint32_t*)gpu->fb_virt;
                for (uint32_t i = 0; i < gpu->width * gpu->height; i++) {
                    fb32[i] = color;
                }
            }
            break;
            
        default:
            return DEVICE_ERROR_UNSUPPORTED;
    }
    
    return DEVICE_OK;
}

/**
 * Get current display information
 */
int intel_gpu_get_display_info(device_t* dev, uint32_t* width, uint32_t* height, uint8_t* bpp) {
    if (!dev || !dev->private_data || !width || !height || !bpp) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Return current display information
    *width = gpu->width;
    *height = gpu->height;
    *bpp = gpu->bpp;
    
    return DEVICE_OK;
}

/**
 * Get framebuffer address and size
 */
int intel_gpu_get_framebuffer(device_t* dev, void** fb_addr, uint32_t* fb_size) {
    if (!dev || !dev->private_data || !fb_addr || !fb_size) {
        return DEVICE_ERROR_INVALID;
    }
    
    // Get the PCI device
    pci_device_t* pci_dev = (pci_device_t*)dev->private_data;
    if (!pci_dev || !pci_dev->private_data) {
        return DEVICE_ERROR_INVALID;
    }
    
    intel_gpu_device_t* gpu = (intel_gpu_device_t*)pci_dev->private_data;
    
    // Return framebuffer information
    *fb_addr = gpu->fb_virt;
    *fb_size = gpu->fb_size;
    
    return DEVICE_OK;
}