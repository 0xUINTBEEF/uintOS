/**
 * @file window.c
 * @brief Window management implementation for the GUI system
 */
#include <stdint.h>
#include <string.h>
#include "window.h"
#include "controls.h"
#include "../logging/log.h"
#include "../graphics/graphics.h"

// Maximum number of windows in the system
#define MAX_WINDOWS 16

// Window array and count
static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int active_window = -1; // Index of the currently active window

// Theme colors
uint32_t window_bg_color = 0xF0F0F0;      // Window background
uint32_t window_border_color = 0x000080;   // Window border
uint32_t titlebar_bg_color = 0x000080;     // Titlebar background
uint32_t titlebar_text_color = 0xFFFFFF;   // Titlebar text
uint32_t control_bg_color = 0xE0E0E0;      // Control background
uint32_t control_text_color = 0x000000;    // Control text
uint32_t control_border_color = 0x808080;  // Control border

/**
 * Set window manager theme colors
 */
void window_set_theme_colors(
    uint32_t window_bg,
    uint32_t window_border,
    uint32_t titlebar_bg,
    uint32_t titlebar_text,
    uint32_t control_bg,
    uint32_t control_text,
    uint32_t control_border
) {
    window_bg_color = window_bg;
    window_border_color = window_border;
    titlebar_bg_color = titlebar_bg;
    titlebar_text_color = titlebar_text;
    control_bg_color = control_bg;
    control_text_color = control_text;
    control_border_color = control_border;
    
    log_debug("GUI", "Window theme changed");
}

/**
 * Create a new window
 */
window_t* window_create(
    int x, int y, int width, int height,
    const char* title,
    uint32_t flags
) {
    // Check if we can create more windows
    if (window_count >= MAX_WINDOWS) {
        log_error("GUI", "Cannot create window, maximum window count reached");
        return NULL;
    }
    
    // Create a new window
    window_t* window = &windows[window_count];
    
    // Initialize window properties
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->min_width = 100;  // Minimum size
    window->min_height = 50;
    window->flags = flags;
    window->drag_state = WINDOW_DRAG_NONE;
    window->resize_state = WINDOW_RESIZE_NONE;
    window->last_mouse_x = 0;
    window->last_mouse_y = 0;
    window->control_count = 0;
    
    // Copy the title (with bounds checking)
    strncpy(window->title, title, WINDOW_TITLE_MAX_LENGTH - 1);
    window->title[WINDOW_TITLE_MAX_LENGTH - 1] = '\0';
    
    // Make this window active
    active_window = window_count;
    window_count++;
    
    log_debug("GUI", "Created window '%s' at (%d,%d) with size %dx%d", 
              window->title, x, y, width, height);
    
    return window;
}

/**
 * Destroy a window
 */
void window_destroy(window_t* window) {
    // Find the window in our array
    int window_index = -1;
    for (int i = 0; i < window_count; i++) {
        if (&windows[i] == window) {
            window_index = i;
            break;
        }
    }
    
    if (window_index < 0) {
        log_error("GUI", "Attempted to destroy a window that doesn't exist");
        return;
    }
    
    log_debug("GUI", "Destroying window '%s'", window->title);
    
    // Move all windows after this one up in the array
    for (int i = window_index; i < window_count - 1; i++) {
        memcpy(&windows[i], &windows[i + 1], sizeof(window_t));
    }
    
    // Decrement the window count
    window_count--;
    
    // Update active window index if needed
    if (active_window == window_index) {
        active_window = window_count > 0 ? window_count - 1 : -1;
    } else if (active_window > window_index) {
        active_window--;
    }
}

/**
 * Add a control to a window
 */
int window_add_control(window_t* window, control_t* control) {
    if (!window || !control) {
        return 0;
    }
    
    // Check if we can add more controls
    if (window->control_count >= sizeof(window->controls) / sizeof(window->controls[0])) {
        log_error("GUI", "Cannot add control to window, maximum control count reached");
        return 0;
    }
    
    // Add the control to the window
    window->controls[window->control_count++] = control;
    
    // Set the parent window for the control
    control->parent = window;
    
    return 1;
}

