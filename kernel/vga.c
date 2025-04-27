#include "vga.h"
#include "io.h"
#include <stddef.h>

// Global variables for VGA state
uint16_t* vga_buffer;
uint8_t vga_current_color;
uint16_t vga_cursor_x;
uint16_t vga_cursor_y;

// Box drawing characters in the extended ASCII set (code page 437)
#define BOX_HORIZONTAL 0xC4
#define BOX_VERTICAL 0xB3
#define BOX_TOP_LEFT 0xDA
#define BOX_TOP_RIGHT 0xBF
#define BOX_BOTTOM_LEFT 0xC0
#define BOX_BOTTOM_RIGHT 0xD9

// Combine foreground and background colors into a VGA color byte
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

// Create a VGA entry with a character and its attributes
uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

// Initialize the VGA hardware
void vga_init() {
    vga_buffer = (uint16_t*)VGA_MEMORY;
    vga_current_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_cursor_x = 0;
    vga_cursor_y = 0;
    
    // Clear the screen
    vga_clear_screen();
    
    // Enable the cursor
    vga_enable_cursor(14, 15);
}

// Set the hardware cursor position
void vga_set_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    
    // Update our tracking variables
    vga_cursor_x = x;
    vga_cursor_y = y;
    
    // Send position to VGA hardware
    outb(VGA_CTRL_REGISTER, VGA_CURSOR_HIGH);
    outb(VGA_DATA_REGISTER, (pos >> 8) & 0xFF);
    outb(VGA_CTRL_REGISTER, VGA_CURSOR_LOW);
    outb(VGA_DATA_REGISTER, pos & 0xFF);
}

// Enable the hardware cursor with specified size
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(VGA_CTRL_REGISTER, 0x0A);
    outb(VGA_DATA_REGISTER, (inb(VGA_DATA_REGISTER) & 0xC0) | cursor_start);
    outb(VGA_CTRL_REGISTER, 0x0B);
    outb(VGA_DATA_REGISTER, (inb(VGA_DATA_REGISTER) & 0xE0) | cursor_end);
}

// Disable the hardware cursor
void vga_disable_cursor() {
    outb(VGA_CTRL_REGISTER, 0x0A);
    outb(VGA_DATA_REGISTER, 0x20);
}

// Clear the entire screen
void vga_clear_screen() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_current_color);
        }
    }
    vga_set_cursor(0, 0);
}

// Handle scrolling when reaching the bottom of the screen
static void vga_scroll() {
    // Move every line up one
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            const size_t dest_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + 1) * VGA_WIDTH + x;
            vga_buffer[dest_index] = vga_buffer[src_index];
        }
    }
    
    // Clear the bottom line
    for (int x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(' ', vga_current_color);
    }
    
    // Move cursor to beginning of the bottom line
    vga_cursor_y = VGA_HEIGHT - 1;
    vga_cursor_x = 0;
}

// Put a character at the current cursor position and advance the cursor
void vga_putchar(char c) {
    // Handle special characters
    if (c == '\n') {
        // Newline
        vga_cursor_x = 0;
        vga_cursor_y++;
    } else if (c == '\r') {
        // Carriage return
        vga_cursor_x = 0;
    } else if (c == '\b') {
        // Backspace - move back one character and clear it
        if (vga_cursor_x > 0) {
            vga_cursor_x--;
            const size_t index = vga_cursor_y * VGA_WIDTH + vga_cursor_x;
            vga_buffer[index] = vga_entry(' ', vga_current_color);
        } else if (vga_cursor_y > 0) {
            // Go to end of previous line
            vga_cursor_y--;
            vga_cursor_x = VGA_WIDTH - 1;
            const size_t index = vga_cursor_y * VGA_WIDTH + vga_cursor_x;
            vga_buffer[index] = vga_entry(' ', vga_current_color);
        }
    } else if (c == '\t') {
        // Tab - move to next tab stop (every 8 spaces)
        vga_cursor_x = (vga_cursor_x + 8) & ~(8 - 1);
    } else {
        // Normal character
        const size_t index = vga_cursor_y * VGA_WIDTH + vga_cursor_x;
        vga_buffer[index] = vga_entry(c, vga_current_color);
        vga_cursor_x++;
    }
    
    // Handle line wrapping
    if (vga_cursor_x >= VGA_WIDTH) {
        vga_cursor_x = 0;
        vga_cursor_y++;
    }
    
    // Handle scrolling
    if (vga_cursor_y >= VGA_HEIGHT) {
        vga_scroll();
    }
    
    // Update the hardware cursor
    vga_set_cursor(vga_cursor_x, vga_cursor_y);
}

