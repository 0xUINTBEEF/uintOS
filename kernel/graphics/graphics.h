/**
 * Graphics subsystem for uintOS
 * Provides framebuffer-based graphics operations including primitive drawing and text rendering
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include "../vga.h"

// Define color constants
#define COLOR_BLACK         0x000000
#define COLOR_BLUE          0x0000AA
#define COLOR_GREEN         0x00AA00
#define COLOR_CYAN          0x00AAAA
#define COLOR_RED           0xAA0000
#define COLOR_MAGENTA       0xAA00AA
#define COLOR_BROWN         0xAA5500
#define COLOR_LIGHT_GRAY    0xAAAAAA
#define COLOR_DARK_GRAY     0x555555
#define COLOR_LIGHT_BLUE    0x5555FF
#define COLOR_LIGHT_GREEN   0x55FF55
#define COLOR_LIGHT_CYAN    0x55FFFF
#define COLOR_LIGHT_RED     0xFF5555
#define COLOR_LIGHT_MAGENTA 0xFF55FF
#define COLOR_YELLOW        0xFFFF55
#define COLOR_WHITE         0xFFFFFF

// Graphics modes
typedef enum {
    GRAPHICS_MODE_TEXT,       // Standard VGA text mode
    GRAPHICS_MODE_VGA_320_200, // 320x200 VGA mode
    GRAPHICS_MODE_VESA_640_480, // 640x480 VESA mode
    GRAPHICS_MODE_VESA_800_600, // 800x600 VESA mode
    GRAPHICS_MODE_VESA_1024_768 // 1024x768 VESA mode
} graphics_mode_t;

// Framebuffer info structure
typedef struct {
    uint8_t* buffer;         // Pointer to the framebuffer
    uint32_t width;          // Width of the screen in pixels
    uint32_t height;         // Height of the screen in pixels
    uint32_t pitch;          // Bytes per scanline
    uint8_t  bpp;            // Bits per pixel
    uint8_t  type;           // Framebuffer type
} framebuffer_t;

/**
 * Initialize the graphics subsystem
 * @param mode The graphics mode to initialize
 * @return 0 on success, non-zero on failure
 */
int graphics_init(graphics_mode_t mode);

/**
 * Clear the screen with a color
 * @param color The color to clear with
 */
void graphics_clear(uint32_t color);

/**
 * Draw a pixel at (x,y) with the specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param color The color to draw
 */
void graphics_draw_pixel(int x, int y, uint32_t color);

/**
 * Draw a line from (x1,y1) to (x2,y2) with the specified color
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color The color to draw
 */
void graphics_draw_line(int x1, int y1, int x2, int y2, uint32_t color);

/**
 * Draw a rectangle with the specified dimensions and color
 * @param x X coordinate of the top-left corner
 * @param y Y coordinate of the top-left corner
 * @param width Width of the rectangle
 * @param height Height of the rectangle
 * @param color The color to draw
 * @param filled Whether to fill the rectangle
 */
void graphics_draw_rect(int x, int y, int width, int height, uint32_t color, int filled);

/**
 * Draw a circle with the specified center, radius, and color
 * @param x X coordinate of the center
 * @param y Y coordinate of the center
 * @param radius Radius of the circle
 * @param color The color to draw
 * @param filled Whether to fill the circle
 */
void graphics_draw_circle(int x, int y, int radius, uint32_t color, int filled);

/**
 * Draw a character at (x,y) with the specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param c Character to draw
 * @param color The color to draw
 * @param scale The scaling factor (1 = normal size)
 */
void graphics_draw_char(int x, int y, char c, uint32_t color, int scale);

/**
 * Draw a string at (x,y) with the specified color
 * @param x X coordinate
 * @param y Y coordinate
 * @param str String to draw
 * @param color The color to draw
 * @param scale The scaling factor (1 = normal size)
 */
void graphics_draw_string(int x, int y, const char* str, uint32_t color, int scale);

/**
 * Get current framebuffer information
 * @return Pointer to the framebuffer info structure
 */
framebuffer_t* graphics_get_framebuffer();

/**
 * Switch to text mode
 */
void graphics_switch_to_text_mode();

#endif // GRAPHICS_H