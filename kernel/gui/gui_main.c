/**
 * @file gui_main.c
 * @brief Main implementation of GUI subsystem for uintOS
 */
#include <stdint.h>
#include "window.h"
#include "controls.h"
#include "layout.h"
#include "../keyboard.h"
#include "../logging/log.h"
#include "../graphics/graphics.h"
#include "../graphics/font8x8.h"
#include "../io.h"

// Global variables for GUI subsystem
static int gui_initialized = 0;
static int gui_running = 0;
static int current_theme = 0; // 0=Classic, 1=Modern, 2=Dark, 3=Light

// Theme color schemes
typedef struct {
    uint32_t window_bg;
    uint32_t window_border;
    uint32_t titlebar_bg;
    uint32_t titlebar_text;
    uint32_t control_bg;
    uint32_t control_text;
    uint32_t control_border;
    uint32_t desktop_bg;
    uint32_t highlight;
    uint32_t shadow;
    uint32_t button_highlight;
    uint32_t button_shadow;
} theme_colors_t;

// Theme definitions
static theme_colors_t themes[] = {
    // Classic theme
    {
        .window_bg = 0xF0F0F0,
        .window_border = 0x000080,
        .titlebar_bg = 0x000080,
        .titlebar_text = 0xFFFFFF,
        .control_bg = 0xE0E0E0,
        .control_text = 0x000000,
        .control_border = 0x808080,
        .desktop_bg = 0x008080,
        .highlight = 0xFFFFFF,
        .shadow = 0x404040,
        .button_highlight = 0xFFFFFF,
        .button_shadow = 0x808080
    },
    // Modern theme
    {
        .window_bg = 0xF8F8F8,
        .window_border = 0xC0C0C0,
        .titlebar_bg = 0x0078D7,
        .titlebar_text = 0xFFFFFF,
        .control_bg = 0xF0F0F0,
        .control_text = 0x202020,
        .control_border = 0xC0C0C0,
        .desktop_bg = 0x0078D7,
        .highlight = 0xE5F1FB,
        .shadow = 0xA0A0A0,
        .button_highlight = 0xE5F1FB,
        .button_shadow = 0xA0A0A0
    },
    // Dark theme
    {
        .window_bg = 0x202020,
        .window_border = 0x404040,
        .titlebar_bg = 0x303030,
        .titlebar_text = 0xE0E0E0,
        .control_bg = 0x303030,
        .control_text = 0xE0E0E0,
        .control_border = 0x505050,
        .desktop_bg = 0x101010,
        .highlight = 0x505050,
        .shadow = 0x000000,
        .button_highlight = 0x505050,
        .button_shadow = 0x202020
    },
    // Light theme
    {
        .window_bg = 0xFFFFFF,
        .window_border = 0xE0E0E0,
        .titlebar_bg = 0xF0F0F0,
        .titlebar_text = 0x303030,
        .control_bg = 0xFAFAFA,
        .control_text = 0x303030,
        .control_border = 0xE0E0E0,
        .desktop_bg = 0xF0F0F0,
        .highlight = 0xFFFFFF,
        .shadow = 0xD0D0D0,
        .button_highlight = 0xFFFFFF,
        .button_shadow = 0xD0D0D0
    }
};

// Function prototypes
void gui_draw_desktop();
int gui_process_keyboard_input();
int gui_process_mouse_input();
void gui_update_windows();
void gui_init_demo_windows();

/**
 * Get current theme index
 */
int gui_get_current_theme(void) {
    return current_theme;
}

/**
 * Set the GUI theme
 * @param theme Theme index (0-3)
 */
void gui_set_theme(int theme) {
    if (theme >= 0 && theme < 4) {
        current_theme = theme;
        
        // Update window manager theme colors
        window_set_theme_colors(
            themes[current_theme].window_bg,
            themes[current_theme].window_border,
            themes[current_theme].titlebar_bg,
            themes[current_theme].titlebar_text,
            themes[current_theme].control_bg,
            themes[current_theme].control_text,
            themes[current_theme].control_border
        );
        
        // Redraw all windows
        window_render_all();
    }
}

/**
 * Main GUI loop
 */
