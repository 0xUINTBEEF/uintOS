/**
 * Graphics subsystem for uintOS
 * Provides framebuffer-based graphics operations including primitive drawing and text rendering
 */

#include "graphics.h"
#include "font8x8.h"
#include "../io.h"
#include "../vga.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../../hal/include/hal_io.h"

// Global framebuffer information
static framebuffer_t g_framebuffer = {0};
static graphics_mode_t g_current_mode = GRAPHICS_MODE_TEXT;

// VGA register port addresses
#define VGA_AC_INDEX      0x3C0
#define VGA_AC_WRITE      0x3C0
#define VGA_AC_READ       0x3C1
#define VGA_MISC_WRITE    0x3C2
#define VGA_SEQ_INDEX     0x3C4
#define VGA_SEQ_DATA      0x3C5
#define VGA_DAC_INDEX_READ 0x3C7
#define VGA_DAC_INDEX_WRITE 0x3C8
#define VGA_DAC_DATA      0x3C9
#define VGA_MISC_READ     0x3CC
#define VGA_GC_INDEX      0x3CE
#define VGA_GC_DATA       0x3CF
#define VGA_CRTC_INDEX    0x3D4
#define VGA_CRTC_DATA     0x3D5
#define VGA_INSTAT_READ   0x3DA

// Functions for VGA mode setting
static void write_registers(uint8_t* registers);
static void set_plane(unsigned p);
static void set_vga_mode13h();

/**
 * Initialize the graphics subsystem
 * @param mode The graphics mode to initialize
 * @return 0 on success, non-zero on failure
 */
int graphics_init(graphics_mode_t mode) {
    switch(mode) {
        case GRAPHICS_MODE_TEXT:
            // Already in text mode or switch back to text mode
            vga_init();
            g_current_mode = GRAPHICS_MODE_TEXT;
            break;
            
        case GRAPHICS_MODE_VGA_320_200:
            // Set up 320x200 mode 13h
            set_vga_mode13h();
            
            // Setup framebuffer information
            g_framebuffer.buffer = (uint8_t*)0xA0000;  // Standard VGA memory address
            g_framebuffer.width = 320;
            g_framebuffer.height = 200;
            g_framebuffer.pitch = 320;
            g_framebuffer.bpp = 8;
            g_framebuffer.type = 1;  // Linear framebuffer
            
            g_current_mode = GRAPHICS_MODE_VGA_320_200;
            break;
            
        case GRAPHICS_MODE_VESA_640_480:
        case GRAPHICS_MODE_VESA_800_600:
        case GRAPHICS_MODE_VESA_1024_768:
            // VESA modes will require VESA BIOS extensions
            // Not implemented in this basic version
            return -1;
            
        default:
            return -1;
    }
    
    return 0;
}

/**
 * Clear the screen with a color
 * @param color The color to clear with
 */
void graphics_clear(uint32_t color) {
    if (g_current_mode == GRAPHICS_MODE_TEXT) {
        // Use VGA text mode clear
        vga_clear_screen();
        return;
    }
    
    // For graphics mode, fill the framebuffer
    if (g_framebuffer.bpp == 8) {
        // 8-bit color mode
        memset(g_framebuffer.buffer, color & 0xFF, g_framebuffer.pitch * g_framebuffer.height);
    } else {
        // Other modes - draw each pixel
        for (uint32_t y = 0; y < g_framebuffer.height; y++) {
            for (uint32_t x = 0; x < g_framebuffer.width; x++) {
                graphics_draw_pixel(x, y, color);
            }
        }
    }
}

/**
 * Draw a pixel at (x,y) with the specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param color The color to draw
 */
void graphics_draw_pixel(int x, int y, uint32_t color) {
    // Bounds check
    if (x < 0 || x >= (int)g_framebuffer.width || 
        y < 0 || y >= (int)g_framebuffer.height) {
        return;
    }

    if (g_current_mode == GRAPHICS_MODE_TEXT) {
        // In text mode, can't draw individual pixels
        return;
    }
    
    if (g_framebuffer.bpp == 8) {
        // 8-bit color mode (Mode 13h)
        uint8_t* pixel = g_framebuffer.buffer + y * g_framebuffer.pitch + x;
        *pixel = color & 0xFF;
    } else {
        // Other bit depths not implemented in this basic version
    }
}

/**
 * Draw a line from (x1,y1) to (x2,y2) with the specified color
 * Using Bresenham's line algorithm
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color The color to draw
 */
