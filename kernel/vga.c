#include "vga.h"
#include "io.h"
#include <stddef.h>
#include <string.h> // For memcpy

// Global variables for VGA state
uint16_t* vga_buffer;
uint16_t* vga_back_buffer = NULL;     // For triple buffering
uint16_t* vga_working_buffer = NULL;  // For triple buffering
uint8_t vga_current_color;
uint16_t vga_cursor_x;
uint16_t vga_cursor_y;

// Virtual terminal support
static uint16_t* vga_terminal_buffers[VGA_MAX_VIRTUAL_TERMINALS] = {NULL};
static int vga_current_terminal = 0;

// Box drawing characters in the extended ASCII set (code page 437)
// Single line box drawing
#define BOX_HORIZONTAL 0xC4
#define BOX_VERTICAL 0xB3
#define BOX_TOP_LEFT 0xDA
#define BOX_TOP_RIGHT 0xBF
#define BOX_BOTTOM_LEFT 0xC0
#define BOX_BOTTOM_RIGHT 0xD9

// Double line box drawing
#define BOX_DOUBLE_HORIZONTAL 0xCD
#define BOX_DOUBLE_VERTICAL 0xBA
#define BOX_DOUBLE_TOP_LEFT 0xC9
#define BOX_DOUBLE_TOP_RIGHT 0xBB
#define BOX_DOUBLE_BOTTOM_LEFT 0xC8
#define BOX_DOUBLE_BOTTOM_RIGHT 0xBC

// Dashed and dotted line characters
#define BOX_DASHED_HORIZONTAL 0xC4 // Using the same as single line for now
#define BOX_DASHED_VERTICAL 0xB3   // Using the same as single line for now
#define BOX_DOTTED_HORIZONTAL 0xA1
#define BOX_DOTTED_VERTICAL 0xB3   // Using the same as single line for now

// Block characters for UI elements
#define BLOCK_FULL 0xDB
#define BLOCK_LIGHT 0xB0
#define BLOCK_MEDIUM 0xB1
#define BLOCK_DARK 0xB2

// Utility function for delay using the PIT
static void delay(int ms) {
    // Use PIT (Programmable Interval Timer) for more accurate timing
    // This is a proper implementation using a sleep function
    uint32_t start_tick = log_timestamp;
    uint32_t target_ticks = ms / 10; // Assuming 100Hz timer (10ms per tick)
    
    if (target_ticks == 0) target_ticks = 1; // At least one tick
    
    while ((log_timestamp - start_tick) < target_ticks) {
        // Use proper sleep function instead of busy waiting
        if (thread_current() != NULL) {
            // If we're in a threaded context, yield to other threads
            thread_sleep(ms);
        } else {
            // Otherwise, halt the CPU until the next interrupt
            asm volatile("sti; hlt");
        }
    }
}

// Combine foreground and background colors into a VGA color byte
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