/**
 * Remove a control from a window
 */
int window_remove_control(window_t* window, control_t* control) {
    if (!window || !control) {
        return 0;
    }
    
    // Find the control in the window
    int control_index = -1;
    for (int i = 0; i < window->control_count; i++) {
        if (window->controls[i] == control) {
            control_index = i;
            break;
        }
    }
    
    if (control_index < 0) {
        return 0;
    }
    
    // Remove the control by shifting all controls after it
    for (int i = control_index; i < window->control_count - 1; i++) {
        window->controls[i] = window->controls[i + 1];
    }
    
    // Decrement the control count
    window->control_count--;
    
    // Clear the parent reference
    control->parent = NULL;
    
    return 1;
}

/**
 * Bring a window to the front
 */
void window_bring_to_front(window_t* window) {
    // Find the window in our array
    int window_index = -1;
    for (int i = 0; i < window_count; i++) {
        if (&windows[i] == window) {
            window_index = i;
            break;
        }
    }
    
    if (window_index < 0 || window_index == window_count - 1) {
        // Already at the front
        return;
    }
    
    // Store the window data
    window_t temp;
    memcpy(&temp, &windows[window_index], sizeof(window_t));
    
    // Move all windows between this one and the front
    for (int i = window_index; i < window_count - 1; i++) {
        memcpy(&windows[i], &windows[i + 1], sizeof(window_t));
    }
    
    // Place the window at the front
    memcpy(&windows[window_count - 1], &temp, sizeof(window_t));
    
    // Update active window index
    active_window = window_count - 1;
}

/**
 * Render the titlebar and frame of a window
 */
static void window_render_frame(window_t* window) {
    int x = window->x;
    int y = window->y;
    int width = window->width;
    int height = window->height;
    int is_active = (&windows[active_window] == window);
    
    // Draw window background
    if (window->flags & WINDOW_FLAG_BORDER) {
        graphics_draw_rect(x, y, width, height, window_bg_color, 1);
        graphics_draw_rect(x, y, width, height, 
                           is_active ? window_border_color : 0x808080, 0);
    } else {
        graphics_draw_rect(x, y, width, height, window_bg_color, 1);
    }
    
    // Draw titlebar if enabled
    if (window->flags & WINDOW_FLAG_TITLEBAR) {
        int titlebar_height = 20;
        
        // Titlebar background
        graphics_draw_rect(x + 1, y + 1, width - 2, titlebar_height, 
                           is_active ? titlebar_bg_color : 0x808080, 1);
        
        // Titlebar title
        graphics_draw_string(x + 5, y + 6, window->title, titlebar_text_color, 1);
        
        // Close button if closable
        if (window->flags & WINDOW_FLAG_CLOSABLE) {
            // Close button background (red for active window)
            uint32_t close_color = is_active ? 0xFF0000 : 0xC00000;
            graphics_draw_rect(x + width - 18, y + 4, 14, 14, close_color, 1);
            
            // Close button X
            uint32_t x_color = 0xFFFFFF;
            graphics_draw_line(x + width - 15, y + 7, x + width - 7, y + 15, x_color);
            graphics_draw_line(x + width - 15, y + 15, x + width - 7, y + 7, x_color);
        }
    }
}

/**
 * Calculate the client area of a window (area where controls are placed)
 */
static void window_calculate_client_area(window_t* window, int* x, int* y, int* width, int* height) {
    *x = window->x;
    *y = window->y;
    *width = window->width;
    *height = window->height;
    
    // Adjust for titlebar if present
    if (window->flags & WINDOW_FLAG_TITLEBAR) {
        *y += 21;  // Titlebar height + 1
        *height -= 21;
    }
    
    // Adjust for border if present
    if (window->flags & WINDOW_FLAG_BORDER) {
        *x += 1;
        *y += 1;
        *width -= 2;
        *height -= 2;
    }
}

/**
 * Render a window and its controls
 */
