/**
 * @file intel_gpu.h
 * @brief Intel GPU driver for uintOS
 * 
 * This file provides driver support for Intel integrated graphics chipsets
 * including basic framebuffer setup and mode switching.
 *
 * Supported hardware:
 * - Intel HD Graphics series
 * - Intel UHD Graphics series
 * - Legacy Intel GMA series
 */

#ifndef INTEL_GPU_H
#define INTEL_GPU_H

#include <stdint.h>
#include <stdbool.h>
#include "../../../kernel/device_manager.h"
#include "../../../drivers/pci/pci.h"

// Intel GPU driver version
#define INTEL_GPU_DRV_VERSION 0x00010000  // 1.0.0.0

// Intel device IDs (partial list of common ones)
#define INTEL_VID                 0x8086  // Intel Vendor ID

// Intel HD/UHD Graphics and Iris series
#define INTEL_HD_2000_3000        0x0102  // Sandy Bridge HD Graphics
#define INTEL_HD_2500_4000        0x0162  // Ivy Bridge HD Graphics  
#define INTEL_HD_4200_5200        0x0412  // Haswell HD Graphics
#define INTEL_HD_510_580          0x1912  // Skylake HD Graphics
#define INTEL_UHD_610_655         0x3E92  // Coffee Lake UHD Graphics
#define INTEL_UHD_710_770         0x4692  // Alder Lake UHD Graphics

// Older Intel GMA series
#define INTEL_GMA_900             0x2582  // GMA 900
#define INTEL_GMA_950             0x2772  // GMA 950
#define INTEL_GMA_X3100           0x2A02  // GMA X3100

// Display modes
#define DISPLAY_MODE_TEXT         0x01    // Text mode
#define DISPLAY_MODE_GRAPHICS     0x02    // Graphics mode

// Graphics mode resolutions
#define GRAPHICS_MODE_640x480     0x01    // 640x480
#define GRAPHICS_MODE_800x600     0x02    // 800x600
#define GRAPHICS_MODE_1024x768    0x03    // 1024x768
#define GRAPHICS_MODE_1280x720    0x04    // 1280x720
#define GRAPHICS_MODE_1280x1024   0x05    // 1280x1024
#define GRAPHICS_MODE_1920x1080   0x06    // 1920x1080

// Graphics mode color depths
#define COLOR_DEPTH_8BPP          0x01    // 8 bits per pixel
#define COLOR_DEPTH_16BPP         0x02    // 16 bits per pixel
#define COLOR_DEPTH_24BPP         0x03    // 24 bits per pixel
#define COLOR_DEPTH_32BPP         0x04    // 32 bits per pixel

// Intel GPU register definitions
#define INTEL_REG_GMBUS0          0x5100  // GMBUS Clock/Port Select
#define INTEL_REG_GMBUS1          0x5104  // GMBUS Command/Status
#define INTEL_REG_GMBUS2          0x5108  // GMBUS Status
#define INTEL_REG_GMBUS3          0x510C  // GMBUS Data
#define INTEL_REG_GMBUS4          0x5110  // GMBUS Interrupt Mask
#define INTEL_REG_GMBUS5          0x5120  // GMBUS 2 Control

#define INTEL_REG_PIPEACONF       0x70008 // Pipe A Configuration
#define INTEL_REG_PIPEBCONF       0x71008 // Pipe B Configuration
#define INTEL_REG_PIPECCONF       0x72008 // Pipe C Configuration

#define INTEL_REG_DSPASURF        0x7019C // Display A Surface Address
#define INTEL_REG_DSPASTRIDE      0x70188 // Display A Stride
#define INTEL_REG_DSPAPOS         0x7018C // Display A Position
#define INTEL_REG_DSPASIZE        0x70190 // Display A Size
#define INTEL_REG_DSPAADDR        0x70184 // Display A Base Address

// Intel GPU private device structure
typedef struct {
    uint32_t mmio_base;           // Memory mapped register base address
    uint32_t fb_base;             // Framebuffer base address
    uint32_t fb_size;             // Framebuffer size
    uint8_t  current_mode;        // Current display mode
    uint8_t  current_resolution;  // Current resolution
    uint8_t  current_color_depth; // Current color depth
    uint32_t width;               // Current width in pixels
    uint32_t height;              // Current height in pixels
    uint32_t pitch;               // Current pitch in bytes
    uint8_t  bpp;                 // Current bits per pixel
    void*    fb_virt;             // Virtual address of framebuffer mapping
    bool     initialized;         // Whether the device has been initialized
} intel_gpu_device_t;

/**
 * Initialize Intel GPU driver
 * 
 * @return 0 on success, negative error code on failure
 */
int intel_gpu_init(void);

/**
 * Set display mode on Intel GPU
 * 
 * @param dev Device pointer
 * @param mode Display mode (text/graphics)
 * @param resolution Resolution identifier
 * @param color_depth Color depth identifier
 * @return 0 on success, negative error code on failure
 */
int intel_gpu_set_mode(device_t* dev, uint8_t mode, uint8_t resolution, uint8_t color_depth);

/**
 * Draw pixel at the specified position
 * 
 * @param dev Device pointer
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Color value (format depends on color depth)
 * @return 0 on success, negative error code on failure
 */
int intel_gpu_draw_pixel(device_t* dev, uint32_t x, uint32_t y, uint32_t color);

/**
 * Clear screen with specified color
 * 
 * @param dev Device pointer
 * @param color Color value (format depends on color depth)
 * @return 0 on success, negative error code on failure
 */
int intel_gpu_clear_screen(device_t* dev, uint32_t color);

/**
 * Get current display information
 * 
 * @param dev Device pointer
 * @param width Pointer to store width
 * @param height Pointer to store height
 * @param bpp Pointer to store bits per pixel
 * @return 0 on success, negative error code on failure
 */
int intel_gpu_get_display_info(device_t* dev, uint32_t* width, uint32_t* height, uint8_t* bpp);

/**
 * Get framebuffer address and size
 * 
 * @param dev Device pointer
 * @param fb_addr Pointer to store framebuffer address
 * @param fb_size Pointer to store framebuffer size
 * @return 0 on success, negative error code on failure
 */
int intel_gpu_get_framebuffer(device_t* dev, void** fb_addr, uint32_t* fb_size);

#endif /* INTEL_GPU_H */