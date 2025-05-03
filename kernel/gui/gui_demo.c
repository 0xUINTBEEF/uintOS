/**
 * @file gui_demo.c
 * @brief Demo application for the GUI system
 */
#include <stdint.h>
#include <string.h>
#include "window.h"
#include "controls.h"
#include "../logging/log.h"
#include "../graphics/graphics.h"
#include "../graphics/font8x8.h"

// Global application state
static int app_running = 0;
static int current_theme = 0;

// Demo window pointers
static window_t* main_window = NULL;
static window_t* info_window = NULL;
static window_t* draw_window = NULL;

// Control pointers for interaction
static control_t* theme_checkbox = NULL;
static control_t* status_label = NULL;
static control_t* counter_label = NULL;
static control_t* counter_button = NULL;
static control_t* close_button = NULL;

// Application state variables
static int counter_value = 0;

/**
 * Counter button click handler
 */
static void on_counter_button_click(control_t* control) {
    counter_value++;
    
    // Update the counter label
    char counter_text[32];
    sprintf(counter_text, "Counter: %d", counter_value);
    strncpy(counter_label->text, counter_text, CONTROL_TEXT_MAX_LENGTH - 1);
    
    // Update status label
    sprintf(status_label->text, "Button clicked! Count: %d", counter_value);
}

/**
 * Theme checkbox click handler
 */
static void on_theme_checkbox_click(control_t* control) {
    // Toggle between dark and light themes
    if (control->state) {
        current_theme = 2; // Dark theme
        strncpy(status_label->text, "Dark theme selected", CONTROL_TEXT_MAX_LENGTH - 1);
    } else {
        current_theme = 0; // Classic theme
        strncpy(status_label->text, "Classic theme selected", CONTROL_TEXT_MAX_LENGTH - 1);
    }
    
    // Apply the theme
    extern void gui_set_theme(int theme);
    gui_set_theme(current_theme);
}

/**
 * Close button click handler
 */
static void on_close_button_click(control_t* control) {
    app_running = 0;
    strncpy(status_label->text, "Exiting application...", CONTROL_TEXT_MAX_LENGTH - 1);
}

/**
 * Custom drawing function for the graphics demo
 */
static void draw_graphics_demo(control_t* control, int x, int y) {
    // Background
    graphics_draw_rect(x, y, control->width, control->height, 0xFFFFFF, 1);
    
    // Draw a colorful pattern
    static int animation_offset = 0;
    animation_offset = (animation_offset + 1) % 360;
    
    int center_x = x + control->width / 2;
    int center_y = y + control->height / 2;
    int radius = control->width < control->height ? control->width / 3 : control->height / 3;
    
    // Draw a rainbow circle
    for (int deg = 0; deg < 360; deg += 5) {
        float angle = (deg + animation_offset) * 3.14159f / 180.0f;
        int x1 = center_x + (int)(cos(angle) * (radius / 2));
        int y1 = center_y + (int)(sin(angle) * (radius / 2));
        int x2 = center_x + (int)(cos(angle) * radius);
        int y2 = center_y + (int)(sin(angle) * radius);
        
        // Rainbow color
        uint32_t color = 0;
        if (deg < 60) {
            color = 0xFF0000 + (deg * 0x000100); // Red to Yellow
        } else if (deg < 120) {
            color = 0xFFFF00 - ((deg - 60) * 0x010000); // Yellow to Green
        } else if (deg < 180) {
            color = 0x00FF00 + ((deg - 120) * 0x000001); // Green to Cyan
        } else if (deg < 240) {
            color = 0x00FFFF - ((deg - 180) * 0x000100); // Cyan to Blue
        } else if (deg < 300) {
            color = 0x0000FF + ((deg - 240) * 0x010000); // Blue to Magenta
        } else {
            color = 0xFF00FF - ((deg - 300) * 0x000001); // Magenta to Red
        }
        
        graphics_draw_line(x1, y1, x2, y2, color);
    }
    
    // Draw a bouncing ball
    static int ball_x = 50;
    static int ball_y = 50;
    static int ball_dx = 3;
    static int ball_dy = 2;
    
    // Update ball position
    ball_x += ball_dx;
    ball_y += ball_dy;
    
    // Bounce off walls
    if (ball_x < x + 10 || ball_x >= x + control->width - 10) {
        ball_dx = -ball_dx;
        ball_x += ball_dx;
    }
    
    if (ball_y < y + 10 || ball_y >= y + control->height - 10) {
        ball_dy = -ball_dy;
        ball_y += ball_dy;
    }
    
    // Draw the ball
    graphics_draw_circle(ball_x, ball_y, 10, 0xFF0000, 1);
    
    // Draw text
    graphics_draw_string(x + 10, y + 10, "Graphics Demo", 0x000000, 1);
}

