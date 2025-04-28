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

// Text attributes flags
#define VGA_ATTR_BLINK 0x80  // Bit 7 of attribute byte controls blinking

// Border styles for boxes and windows
enum vga_border_style {
    VGA_BORDER_SINGLE,      // Single-line border
    VGA_BORDER_DOUBLE,      // Double-line border
    VGA_BORDER_DASHED,      // Dashed border
    VGA_BORDER_DOTTED       // Dotted border
};

// Line styles
enum vga_line_style {
    VGA_LINE_SOLID,         // Solid line
    VGA_LINE_DASHED,        // Dashed line
    VGA_LINE_DOTTED         // Dotted line
};

// VGA port and register constants
#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_CURSOR_HIGH 0x0E
#define VGA_CURSOR_LOW 0x0F

// Triple buffer size for animation
#define VGA_BUFFER_SIZE (VGA_WIDTH * VGA_HEIGHT * 2)

// Screen state
extern uint16_t* vga_buffer;
extern uint16_t* vga_back_buffer;    // For triple buffering
extern uint16_t* vga_working_buffer; // For triple buffering
extern uint8_t vga_current_color;
extern uint16_t vga_cursor_x;
extern uint16_t vga_cursor_y;

// Color manipulation functions
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg);
uint16_t vga_entry(unsigned char c, uint8_t color);
uint8_t vga_entry_color_blink(enum vga_color fg, enum vga_color bg, uint8_t blink);

// VGA driver initialization
void vga_init(void);
void vga_init_triple_buffer(void);
void vga_swap_buffers(void);

// Cursor functions
void vga_set_cursor(int x, int y);
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_disable_cursor(void);
void vga_get_cursor_position(int* x, int* y);

// Display functions
void vga_clear_screen(void);
void vga_clear_region(int x1, int y1, int x2, int y2);
void vga_putchar(char c);
void vga_write(const char* data, size_t size);
void vga_write_string(const char* data);
void vga_write_string_at(const char* data, int x, int y);
void vga_write_char_animated(char c, int delay_ms);
void vga_write_string_animated(const char* data, int delay_ms);
void vga_set_color(uint8_t color);
void vga_set_fg_color(enum vga_color fg);
void vga_set_bg_color(enum vga_color bg);
void vga_enable_blinking(void);
void vga_disable_blinking(void);

// Advanced scrolling
void vga_scroll_region(int x1, int y1, int x2, int y2, int lines);
void vga_smooth_scroll(int lines, int delay_ms);

// Graphics primitives (text based)
void vga_draw_box(int x1, int y1, int x2, int y2, enum vga_color color);
void vga_draw_styled_box(int x1, int y1, int x2, int y2, enum vga_color color, enum vga_border_style style);
void vga_draw_horizontal_line(int x, int y, int length, enum vga_color color);
void vga_draw_vertical_line(int x, int y, int length, enum vga_color color);
void vga_draw_styled_horizontal_line(int x, int y, int length, enum vga_color color, enum vga_line_style style);
void vga_draw_styled_vertical_line(int x, int y, int length, enum vga_color color, enum vga_line_style style);
void vga_draw_rectangle(int x1, int y1, int x2, int y2, enum vga_color color, char fill_char);
void vga_draw_circle(int center_x, int center_y, int radius, enum vga_color color, char fill_char);

// Window handling (text-based windows)
void vga_draw_window(int x1, int y1, int x2, int y2, const char* title, enum vga_color border_color, enum vga_color title_color);
void vga_draw_styled_window(int x1, int y1, int x2, int y2, const char* title, 
                           enum vga_color border_color, enum vga_color title_color,
                           enum vga_border_style style, int draw_shadow);

// UI elements
void vga_draw_progress_bar(int x, int y, int width, int progress, int max_value, 
                          enum vga_color border_color, enum vga_color fill_color);
void vga_draw_menu(int x, int y, const char** items, int item_count, int selected_item,
                  enum vga_color normal_color, enum vga_color selected_color);
int vga_draw_dialog(int x, int y, int width, const char* title, const char* message,
                  const char** options, int option_count);
void vga_draw_status_bar(int y, const char* text, enum vga_color color);

// Screen buffer management
void vga_capture_screen(uint16_t* buffer);
void vga_restore_screen(const uint16_t* buffer);

// Virtual terminal support
#define VGA_MAX_VIRTUAL_TERMINALS 4
void vga_init_virtual_terminals(void);
void vga_switch_terminal(int terminal_id);
int vga_get_current_terminal(void);

// Custom character support
void vga_define_custom_char(uint8_t char_code, const uint8_t* bitmap);

// Color effects
void vga_fade_in(int delay_ms);
void vga_fade_out(int delay_ms);
void vga_color_transition(enum vga_color start_color, enum vga_color end_color, int delay_ms);

#endif // VGA_H