// Write a buffer of characters to the screen
void vga_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        vga_putchar(data[i]);
    }
}

// Write a null-terminated string to the screen
void vga_write_string(const char* data) {
    while (*data != '\0') {
        vga_putchar(*data);
        data++;
    }
}

// Write a string at a specific position
void vga_write_string_at(const char* data, int x, int y) {
    // Save current cursor position
    int old_x = vga_cursor_x;
    int old_y = vga_cursor_y;
    
    // Set cursor to specified position
    vga_set_cursor(x, y);
    
    // Write the string
    vga_write_string(data);
    
    // Restore cursor position
    vga_set_cursor(old_x, old_y);
}

// Set both foreground and background color
void vga_set_color(uint8_t color) {
    vga_current_color = color;
}

// Set only the foreground color, preserving the background
void vga_set_fg_color(enum vga_color fg) {
    vga_current_color = (vga_current_color & 0xF0) | fg;
}

// Set only the background color, preserving the foreground
void vga_set_bg_color(enum vga_color bg) {
    vga_current_color = (vga_current_color & 0x0F) | (bg << 4);
}

// Draw a horizontal line using box drawing characters
void vga_draw_horizontal_line(int x, int y, int length, enum vga_color color) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    for (int i = 0; i < length; i++) {
        const size_t index = y * VGA_WIDTH + (x + i);
        vga_buffer[index] = vga_entry(BOX_HORIZONTAL, vga_current_color);
    }
    
    vga_set_color(old_color);
}

// Draw a vertical line using box drawing characters
void vga_draw_vertical_line(int x, int y, int length, enum vga_color color) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    for (int i = 0; i < length; i++) {
        const size_t index = (y + i) * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(BOX_VERTICAL, vga_current_color);
    }
    
    vga_set_color(old_color);
}

// Draw a rectangular box
void vga_draw_box(int x1, int y1, int x2, int y2, enum vga_color color) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    // Draw the corners
    const size_t top_left = y1 * VGA_WIDTH + x1;
    const size_t top_right = y1 * VGA_WIDTH + x2;
    const size_t bottom_left = y2 * VGA_WIDTH + x1;
    const size_t bottom_right = y2 * VGA_WIDTH + x2;
    
    vga_buffer[top_left] = vga_entry(BOX_TOP_LEFT, vga_current_color);
    vga_buffer[top_right] = vga_entry(BOX_TOP_RIGHT, vga_current_color);
    vga_buffer[bottom_left] = vga_entry(BOX_BOTTOM_LEFT, vga_current_color);
    vga_buffer[bottom_right] = vga_entry(BOX_BOTTOM_RIGHT, vga_current_color);
    
    // Draw horizontal lines
    vga_draw_horizontal_line(x1 + 1, y1, x2 - x1 - 1, color);
    vga_draw_horizontal_line(x1 + 1, y2, x2 - x1 - 1, color);
    
    // Draw vertical lines
    vga_draw_vertical_line(x1, y1 + 1, y2 - y1 - 1, color);
    vga_draw_vertical_line(x2, y1 + 1, y2 - y1 - 1, color);
    
    vga_set_color(old_color);
}

// Draw a window with a title
void vga_draw_window(int x1, int y1, int x2, int y2, const char* title, enum vga_color border_color, enum vga_color title_color) {
    // Draw the basic box
    vga_draw_box(x1, y1, x2, y2, border_color);
    
    // Draw title if there is one
    if (title && *title) {
        int title_len = 0;
        const char* p = title;
        while (*p++) title_len++;
        
        // Center the title
        int title_x = x1 + ((x2 - x1 - title_len) / 2);
        if (title_x < x1 + 1) title_x = x1 + 1;
        
        // Save current color
        uint8_t old_color = vga_current_color;
        vga_set_color(title_color);
        
        // Write the title
        vga_write_string_at(title, title_x, y1);
        
        // Restore color
        vga_set_color(old_color);
    }
}