void window_render(window_t* window) {
    if (!window || !(window->flags & WINDOW_FLAG_VISIBLE)) {
        return;
    }
    
    // Render window frame
    window_render_frame(window);
    
    // Get client area for control rendering
    int client_x, client_y, client_width, client_height;
    window_calculate_client_area(window, &client_x, &client_y, &client_width, &client_height);
    
    // Render all controls
    for (int i = 0; i < window->control_count; i++) {
        control_t* control = window->controls[i];
        if (control && (control->flags & CONTROL_FLAG_VISIBLE)) {
            control_render(control, client_x + control->x, client_y + control->y);
        }
    }
}

/**
 * Render all windows in the correct Z-order
 */
void window_render_all(void) {
    // Render from back to front (first to last in array)
    for (int i = 0; i < window_count; i++) {
        window_render(&windows[i]);
    }
}

/**
 * Check if a point is inside a window's titlebar close button
 */
static int is_point_in_close_button(window_t* window, int x, int y) {
    if (!(window->flags & WINDOW_FLAG_TITLEBAR) || 
        !(window->flags & WINDOW_FLAG_CLOSABLE)) {
        return 0;
    }
    
    int close_x = window->x + window->width - 18;
    int close_y = window->y + 4;
    
    return (x >= close_x && x < close_x + 14 && y >= close_y && y < close_y + 14);
}

/**
 * Check if a point is inside a window's titlebar (for dragging)
 */
static int is_point_in_titlebar(window_t* window, int x, int y) {
    if (!(window->flags & WINDOW_FLAG_TITLEBAR)) {
        return 0;
    }
    
    return (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + 21 &&
            !is_point_in_close_button(window, x, y));
}

/**
 * Check if a point is inside a window's resize area
 */
static window_resize_state_t get_resize_area(window_t* window, int x, int y) {
    if (!(window->flags & WINDOW_FLAG_RESIZABLE)) {
        return WINDOW_RESIZE_NONE;
    }
    
    // Define resize border width
    const int border = 5;
    int right = (x >= window->x + window->width - border && 
                x <= window->x + window->width &&
                y >= window->y && 
                y <= window->y + window->height);
    
    int bottom = (y >= window->y + window->height - border && 
                  y <= window->y + window->height &&
                  x >= window->x && 
                  x <= window->x + window->width);
    
    // Check for corner
    if (right && bottom) {
        return WINDOW_RESIZE_BOTTOM_RIGHT;
    } else if (right) {
        return WINDOW_RESIZE_RIGHT;
    } else if (bottom) {
        return WINDOW_RESIZE_BOTTOM;
    } else {
        return WINDOW_RESIZE_NONE;
    }
}

/**
 * Find the window at a particular screen position
 */
static int find_window_at_position(int x, int y) {
    // Search from front to back (last to first in array)
    for (int i = window_count - 1; i >= 0; i--) {
        window_t* window = &windows[i];
        if ((window->flags & WINDOW_FLAG_VISIBLE) &&
            x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height) {
            return i;
        }
    }
    
    return -1; // No window found
}

/**
 * Process a mouse event for the window manager
 */