// Create a VGA color byte with blinking text option
uint8_t vga_entry_color_blink(enum vga_color fg, enum vga_color bg, uint8_t blink) {
    return fg | bg << 4 | (blink ? VGA_ATTR_BLINK : 0);
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

// Initialize triple buffering support
void vga_init_triple_buffer() {
    // Allocate memory for the back and working buffers
    // Use proper memory allocation from the kernel heap
    vga_back_buffer = (uint16_t*)kmalloc(VGA_BUFFER_SIZE);
    vga_working_buffer = (uint16_t*)kmalloc(VGA_BUFFER_SIZE);
    
    if (!vga_back_buffer || !vga_working_buffer) {
        if (vga_back_buffer) kfree(vga_back_buffer);
        if (vga_working_buffer) kfree(vga_working_buffer);
        
        // Fallback to static allocation if dynamic allocation fails
        static uint16_t back_buffer_memory[VGA_BUFFER_SIZE];
        static uint16_t working_buffer_memory[VGA_BUFFER_SIZE];
        
        vga_back_buffer = back_buffer_memory;
        vga_working_buffer = working_buffer_memory;
        
        log_warning("VGA", "Failed to allocate triple buffer memory, using static fallback");
    }
    
    // Copy the current screen to the back buffer
    vga_capture_screen(vga_back_buffer);
    vga_capture_screen(vga_working_buffer);
}

// Swap the buffers for flicker-free animation
void vga_swap_buffers() {
    if (!vga_back_buffer || !vga_working_buffer) {
        return;
    }
    
    // Copy working buffer to back buffer
    memcpy(vga_back_buffer, vga_working_buffer, VGA_BUFFER_SIZE);
    
    // Copy back buffer to screen
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_back_buffer[i];
    }
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

// Get the current cursor position
void vga_get_cursor_position(int* x, int* y) {
    *x = vga_cursor_x;
    *y = vga_cursor_y;
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

// Enable text blinking
void vga_enable_blinking() {
    // The blinking attribute is controlled by the VGA's attribute controller
    // We need to toggle bit 3 of the mode control register
    
    // First, read the current value
    outb(0x3C0, 0x10); // Select mode control register
    uint8_t mode = inb(0x3C1);
    
    // Set bit 3 to enable blinking
    mode |= 0x08;
    
    // Write the new value back
    outb(0x3C0, 0x10);
    outb(0x3C0, mode);
}

// Disable text blinking
void vga_disable_blinking() {
    // Read current value of mode control register
    outb(0x3C0, 0x10);
    uint8_t mode = inb(0x3C1);
    
    // Clear bit 3 to disable blinking
    mode &= ~0x08;
    
    // Write the new value back
    outb(0x3C0, 0x10);
    outb(0x3C0, mode);
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

// Clear a specific region of the screen
void vga_clear_region(int x1, int y1, int x2, int y2) {
    // Ensure coordinates are within bounds
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= VGA_WIDTH) x2 = VGA_WIDTH - 1;
    if (y2 >= VGA_HEIGHT) y2 = VGA_HEIGHT - 1;
    
    // Check if coordinates are valid
    if (x1 > x2 || y1 > y2) {
        return;
    }
    
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_current_color);
        }
    }
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

// Scroll a specific region of the screen
void vga_scroll_region(int x1, int y1, int x2, int y2, int lines) {
    // Ensure coordinates are within bounds
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= VGA_WIDTH) x2 = VGA_WIDTH - 1;
    if (y2 >= VGA_HEIGHT) y2 = VGA_HEIGHT - 1;
    
    // Check if coordinates are valid
    if (x1 > x2 || y1 > y2 || lines <= 0 || lines > (y2 - y1 + 1)) {
        return;
    }
    
    // Scroll the region up by 'lines' lines
    for (int y = y1; y <= y2 - lines; y++) {
        for (int x = x1; x <= x2; x++) {
            const size_t dest_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + lines) * VGA_WIDTH + x;
            vga_buffer[dest_index] = vga_buffer[src_index];
        }
    }
    
    // Clear the newly exposed lines at the bottom of the region
    for (int y = y2 - lines + 1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_current_color);
        }
    }
}