/**
 * Initialize the demo windows and controls
 */
void gui_demo_create_windows(void) {
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        log_error("GUI", "Failed to get framebuffer for GUI demo");
        return;
    }
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Create the main window
    main_window = window_create(
        screen_width / 2 - 200, 
        screen_height / 2 - 150, 
        400, 300,
        "uintOS GUI Demo",
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MOVABLE | WINDOW_FLAG_RESIZABLE
    );
    
    if (!main_window) {
        log_error("GUI", "Failed to create main window");
        return;
    }
    
    // Add controls to the main window
    control_t* title_label = control_create_label(
        20, 20, 360, 20,
        "Welcome to uintOS GUI System!",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, title_label);
    
    status_label = control_create_label(
        20, 50, 360, 20,
        "System ready.",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, status_label);
    
    counter_button = control_create_button(
        20, 90, 120, 30,
        "Click Me",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    control_set_click_handler(counter_button, on_counter_button_click);
    window_add_control(main_window, counter_button);
    
    counter_label = control_create_label(
        150, 95, 200, 20,
        "Counter: 0",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, counter_label);
    
    theme_checkbox = control_create_checkbox(
        20, 140, 200, 20,
        "Use Dark Theme",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    control_set_click_handler(theme_checkbox, on_theme_checkbox_click);
    window_add_control(main_window, theme_checkbox);
    
    control_t* textbox = control_create_textbox(
        20, 180, 360, 30,
        "Type here...",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(main_window, textbox);
    
    close_button = control_create_button(
        150, 240, 100, 30,
        "Close",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    control_set_click_handler(close_button, on_close_button_click);
    window_add_control(main_window, close_button);
    
    // Create the info window
    info_window = window_create(
        30, 30, 250, 200,
        "System Information",
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MOVABLE
    );
    
    if (!info_window) {
        log_error("GUI", "Failed to create info window");
        return;
    }
    
    // Add info controls
    char display_info[64];
    sprintf(display_info, "Display: %dx%d, %d bpp", fb->width, fb->height, fb->bpp);
    
    control_t* display_label = control_create_label(
        10, 20, 230, 20,
        display_info,
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, display_label);
    
    control_t* os_label = control_create_label(
        10, 50, 230, 20,
        "uintOS v1.0 Graphical Interface",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, os_label);
    
    control_t* memory_label = control_create_label(
        10, 80, 230, 20,
        "Memory: 16 MB RAM",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    window_add_control(info_window, memory_label);
    
    // Create the drawing demo window
    draw_window = window_create(
        screen_width - 280, 70, 250, 250,
        "Graphics Demo",
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MOVABLE
    );
    
    if (!draw_window) {
        log_error("GUI", "Failed to create draw window");
        return;
    }
    
    // Add a custom drawing control
    control_t* draw_control = control_create_custom(
        10, 10, 230, 230,
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED,
        draw_graphics_demo,
        NULL
    );
    window_add_control(draw_window, draw_control);
}

/**
 * Clean up the demo windows and controls
 */
void gui_demo_cleanup(void) {
    if (draw_window) {
        window_destroy(draw_window);
        draw_window = NULL;
    }
    
    if (info_window) {
        window_destroy(info_window);
        info_window = NULL;
    }
    
    if (main_window) {
        window_destroy(main_window);
        main_window = NULL;
    }
    
    // Reset state
    counter_value = 0;
    app_running = 0;
}

/**
 * Run the GUI demo application
 */
void gui_run_demo(void) {
    log_info("GUI", "Starting GUI demo");
    
    // Create the demo windows
    gui_demo_create_windows();
    
    // Set application running flag
    app_running = 1;
    
    // Main demo loop
    while (app_running) {
        // Update window states as needed
        
        // Periodically update the status
        static int tick = 0;
        if (++tick % 100 == 0) {
            // Update status with system time (if we had it)
        }
        
        // Wait for a short period to prevent CPU hogging
        for (volatile int i = 0; i < 10000; i++) { /* delay */ }
    }
    
    // Clean up
    gui_demo_cleanup();
    
    log_info("GUI", "GUI demo completed");
}