void graphics_draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int e2;
    
    while (1) {
        graphics_draw_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/**
 * Draw a rectangle with the specified dimensions and color
 * @param x X coordinate of the top-left corner
 * @param y Y coordinate of the top-left corner
 * @param width Width of the rectangle
 * @param height Height of the rectangle
 * @param color The color to draw
 * @param filled Whether to fill the rectangle
 */
void graphics_draw_rect(int x, int y, int width, int height, uint32_t color, int filled) {
    if (filled) {
        // Fill the rectangle
        for (int j = y; j < y + height; j++) {
            for (int i = x; i < x + width; i++) {
                graphics_draw_pixel(i, j, color);
            }
        }
    } else {
        // Draw the outline
        graphics_draw_line(x, y, x + width - 1, y, color);                 // Top
        graphics_draw_line(x, y + height - 1, x + width - 1, y + height - 1, color); // Bottom
        graphics_draw_line(x, y, x, y + height - 1, color);                // Left
        graphics_draw_line(x + width - 1, y, x + width - 1, y + height - 1, color);  // Right
    }
}

/**
 * Draw a circle with the specified center, radius, and color
 * Using the midpoint circle algorithm
 * @param x X coordinate of the center
 * @param y Y coordinate of the center
 * @param radius Radius of the circle
 * @param color The color to draw
 * @param filled Whether to fill the circle
 */
void graphics_draw_circle(int x, int y, int radius, uint32_t color, int filled) {
    int f = 1 - radius;
    int ddF_x = 1;
    int ddF_y = -2 * radius;
    int px = 0;
    int py = radius;
    
    if (filled) {
        // Filled circle - draw horizontal lines across circle for each y
        for (int cy = -radius; cy <= radius; cy++) {
            int cx_bound = (int)sqrt(radius*radius - cy*cy);
            graphics_draw_line(x - cx_bound, y + cy, x + cx_bound, y + cy, color);
        }
    } else {
        // Outline only
        graphics_draw_pixel(x, y + radius, color);
        graphics_draw_pixel(x, y - radius, color);
        graphics_draw_pixel(x + radius, y, color);
        graphics_draw_pixel(x - radius, y, color);
    
        while (px < py) {
            if (f >= 0) {
                py--;
                ddF_y += 2;
                f += ddF_y;
            }
            px++;
            ddF_x += 2;
            f += ddF_x;
            
            graphics_draw_pixel(x + px, y + py, color);
            graphics_draw_pixel(x - px, y + py, color);
            graphics_draw_pixel(x + px, y - py, color);
            graphics_draw_pixel(x - px, y - py, color);
            graphics_draw_pixel(x + py, y + px, color);
            graphics_draw_pixel(x - py, y + px, color);
            graphics_draw_pixel(x + py, y - px, color);
            graphics_draw_pixel(x - py, y - px, color);
        }
    }
}

/**
 * Draw a character at (x,y) with the specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param c Character to draw
 * @param color The color to draw
 * @param scale The scaling factor (1 = normal size)
 */
void graphics_draw_char(int x, int y, char c, uint32_t color, int scale) {
    if (g_current_mode == GRAPHICS_MODE_TEXT) {
        // In text mode, use the VGA text mode function
        vga_put_char(c, color & 0xFF, (y / 16) * 80 + (x / 8));
        return;
    }
    
    // Get the bitmap for this character from the 8x8 font
    const uint8_t* glyph = font8x8_basic[(unsigned char)c];
    
    // Draw each pixel of the character
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 8; i++) {
            if (glyph[j] & (1 << i)) {
                // Scale the character if requested
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        graphics_draw_pixel(x + (i * scale) + sx, y + (j * scale) + sy, color);
                    }
                }
            }
        }
    }
}

/**
 * Draw a string at (x,y) with the specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param str String to draw
 * @param color The color to draw
 * @param scale The scaling factor (1 = normal size)
 */
void graphics_draw_string(int x, int y, const char* str, uint32_t color, int scale) {
    if (g_current_mode == GRAPHICS_MODE_TEXT) {
        // In text mode, use VGA text functions
        int row = y / 16;
        int col = x / 8;
        
        while (*str) {
            if (*str == '\n') {
                row++;
                col = x / 8;
            } else {
                vga_put_char(*str, color & 0xFF, row * 80 + col);
                col++;
            }
            str++;
        }
        return;
    }
    
    // In graphics mode, draw each character
    int start_x = x;
    
    while (*str) {
        if (*str == '\n') {
            y += 8 * scale;
            x = start_x;
        } else if (*str == '\r') {
            x = start_x;
        } else {
            graphics_draw_char(x, y, *str, color, scale);
            x += 8 * scale;
        }
        str++;
    }
}