// Smooth scrolling with animation effect
void vga_smooth_scroll(int lines, int delay_ms) {
    // We need triple buffering for this
    if (!vga_back_buffer || !vga_working_buffer) {
        vga_init_triple_buffer();
    }
    
    // Save current screen to working buffer
    vga_capture_screen(vga_working_buffer);
    
    for (int step = 0; step < lines; step++) {
        // Scroll one line
        vga_scroll();
        
        // Copy current screen to back buffer
        vga_capture_screen(vga_back_buffer);
        
        // Swap buffers to display
        vga_swap_buffers();
        
        // Delay for smooth animation
        delay(delay_ms);
    }
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

// Write a character with animation delay
void vga_write_char_animated(char c, int delay_ms) {
    vga_putchar(c);
    delay(delay_ms);
}

// Write a string with animation, character by character
void vga_write_string_animated(const char* data, int delay_ms) {
    while (*data != '\0') {
        vga_putchar(*data);
        data++;
        delay(delay_ms);
    }
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

// Draw a horizontal line using box drawing characters with specified style
void vga_draw_styled_horizontal_line(int x, int y, int length, enum vga_color color, enum vga_line_style style) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    char line_char;
    switch (style) {
        case VGA_LINE_SOLID:
            line_char = BOX_HORIZONTAL;
            break;
        case VGA_LINE_DASHED:
            line_char = BOX_DASHED_HORIZONTAL;
            break;
        case VGA_LINE_DOTTED:
            line_char = BOX_DOTTED_HORIZONTAL;
            break;
        default:
            line_char = BOX_HORIZONTAL;
    }
    
    for (int i = 0; i < length; i++) {
        const size_t index = y * VGA_WIDTH + (x + i);
        vga_buffer[index] = vga_entry(line_char, vga_current_color);
    }
    
    vga_set_color(old_color);
}

// Draw a horizontal line using box drawing characters (default solid style)
void vga_draw_horizontal_line(int x, int y, int length, enum vga_color color) {
    vga_draw_styled_horizontal_line(x, y, length, color, VGA_LINE_SOLID);
}

// Draw a vertical line using box drawing characters with specified style
void vga_draw_styled_vertical_line(int x, int y, int length, enum vga_color color, enum vga_line_style style) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    char line_char;
    switch (style) {
        case VGA_LINE_SOLID:
            line_char = BOX_VERTICAL;
            break;
        case VGA_LINE_DASHED:
            line_char = BOX_DASHED_VERTICAL;
            break;
        case VGA_LINE_DOTTED:
            line_char = BOX_DOTTED_VERTICAL;
            break;
        default:
            line_char = BOX_VERTICAL;
    }
    
    for (int i = 0; i < length; i++) {
        const size_t index = (y + i) * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(line_char, vga_current_color);
    }
    
    vga_set_color(old_color);
}

// Draw a vertical line using box drawing characters (default solid style)
void vga_draw_vertical_line(int x, int y, int length, enum vga_color color) {
    vga_draw_styled_vertical_line(x, y, length, color, VGA_LINE_SOLID);
}

// Draw a rectangular box with a specific border style
void vga_draw_styled_box(int x1, int y1, int x2, int y2, enum vga_color color, enum vga_border_style style) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    // Select characters based on border style
    char h_char, v_char, tl_char, tr_char, bl_char, br_char;
    
    switch (style) {
        case VGA_BORDER_DOUBLE:
            h_char = BOX_DOUBLE_HORIZONTAL;
            v_char = BOX_DOUBLE_VERTICAL;
            tl_char = BOX_DOUBLE_TOP_LEFT;
            tr_char = BOX_DOUBLE_TOP_RIGHT;
            bl_char = BOX_DOUBLE_BOTTOM_LEFT;
            br_char = BOX_DOUBLE_BOTTOM_RIGHT;
            break;
        case VGA_BORDER_DASHED:
            h_char = BOX_DASHED_HORIZONTAL;
            v_char = BOX_DASHED_VERTICAL;
            tl_char = BOX_TOP_LEFT; // Using single line corners
            tr_char = BOX_TOP_RIGHT;
            bl_char = BOX_BOTTOM_LEFT;
            br_char = BOX_BOTTOM_RIGHT;
            break;
        case VGA_BORDER_DOTTED:
            h_char = BOX_DOTTED_HORIZONTAL;
            v_char = BOX_DOTTED_VERTICAL;
            tl_char = BOX_TOP_LEFT; // Using single line corners
            tr_char = BOX_TOP_RIGHT;
            bl_char = BOX_BOTTOM_LEFT;
            br_char = BOX_BOTTOM_RIGHT;
            break;
        case VGA_BORDER_SINGLE:
        default:
            h_char = BOX_HORIZONTAL;
            v_char = BOX_VERTICAL;
            tl_char = BOX_TOP_LEFT;
            tr_char = BOX_TOP_RIGHT;
            bl_char = BOX_BOTTOM_LEFT;
            br_char = BOX_BOTTOM_RIGHT;
    }
    
    // Draw the corners
    const size_t top_left = y1 * VGA_WIDTH + x1;
    const size_t top_right = y1 * VGA_WIDTH + x2;
    const size_t bottom_left = y2 * VGA_WIDTH + x1;
    const size_t bottom_right = y2 * VGA_WIDTH + x2;
    
    vga_buffer[top_left] = vga_entry(tl_char, vga_current_color);
    vga_buffer[top_right] = vga_entry(tr_char, vga_current_color);
    vga_buffer[bottom_left] = vga_entry(bl_char, vga_current_color);
    vga_buffer[bottom_right] = vga_entry(br_char, vga_current_color);
    
    // Draw horizontal lines
    for (int x = x1 + 1; x < x2; x++) {
        vga_buffer[y1 * VGA_WIDTH + x] = vga_entry(h_char, vga_current_color);
        vga_buffer[y2 * VGA_WIDTH + x] = vga_entry(h_char, vga_current_color);
    }
    
    // Draw vertical lines
    for (int y = y1 + 1; y < y2; y++) {
        vga_buffer[y * VGA_WIDTH + x1] = vga_entry(v_char, vga_current_color);
        vga_buffer[y * VGA_WIDTH + x2] = vga_entry(v_char, vga_current_color);
    }
    
    vga_set_color(old_color);
}