void gui_main_loop(void) {
    log_info("GUI", "Starting GUI main loop");
    
    // Set the running flag
    gui_running = 1;
    
    // Set theme colors
    gui_set_theme(current_theme);
    
    // Draw initial desktop background
    gui_draw_desktop();
    
    // Main event loop
    while (gui_running) {
        // Process keyboard input
        if (is_key_available()) {
            if (gui_process_keyboard_input()) {
                break;  // Exit loop if ESC is pressed
            }
        }
        
        // Process mouse input if we have a mouse
        gui_process_mouse_input();
        
        // Update and render windows
        gui_update_windows();
        
        // Very short delay to prevent CPU hogging
        for (volatile int i = 0; i < 1000; i++) { /* delay */ }
    }
    
    // GUI is shutting down
    log_info("GUI", "GUI main loop exited");
    gui_running = 0;
    
    // Switch back to text mode
    graphics_switch_to_text_mode();
}

/**
 * Draw the desktop background
 */
void gui_draw_desktop() {
    // Get current frame buffer
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) return;
    
    // Fill the screen with the desktop color
    graphics_clear(themes[current_theme].desktop_bg);
    
    // Draw a simple desktop pattern (grid)
    uint32_t grid_color = themes[current_theme].desktop_bg;
    
    // Adjust grid color to be slightly lighter/darker than background
    if (current_theme == 2) { // Dark theme
        grid_color += 0x101010; // Lighter
    } else {
        grid_color -= 0x101010; // Darker
    }
    
    // Draw grid lines
    for (int y = 0; y < fb->height; y += 40) {
        graphics_draw_line(0, y, fb->width, y, grid_color);
    }
    
    for (int x = 0; x < fb->width; x += 40) {
        graphics_draw_line(x, 0, x, fb->height, grid_color);
    }
    
    // Draw desktop elements
    
    // System info in bottom right corner
    char info_text[64];
    strcpy(info_text, "uintOS GUI v1.0");
    int text_len = strlen(info_text);
    int text_x = fb->width - (text_len * 8) - 10;
    int text_y = fb->height - 20;
    
    // Draw with slight shadow effect for visibility
    graphics_draw_string(text_x + 1, text_y + 1, info_text, 0x000000, 1);
    graphics_draw_string(text_x, text_y, info_text, 0xFFFFFF, 1);
}

/**
 * Process keyboard input for the GUI
 * @return 1 if the GUI should exit, 0 otherwise
 */
int gui_process_keyboard_input() {
    char key = keyboard_read_key();
    
    // Check for exit key (ESC)
    if (key == 0x1B) {
        log_info("GUI", "ESC key pressed, exiting GUI");
        return 1;
    }
    
    // Pass the key to the window manager
    window_process_key(key, key, 1); // Press
    window_process_key(key, key, 0); // Release
    
    return 0;
}

/**
 * Process mouse input for the GUI
 * Note: This is a stub implementation until we have actual mouse input
 */
int gui_process_mouse_input() {
    // For now, we simulate mouse movement with keyboard arrow keys
    // This would be replaced with actual mouse handling
    return 0;
}

/**
 * Update and render windows
 */
void gui_update_windows() {
    // Render all windows using the window manager
    window_render_all();
}

/**
 * Check if graphics subsystem is initialized
 */
int graphics_is_initialized(void) {
    // Check if we have a valid framebuffer
    return graphics_get_framebuffer() != NULL;
}

/**
 * Check if window manager is initialized
 */
int window_manager_is_initialized(void) {
    extern int window_manager_ready;
    return window_manager_ready;
}

/**
 * Get window count
 */
int window_get_count(void) {
    extern int window_count;
    return window_count;
}

/**
 * Get video memory size
 */
uint32_t graphics_get_video_memory(void) {
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) return 0;
    
    // Calculate video memory size in bytes
    return fb->width * fb->height * (fb->bpp / 8);
}

/**
 * Set graphics resolution
 */
int graphics_set_resolution(int mode) {
    // Initialize graphics with the specified mode
    return graphics_init(mode);
}

/**
 * Get current graphics resolution
 */
void graphics_get_resolution(int* width, int* height, int* bpp) {
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        *width = 0;
        *height = 0;
        *bpp = 0;
        return;
    }
    
    *width = fb->width;
    *height = fb->height;
    *bpp = fb->bpp;
}

