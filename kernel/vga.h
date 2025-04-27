#ifndef VGA_H
#define VGA_H

#include <stdint.h>

// VGA hardware text mode constants
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

// VGA color constants
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

// VGA port and register constants
#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_CURSOR_HIGH 0x0E
#define VGA_CURSOR_LOW 0x0F

// Screen state
extern uint16_t* vga_buffer;
extern uint8_t vga_current_color;
extern uint16_t vga_cursor_x;
extern uint16_t vga_cursor_y;

// Color manipulation functions
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg);
uint16_t vga_entry(unsigned char c, uint8_t color);

// VGA driver initialization
void vga_init(void);

// Cursor functions
void vga_set_cursor(int x, int y);
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_disable_cursor(void);

// Display functions
void vga_clear_screen(void);
void vga_putchar(char c);
void vga_write(const char* data, size_t size);
void vga_write_string(const char* data);
void vga_write_string_at(const char* data, int x, int y);
void vga_set_color(uint8_t color);
void vga_set_fg_color(enum vga_color fg);
void vga_set_bg_color(enum vga_color bg);

// Graphics primitives (text based)
void vga_draw_box(int x1, int y1, int x2, int y2, enum vga_color color);
void vga_draw_horizontal_line(int x, int y, int length, enum vga_color color);
void vga_draw_vertical_line(int x, int y, int length, enum vga_color color);

// Window handling (simple text-based windows)
void vga_draw_window(int x1, int y1, int x2, int y2, const char* title, enum vga_color border_color, enum vga_color title_color);

#endif // VGA_H