// Draw a regular box with single line borders
void vga_draw_box(int x1, int y1, int x2, int y2, enum vga_color color) {
    vga_draw_styled_box(x1, y1, x2, y2, color, VGA_BORDER_SINGLE);
}

// Draw a filled rectangle
void vga_draw_rectangle(int x1, int y1, int x2, int y2, enum vga_color color, char fill_char) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(fill_char, vga_current_color);
        }
    }
    
    vga_set_color(old_color);
}

// Draw a circle using text characters (approximation)
void vga_draw_circle(int center_x, int center_y, int radius, enum vga_color color, char fill_char) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            // Use the equation of a circle: x² + y² <= r²
            if (x*x + y*y <= radius*radius) {
                int draw_x = center_x + x;
                int draw_y = center_y + y;
                
                // Check if point is within screen bounds
                if (draw_x >= 0 && draw_x < VGA_WIDTH && draw_y >= 0 && draw_y < VGA_HEIGHT) {
                    const size_t index = draw_y * VGA_WIDTH + draw_x;
                    vga_buffer[index] = vga_entry(fill_char, vga_current_color);
                }
            }
        }
    }
    
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

// Draw a styled window with optional shadow
void vga_draw_styled_window(int x1, int y1, int x2, int y2, const char* title, 
                           enum vga_color border_color, enum vga_color title_color,
                           enum vga_border_style style, int draw_shadow) {
    
    // Draw shadow first (if requested)
    if (draw_shadow) {
        uint8_t old_color = vga_current_color;
        vga_set_color(vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_BLACK));
        
        // Draw right shadow
        for (int y = y1 + 1; y <= y2 + 1; y++) {
            if (x2 + 1 < VGA_WIDTH && y < VGA_HEIGHT) {
                const size_t index = y * VGA_WIDTH + (x2 + 1);
                vga_buffer[index] = vga_entry(' ', vga_current_color);
            }
        }
        
        // Draw bottom shadow
        for (int x = x1 + 1; x <= x2 + 1; x++) {
            if (x < VGA_WIDTH && y2 + 1 < VGA_HEIGHT) {
                const size_t index = (y2 + 1) * VGA_WIDTH + x;
                vga_buffer[index] = vga_entry(' ', vga_current_color);
            }
        }
        
        vga_set_color(old_color);
    }
    
    // Draw the styled box
    vga_draw_styled_box(x1, y1, x2, y2, border_color, style);
    
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

// Draw a progress bar
void vga_draw_progress_bar(int x, int y, int width, int progress, int max_value, 
                          enum vga_color border_color, enum vga_color fill_color) {
    
    // Calculate the number of filled blocks
    int filled = (width - 2) * progress / max_value;
    if (filled > width - 2) {
        filled = width - 2;
    }
    
    // Draw the border
    uint8_t old_color = vga_current_color;
    vga_set_color(border_color);
    
    // Draw left and right borders
    vga_buffer[y * VGA_WIDTH + x] = vga_entry('[', vga_current_color);
    vga_buffer[y * VGA_WIDTH + (x + width - 1)] = vga_entry(']', vga_current_color);
    
    // Draw the filled portion
    vga_set_color(fill_color);
    for (int i = 0; i < filled; i++) {
        vga_buffer[y * VGA_WIDTH + (x + 1 + i)] = vga_entry(BLOCK_FULL, vga_current_color);
    }
    
    // Draw the empty portion
    for (int i = filled; i < width - 2; i++) {
        vga_buffer[y * VGA_WIDTH + (x + 1 + i)] = vga_entry(' ', vga_current_color);
    }
    
    vga_set_color(old_color);
}