/**
 * Initialize the window manager
 */
int window_manager_init(void) {
    // Initialize window manager (this function should be defined elsewhere)
    return 0;
}

/**
 * Shut down the window manager
 */
void window_manager_shutdown(void) {
    // Shut down window manager (this function should be defined elsewhere)
}

/**
 * Run GUI demo
 * Creates a set of windows demonstrating the GUI capabilities
 */
void gui_demo() {
    log_info("GUI", "Running GUI demonstration");
    
    // Set the theme colors
    gui_set_theme(current_theme);
    
    // Draw the desktop
    gui_draw_desktop();
    
    // Create demo windows
    gui_init_demo_windows();
    
    // Run the GUI loop until a key is pressed
    log_info("GUI", "GUI demo started, press any key to exit");
    
    gui_running = 1;
    
    // Wait for a keypress
    while (!is_key_available() && gui_running) {
        // Update and render windows
        gui_update_windows();
        
        // Process simulated mouse movement
        static int frame = 0;
        if (frame++ % 20 == 0) {
            // Simulate mouse movement in a circle
            int center_x = graphics_get_framebuffer()->width / 2;
            int center_y = graphics_get_framebuffer()->height / 2;
            int radius = 100;
            float angle = (float)frame / 100.0f;
            
            int x = center_x + (int)(cos(angle) * radius);
            int y = center_y + (int)(sin(angle) * radius);
            
            window_process_mouse(x, y, 0, 0);
        }
        
        // Very short delay
        for (volatile int i = 0; i < 5000; i++) { /* delay */ }
    }
    
    // Clear any pending keys
    if (is_key_available()) {
        keyboard_read_key();
    }
    
    // Demo finished
    gui_running = 0;
    log_info("GUI", "GUI demo completed");
}

/**
 * Initialize demo windows
 */
void gui_init_demo_windows() {
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) return;
    
    // Calculate some dimensions based on screen size
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Create a main window
    window_t* main_window = window_create(
        screen_width / 2 - 200,
        screen_height / 2 - 150,
        400,
        300,
        "uintOS Demo Window",
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MOVABLE | WINDOW_FLAG_RESIZABLE
    );
    
    // Create some controls in the main window
    control_t* label1 = control_create_label(
        20, 20, 360, 20,
        "Welcome to uintOS GUI System!",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, label1);
    
    control_t* button1 = control_create_button(
        20, 60, 100, 30,
        "Click Me!",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, button1);
    
    control_t* checkbox1 = control_create_checkbox(
        20, 110, 200, 20,
        "Enable Feature",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, checkbox1);
    
    control_t* textbox1 = control_create_textbox(
        20, 150, 360, 30,
        "This is a text input field",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, textbox1);
    
    // Create a small info window
    window_t* info_window = window_create(
        30,
        30,
        250,
        200,
        "System Information",
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MOVABLE
    );
    
    // Add some controls to the info window
    char info_text[128];
    sprintf(info_text, "Screen: %dx%d, %d bpp", fb->width, fb->height, fb->bpp);
    
    control_t* info_label1 = control_create_label(
        10, 20, 230, 20,
        info_text,
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, info_label1);
    
    control_t* info_label2 = control_create_label(
        10, 50, 230, 20,
        "uintOS Graphical User Interface",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, info_label2);
    
    control_t* info_label3 = control_create_label(
        10, 80, 230, 20,
        "Memory: 16 MB RAM",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, info_label3);
    
    control_t* close_button = control_create_button(
        150, 140, 80, 30,
        "Close",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, close_button);
    
    // Create a third window with some drawing
    window_t* draw_window = window_create(
        screen_width - 280,
        70,
        250,
        250,
        "Graphics Demo",
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MOVABLE
    );
    
    // Draw some graphics in this window's client area using a custom control
    control_t* draw_area = control_create_custom(
        10, 10, 230, 230,
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED,
        NULL,  // No event handler yet
        NULL   // No user data
    );
    
    // Set the render function for the custom control
    draw_area->render = NULL; // This would be set to a function that draws the graphics
    
    window_add_control(draw_window, draw_area);
}