/**
 * Get current framebuffer information
 * @return Pointer to the framebuffer info structure
 */
framebuffer_t* graphics_get_framebuffer() {
    return &g_framebuffer;
}

/**
 * Switch to text mode
 */
void graphics_switch_to_text_mode() {
    if (g_current_mode != GRAPHICS_MODE_TEXT) {
        graphics_init(GRAPHICS_MODE_TEXT);
    }
}

/**
 * Set the VGA to Mode 13h (320x200 with 256 colors)
 */
static void set_vga_mode13h() {
    // First, reset VGA state
    hal_outb(VGA_MISC_WRITE, 0x63);
    
    // Disable sequencer
    hal_outb(VGA_SEQ_INDEX, 0);
    hal_outb(VGA_SEQ_DATA, 0x01);

    // Write sequencer registers
    uint8_t seq_regs[] = { 0x03, 0x01, 0x0F, 0x00, 0x0E };
    for (int i = 1; i < 5; i++) {
        hal_outb(VGA_SEQ_INDEX, i);
        hal_outb(VGA_SEQ_DATA, seq_regs[i]);
    }
    
    // Clear protection bits in CRTC
    hal_outb(VGA_CRTC_INDEX, 0x03);
    hal_outb(VGA_CRTC_DATA, hal_inb(VGA_CRTC_DATA) & 0x7F);
    hal_outb(VGA_CRTC_INDEX, 0x11);
    hal_outb(VGA_CRTC_DATA, hal_inb(VGA_CRTC_DATA) & 0x7F);
    
    // CRTC registers for mode 13h
    uint8_t crtc_regs[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    };
    
    // Write CRTC registers
    for (int i = 0; i < 24; i++) {
        hal_outb(VGA_CRTC_INDEX, i);
        hal_outb(VGA_CRTC_DATA, crtc_regs[i]);
    }
    
    // Graphics Controller registers
    uint8_t gc_regs[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF };
    for (int i = 0; i < 9; i++) {
        hal_outb(VGA_GC_INDEX, i);
        hal_outb(VGA_GC_DATA, gc_regs[i]);
    }
    
    // Attribute controller registers
    uint8_t ac_regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    };
    
    // Reset attribute controller flip-flop to index state
    hal_inb(VGA_INSTAT_READ);
    
    // Write attribute controller registers
    for (int i = 0; i < 21; i++) {
        hal_outb(VGA_AC_INDEX, i);
        hal_outb(VGA_AC_WRITE, ac_regs[i]);
    }
    
    // Enable video output
    hal_inb(VGA_INSTAT_READ);
    hal_outb(VGA_AC_INDEX, 0x20);
    
    // Setup basic palette for mode 13h with VGA default colors
    for (int i = 0; i < 16; i++) {
        // Set the color index
        hal_outb(VGA_DAC_INDEX_WRITE, i);
        
        // VGA palette values (RGB, 6-bit per component)
        uint8_t r = (i & 4) ? ((i & 8) ? 0x3F : 0x2A) : 0;
        uint8_t g = (i & 2) ? ((i & 8) ? 0x3F : 0x2A) : 0;
        uint8_t b = (i & 1) ? ((i & 8) ? 0x3F : 0x2A) : 0;
        
        // Write RGB components
        hal_outb(VGA_DAC_DATA, r);
        hal_outb(VGA_DAC_DATA, g);
        hal_outb(VGA_DAC_DATA, b);
    }
}

/**
 * Set the active plane for VGA memory operations
 * @param p Plane number (0-3)
 */
static void set_plane(unsigned p) {
    uint8_t pmask = 1 << p;
    
    // Set read plane
    hal_outb(VGA_GC_INDEX, 4);
    hal_outb(VGA_GC_DATA, p);
    
    // Set write plane
    hal_outb(VGA_SEQ_INDEX, 2);
    hal_outb(VGA_SEQ_DATA, pmask);
}

/**
 * Write a set of VGA registers
 * @param registers Array of register values
 */
static void write_registers(uint8_t* registers) {
    // Not implemented in this basic version
    // Would contain code to program VGA registers for custom modes
}