// Draw a simple menu with selectable items
void vga_draw_menu(int x, int y, const char** items, int item_count, int selected_item,
                  enum vga_color normal_color, enum vga_color selected_color) {
    
    uint8_t old_color = vga_current_color;
    
    for (int i = 0; i < item_count; i++) {
        // Set appropriate color based on selection
        if (i == selected_item) {
            vga_set_color(selected_color);
        } else {
            vga_set_color(normal_color);
        }
        
        // Draw the menu item
        vga_write_string_at(items[i], x, y + i);
    }
    
    vga_set_color(old_color);
}

// Draw a dialog box with a message and options
int vga_draw_dialog(int x, int y, int width, const char* title, const char* message,
                  const char** options, int option_count) {
    
    // Calculate dialog dimensions
    int message_len = 0;
    const char* p = message;
    while (*p++) message_len++;
    
    int height = 6 + (message_len / (width - 4)) + option_count;
    
    // Draw the dialog box
    vga_draw_styled_window(x, y, x + width, y + height, title, 
                          vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE),
                          vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED),
                          VGA_BORDER_DOUBLE, 1);
    
    // Draw the message
    uint8_t old_color = vga_current_color;
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    vga_write_string_at(message, x + 2, y + 2);
    
    // Draw options
    for (int i = 0; i < option_count; i++) {
        vga_write_string_at(options[i], x + 2, y + 4 + i);
    }
    
    vga_set_color(old_color);
    
    return 0; // In a real implementation, would wait for user input and return selection
}

// Draw a status bar at the bottom of the screen
void vga_draw_status_bar(int y, const char* text, enum vga_color color) {
    uint8_t old_color = vga_current_color;
    vga_set_color(color);
    
    // Clear the status bar line
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', vga_current_color);
    }
    
    // Write the status text
    vga_write_string_at(text, 1, y);
    
    vga_set_color(old_color);
}

// Capture current screen to a buffer
void vga_capture_screen(uint16_t* buffer) {
    if (!buffer) return;
    
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            buffer[index] = vga_buffer[index];
        }
    }
}

// Restore screen from a buffer
void vga_restore_screen(const uint16_t* buffer) {
    if (!buffer) return;
    
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = buffer[index];
        }
    }
}

// Initialize virtual terminals
void vga_init_virtual_terminals() {
    // Allocate memory for each terminal buffer
    for (int i = 0; i < VGA_MAX_VIRTUAL_TERMINALS; i++) {
        if (!vga_terminal_buffers[i]) {
            // Allocate memory from the kernel heap
            vga_terminal_buffers[i] = (uint16_t*)kmalloc(VGA_WIDTH * VGA_HEIGHT * sizeof(uint16_t));
            
            // If allocation fails, use static fallback
            if (!vga_terminal_buffers[i]) {
                static uint16_t static_terminal_buffers[VGA_MAX_VIRTUAL_TERMINALS][VGA_WIDTH * VGA_HEIGHT];
                vga_terminal_buffers[i] = static_terminal_buffers[i];
                log_warning("VGA", "Failed to allocate memory for terminal %d, using static fallback", i);
            }
            
            // Initialize each terminal with clear screen
            for (int j = 0; j < VGA_WIDTH * VGA_HEIGHT; j++) {
                vga_terminal_buffers[i][j] = vga_entry(' ', vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
        }
    }
    
    // Start with terminal 0
    vga_current_terminal = 0;
}

// Switch to a different virtual terminal
void vga_switch_terminal(int terminal_id) {
    if (terminal_id < 0 || terminal_id >= VGA_MAX_VIRTUAL_TERMINALS) {
        return;
    }
    
    if (!vga_terminal_buffers[0]) {
        // Initialize terminals if not already done
        vga_init_virtual_terminals();
    }
    
    // Save current screen to current terminal's buffer
    vga_capture_screen(vga_terminal_buffers[vga_current_terminal]);
    
    // Set the new current terminal
    vga_current_terminal = terminal_id;
    
    // Restore screen from the new terminal's buffer
    vga_restore_screen(vga_terminal_buffers[vga_current_terminal]);
}

// Get current terminal ID
int vga_get_current_terminal() {
    return vga_current_terminal;
}

// Define a custom character (for fonts)
void vga_define_custom_char(uint8_t char_code, const uint8_t* bitmap) {
    // VGA text mode doesn't allow for custom characters directly
    // This would require implementation using a CGA/EGA character generator
    // This is a placeholder - in a real implementation, you'd need to use port I/O
    // to program the character generator
}

// Fade the screen in
void vga_fade_in(int delay_ms) {
    // Save original screen
    static uint16_t original_screen[VGA_WIDTH * VGA_HEIGHT];
    vga_capture_screen(original_screen);
    
    // Start with black
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            char character = original_screen[index] & 0xFF;
            vga_buffer[index] = vga_entry(character, vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_BLACK));
        }
    }
    
    // Fade through gray tones to original colors
    for (int step = 0; step < 8; step++) {
        delay(delay_ms);
        
        for (int y = 0; y < VGA_HEIGHT; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                const size_t index = y * VGA_WIDTH + x;
                char character = original_screen[index] & 0xFF;
                uint8_t attr = original_screen[index] >> 8;
                uint8_t fg = attr & 0xF;
                uint8_t bg = (attr >> 4) & 0xF;
                
                // Gradually increase color intensity
                uint8_t new_fg = fg * step / 7;
                if (new_fg > 15) new_fg = 15;
                uint8_t new_bg = bg * step / 7;
                if (new_bg > 15) new_bg = 15;
                
                vga_buffer[index] = vga_entry(character, vga_entry_color(new_fg, new_bg));
            }
        }
    }
    
    // Final step - restore original screen
    vga_restore_screen(original_screen);
}