void window_process_mouse(int x, int y, int button, int press) {
    static int dragging_window = -1;
    static window_resize_state_t resizing_window_state = WINDOW_RESIZE_NONE;
    
    // First, handle ongoing drag or resize operations
    if (button == 0 && !press) {
        // Mouse released - end any drag/resize operation
        if (dragging_window >= 0) {
            windows[dragging_window].drag_state = WINDOW_DRAG_NONE;
            dragging_window = -1;
        }
        
        if (resizing_window_state != WINDOW_RESIZE_NONE) {
            windows[active_window].resize_state = WINDOW_RESIZE_NONE;
            resizing_window_state = WINDOW_RESIZE_NONE;
        }
    }
    else if (dragging_window >= 0) {
        // Continue window dragging
        window_t* window = &windows[dragging_window];
        int dx = x - window->last_mouse_x;
        int dy = y - window->last_mouse_y;
        
        window->x += dx;
        window->y += dy;
        window->last_mouse_x = x;
        window->last_mouse_y = y;
        return;
    }
    else if (resizing_window_state != WINDOW_RESIZE_NONE) {
        // Continue window resizing
        window_t* window = &windows[active_window];
        int dx = x - window->last_mouse_x;
        int dy = y - window->last_mouse_y;
        
        switch (resizing_window_state) {
            case WINDOW_RESIZE_RIGHT:
                window->width += dx;
                if (window->width < window->min_width) {
                    window->width = window->min_width;
                }
                break;
                
            case WINDOW_RESIZE_BOTTOM:
                window->height += dy;
                if (window->height < window->min_height) {
                    window->height = window->min_height;
                }
                break;
                
            case WINDOW_RESIZE_BOTTOM_RIGHT:
                window->width += dx;
                window->height += dy;
                if (window->width < window->min_width) {
                    window->width = window->min_width;
                }
                if (window->height < window->min_height) {
                    window->height = window->min_height;
                }
                break;
                
            default:
                break;
        }
        
        window->last_mouse_x = x;
        window->last_mouse_y = y;
        return;
    }
    
    // Find the window under the cursor
    int window_index = find_window_at_position(x, y);
    if (window_index < 0) {
        return; // No window at this position
    }
    
    window_t* window = &windows[window_index];
    
    // If button is pressed
    if (press && button) {
        // Check if clicking on the close button
        if (is_point_in_close_button(window, x, y)) {
            // Close the window
            window_destroy(window);
            return;
        }
        
        // Check if clicking on the titlebar (for dragging)
        if (is_point_in_titlebar(window, x, y) && (window->flags & WINDOW_FLAG_MOVABLE)) {
            // Start dragging the window
            window->drag_state = WINDOW_DRAG_MOVE;
            window->last_mouse_x = x;
            window->last_mouse_y = y;
            dragging_window = window_index;
            
            // Bring to front if clicked
            if (window_index != active_window) {
                window_bring_to_front(window);
                window_index = window_count - 1;  // Window is now at the end
            }
            return;
        }
        
        // Check if clicking on a resize handle
        window_resize_state_t resize_state = get_resize_area(window, x, y);
        if (resize_state != WINDOW_RESIZE_NONE) {
            // Start resizing the window
            window->resize_state = resize_state;
            window->last_mouse_x = x;
            window->last_mouse_y = y;
            resizing_window_state = resize_state;
            
            // Bring to front if clicked
            if (window_index != active_window) {
                window_bring_to_front(window);
            }
            return;
        }
        
        // Bring window to front if clicked
        if (window_index != active_window) {
            window_bring_to_front(window);
            window_index = window_count - 1;  // Window is now at the end
        }
    }
    
    // Pass the event to the controls in the window
    if (window_index == active_window) {
        int client_x, client_y, client_width, client_height;
        window_calculate_client_area(window, &client_x, &client_y, &client_width, &client_height);
        
        // Convert screen coords to client coords
        int client_mouse_x = x - client_x;
        int client_mouse_y = y - client_y;
        
        // Process controls in reverse order (front to back)
        for (int i = window->control_count - 1; i >= 0; i--) {
            control_t* control = window->controls[i];
            if (control && (control->flags & CONTROL_FLAG_VISIBLE) &&
                client_mouse_x >= control->x && client_mouse_x < control->x + control->width &&
                client_mouse_y >= control->y && client_mouse_y < control->y + control->height) {
                
                // Pass the event to the control
                control_handle_mouse(
                    control, 
                    client_mouse_x - control->x, 
                    client_mouse_y - control->y, 
                    button, press
                );
                
                // Only one control gets the event
                break;
            }
        }
    }
}

/**
 * Process a keyboard event for the window manager
 */
void window_process_key(int key, int scancode, int press) {
    // Only the active window receives keyboard events
    if (active_window < 0) {
        return;
    }
    
    window_t* window = &windows[active_window];
    
    // Pass to the focused control in the active window
    for (int i = 0; i < window->control_count; i++) {
        control_t* control = window->controls[i];
        if (control && (control->flags & CONTROL_FLAG_VISIBLE) && 
            (control->flags & CONTROL_FLAG_CAN_FOCUS)) {
            control_handle_key(control, key, scancode, press);
        }
    }
}