// Fade the screen out
void vga_fade_out(int delay_ms) {
    // Save original screen
    static uint16_t original_screen[VGA_WIDTH * VGA_HEIGHT];
    vga_capture_screen(original_screen);
    
    // Fade through gray tones to black
    for (int step = 7; step >= 0; step--) {
        for (int y = 0; y < VGA_HEIGHT; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                const size_t index = y * VGA_WIDTH + x;
                char character = original_screen[index] & 0xFF;
                uint8_t attr = original_screen[index] >> 8;
                uint8_t fg = attr & 0xF;
                uint8_t bg = (attr >> 4) & 0xF;
                
                // Gradually decrease color intensity
                uint8_t new_fg = fg * step / 7;
                uint8_t new_bg = bg * step / 7;
                
                vga_buffer[index] = vga_entry(character, vga_entry_color(new_fg, new_bg));
            }
        }
        
        delay(delay_ms);
    }
}

// Transition from one color to another
void vga_color_transition(enum vga_color start_color, enum vga_color end_color, int delay_ms) {
    uint8_t old_color = vga_current_color;
    
    // Get current screen content
    static uint16_t screen_content[VGA_WIDTH * VGA_HEIGHT];
    vga_capture_screen(screen_content);
    
    // Transition through intermediate colors
    for (int step = 0; step <= 8; step++) {
        for (int y = 0; y < VGA_HEIGHT; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                const size_t index = y * VGA_WIDTH + x;
                char character = screen_content[index] & 0xFF;
                uint8_t attr = screen_content[index] >> 8;
                
                // If the foreground is start_color, transition it
                if ((attr & 0xF) == start_color) {
                    uint8_t transition_color;
                    if (step == 8) {
                        transition_color = end_color;
                    } else if (step < 4) {
                        transition_color = start_color;
                    } else {
                        transition_color = end_color;
                    }
                    
                    attr = (attr & 0xF0) | transition_color;
                }
                
                // If the background is start_color, transition it
                if (((attr >> 4) & 0xF) == start_color) {
                    uint8_t transition_color;
                    if (step == 8) {
                        transition_color = end_color;
                    } else if (step < 4) {
                        transition_color = start_color;
                    } else {
                        transition_color = end_color;
                    }
                    
                    attr = (attr & 0x0F) | (transition_color << 4);
                }
                
                vga_buffer[index] = vga_entry(character, attr);
            }
        }
        
        delay(delay_ms);
    }
    
    vga_set_color(old_color);
}