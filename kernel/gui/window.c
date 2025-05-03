/**
 * @file window.c
 * @brief Window management implementation for uintOS GUI
 */

#include "window.h"
#include "controls.h"
#include "../graphics/graphics.h"
#include "../logging/log.h"
#include <string.h>
#include <stdlib.h>

/* Global window manager state */
static window_t* windows[WINDOW_MAX_WINDOWS];
static int window_count = 0;
static int active_window = -1;
static int dragging_window = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

/**
 * Initialize the window manager
 */
int window_manager_init(void) {
    LOG(LOG_INFO, "Initializing window manager");
    
    // Initialize window array
    for (int i = 0; i < WINDOW_MAX_WINDOWS; i++) {
        windows[i] = NULL;
    }
    
    window_count = 0;
    active_window = -1;
    
    return 0;
}

/**
 * Create a new window
 */
window_t* window_create(int x, int y, int width, int height, const char* title, uint32_t flags) {
    if (window_count >= WINDOW_MAX_WINDOWS) {
        LOG(LOG_ERROR, "Maximum number of windows reached");
        return NULL;
    }
    
    window_t* window = (window_t*)malloc(sizeof(window_t));
    if (!window) {
        LOG(LOG_ERROR, "Failed to allocate memory for window");
        return NULL;
    }
    
    // Initialize window properties
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->flags = flags | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLEBAR | WINDOW_FLAG_CLOSABLE;
    window->bg_color = WINDOW_COLOR_BACKGROUND;
    
    // Set window title
    strncpy(window->title, title, WINDOW_TITLE_MAX_LENGTH - 1);
    window->title[WINDOW_TITLE_MAX_LENGTH - 1] = '\0';
    
    // Calculate client area
    window->client_x = WINDOW_BORDER_WIDTH;
    window->client_y = WINDOW_BORDER_WIDTH;
    
    if (window->flags & WINDOW_FLAG_TITLEBAR) {
        window->client_y += WINDOW_TITLEBAR_HEIGHT;
    }
    
    window->client_width = window->width - (2 * WINDOW_BORDER_WIDTH);
    window->client_height = window->height - window->client_y - WINDOW_BORDER_WIDTH;
    
    // Initialize control array
    for (int i = 0; i < WINDOW_MAX_CONTROLS; i++) {
        window->controls[i] = NULL;
    }
    
    window->control_count = 0;
    window->handler = NULL;
    window->user_data = NULL;
    
    // Add to window array
    windows[window_count] = window;
    active_window = window_count;
    window_count++;
    
    LOG(LOG_INFO, "Created window '%s' (%d,%d,%d,%d)", window->title, x, y, width, height);
    
    return window;
}

/**
 * Destroy a window and free resources
 */
void window_destroy(window_t* window) {
    if (!window) return;
    
    // Find window in array
    int window_index = -1;
    for (int i = 0; i < window_count; i++) {
        if (windows[i] == window) {
            window_index = i;
            break;
        }
    }
    
    if (window_index == -1) {
        LOG(LOG_WARN, "Window not found in window array");
        return;
    }
    
    LOG(LOG_INFO, "Destroying window '%s'", window->title);
    
    // Destroy all controls
    for (int i = 0; i < window->control_count; i++) {
        if (window->controls[i] && window->controls[i]->destroy) {
            window->controls[i]->destroy(window->controls[i]);
        }
    }
    
    // Remove from window array
    for (int i = window_index; i < window_count - 1; i++) {
        windows[i] = windows[i + 1];
    }
    
    window_count--;
    
    // Update active window
    if (active_window == window_index) {
        active_window = (window_count > 0) ? window_count - 1 : -1;
    } else if (active_window > window_index) {
        active_window--;
    }
    
    // Free window memory
    free(window);
}

/**
 * Set window position
 */
void window_set_position(window_t* window, int x, int y) {
    if (!window) return;
    
    window->x = x;
    window->y = y;
}

/**
 * Set window size
 */
void window_set_size(window_t* window, int width, int height) {
    if (!window) return;
    
    window->width = width;
    window->height = height;
    
    // Recalculate client area
    window->client_width = window->width - (2 * WINDOW_BORDER_WIDTH);
    window->client_height = window->height - window->client_y - WINDOW_BORDER_WIDTH;
}

/**
 * Set window title
 */
void window_set_title(window_t* window, const char* title) {
    if (!window || !title) return;
    
    strncpy(window->title, title, WINDOW_TITLE_MAX_LENGTH - 1);
    window->title[WINDOW_TITLE_MAX_LENGTH - 1] = '\0';
}

/**
 * Set window event handler
 */
void window_set_handler(window_t* window, void (*handler)(window_t*, event_t*, void*), void* user_data) {
    if (!window) return;
    
    window->handler = handler;
    window->user_data = user_data;
}

/**
 * Add a control to a window
 */
void window_add_control(window_t* window, control_t* control) {
    if (!window || !control) return;
    
    if (window->control_count >= WINDOW_MAX_CONTROLS) {
        LOG(LOG_ERROR, "Maximum number of controls reached for window '%s'", window->title);
        return;
    }
    
    control->parent = window;
    window->controls[window->control_count] = control;
    window->control_count++;
    
    LOG(LOG_INFO, "Added control to window '%s'", window->title);
}

/**
 * Bring a window to the front
 */
void window_bring_to_front(window_t* window) {
    if (!window) return;
    
    // Find window in array
    int window_index = -1;
    for (int i = 0; i < window_count; i++) {
        if (windows[i] == window) {
            window_index = i;
            break;
        }
    }
    
    if (window_index == -1 || window_index == window_count - 1) {
        return;  // Window not found or already at front
    }
    
    // Move window to the end of the array (top of z-order)
    window_t* temp = windows[window_index];
    for (int i = window_index; i < window_count - 1; i++) {
        windows[i] = windows[i + 1];
    }
    
    windows[window_count - 1] = temp;
    active_window = window_count - 1;
}

/**
 * Render all windows
 */
void window_render_all(void) {
    // Render windows from back to front
    for (int i = 0; i < window_count; i++) {
        window_t* window = windows[i];
        
        if (!window || !(window->flags & WINDOW_FLAG_VISIBLE)) {
            continue;
        }
        
        // Draw window border
        if (window->flags & WINDOW_FLAG_BORDER) {
            uint32_t border_color = (i == active_window) ? WINDOW_COLOR_BORDER : WINDOW_COLOR_INACTIVE;
            
            // Top border
            graphics_fill_rect(window->x, window->y, 
                             window->width, WINDOW_BORDER_WIDTH, 
                             border_color);
            
            // Left border
            graphics_fill_rect(window->x, window->y + WINDOW_BORDER_WIDTH,
                             WINDOW_BORDER_WIDTH, window->height - WINDOW_BORDER_WIDTH,
                             border_color);
            
            // Right border
            graphics_fill_rect(window->x + window->width - WINDOW_BORDER_WIDTH, 
                             window->y + WINDOW_BORDER_WIDTH,
                             WINDOW_BORDER_WIDTH, 
                             window->height - WINDOW_BORDER_WIDTH,
                             border_color);
            
            // Bottom border
            graphics_fill_rect(window->x, 
                             window->y + window->height - WINDOW_BORDER_WIDTH,
                             window->width, 
                             WINDOW_BORDER_WIDTH,
                             border_color);
        }
        
        // Draw title bar
        if (window->flags & WINDOW_FLAG_TITLEBAR) {
            uint32_t titlebar_color = (i == active_window) ? WINDOW_COLOR_TITLEBAR : WINDOW_COLOR_INACTIVE;
            
            // Title bar background
            graphics_fill_rect(window->x + WINDOW_BORDER_WIDTH, 
                             window->y + WINDOW_BORDER_WIDTH,
                             window->width - (2 * WINDOW_BORDER_WIDTH), 
                             WINDOW_TITLEBAR_HEIGHT,
                             titlebar_color);
            
            // Title text
            graphics_draw_text(window->x + WINDOW_BORDER_WIDTH + 5, 
                             window->y + WINDOW_BORDER_WIDTH + 5,
                             window->title, 
                             WINDOW_COLOR_TITLEBAR_TEXT);
            
            // Close button
            if (window->flags & WINDOW_FLAG_CLOSABLE) {
                int close_btn_x = window->x + window->width - WINDOW_BORDER_WIDTH - 15;
                int close_btn_y = window->y + WINDOW_BORDER_WIDTH + 5;
                
                graphics_fill_rect(close_btn_x, close_btn_y, 10, 10, 0xFF0000);
                graphics_draw_line(close_btn_x + 2, close_btn_y + 2, 
                                 close_btn_x + 8, close_btn_y + 8, 
                                 0xFFFFFF);
                graphics_draw_line(close_btn_x + 8, close_btn_y + 2, 
                                 close_btn_x + 2, close_btn_y + 8, 
                                 0xFFFFFF);
            }
        }
        
        // Draw client area background
        graphics_fill_rect(window->x + window->client_x, 
                         window->y + window->client_y,
                         window->client_width, 
                         window->client_height,
                         window->bg_color);
        
        // Draw controls
        for (int j = 0; j < window->control_count; j++) {
            control_t* control = window->controls[j];
            
            if (control && control->render) {
                control->render(control);
            }
        }
    }
}

/**
 * Process mouse input
 */
void window_process_mouse(int x, int y, int button, int state) {
    // Check for window dragging
    if (state == 1) { // Mouse button pressed
        if (dragging_window == -1) {
            // Check if mouse is over title bar of a window
            for (int i = window_count - 1; i >= 0; i--) {
                window_t* window = windows[i];
                
                if (!window || !(window->flags & WINDOW_FLAG_VISIBLE)) {
                    continue;
                }
                
                // Check title bar hit
                if ((window->flags & WINDOW_FLAG_TITLEBAR) &&
                    (window->flags & WINDOW_FLAG_MOVABLE) &&
                    x >= window->x + WINDOW_BORDER_WIDTH &&
                    x <= window->x + window->width - WINDOW_BORDER_WIDTH &&
                    y >= window->y + WINDOW_BORDER_WIDTH &&
                    y <= window->y + WINDOW_BORDER_WIDTH + WINDOW_TITLEBAR_HEIGHT) {
                    
                    // Check close button
                    if ((window->flags & WINDOW_FLAG_CLOSABLE) && 
                        x >= window->x + window->width - WINDOW_BORDER_WIDTH - 15 &&
                        x <= window->x + window->width - WINDOW_BORDER_WIDTH - 5 &&
                        y >= window->y + WINDOW_BORDER_WIDTH + 5 &&
                        y <= window->y + WINDOW_BORDER_WIDTH + 15) {
                        
                        // Send close event
                        if (window->handler) {
                            event_t event;
                            event.type = EVENT_WINDOW_CLOSE;
                            event.window = window;
                            window->handler(window, &event, window->user_data);
                        }
                        return;
                    }
                    
                    // Start dragging window
                    dragging_window = i;
                    drag_offset_x = x - window->x;
                    drag_offset_y = y - window->y;
                    
                    // Make window active
                    if (i != active_window) {
                        window_bring_to_front(window);
                    }
                    return;
                }
                
                // Check client area hit
                if (x >= window->x + window->client_x &&
                    x <= window->x + window->client_x + window->client_width &&
                    y >= window->y + window->client_y &&
                    y <= window->y + window->client_y + window->client_height) {
                    
                    // Make window active
                    if (i != active_window) {
                        window_bring_to_front(window);
                    }
                    
                    // Calculate client area coordinates
                    int client_x = x - (window->x + window->client_x);
                    int client_y = y - (window->y + window->client_y);
                    
                    // Check for control hits
                    for (int j = 0; j < window->control_count; j++) {
                        control_t* control = window->controls[j];
                        
                        if (!control || !(control->flags & CONTROL_FLAG_VISIBLE)) {
                            continue;
                        }
                        
                        if (client_x >= control->x && client_x <= control->x + control->width &&
                            client_y >= control->y && client_y <= control->y + control->height) {
                            
                            // Send mouse event to control
                            if (control->handler) {
                                event_t event;
                                event.type = EVENT_MOUSE_DOWN;
                                event.data.mouse.x = client_x - control->x;
                                event.data.mouse.y = client_y - control->y;
                                event.data.mouse.button = MOUSE_BUTTON_LEFT;
                                event.window = window;
                                control->handler(control, &event, NULL);
                            }
                            return;
                        }
                    }
                    
                    return;
                }
            }
        }
    } else { // Mouse button released
        if (dragging_window != -1) {
            dragging_window = -1;
        }
    }
    
    // Move window if dragging
    if (dragging_window != -1) {
        window_t* window = windows[dragging_window];
        if (window) {
            window_set_position(window, x - drag_offset_x, y - drag_offset_y);
        }
    }
}

/**
 * Process keyboard input
 */
void window_process_key(char key, int scancode, int state) {
    if (active_window == -1) return;
    
    window_t* window = windows[active_window];
    if (!window) return;
    
    // Send key event to active window
    if (window->handler) {
        event_t event;
        event.type = state ? EVENT_KEY_DOWN : EVENT_KEY_UP;
        event.data.key.key = key;
        event.data.key.scancode = scancode;
        event.data.key.modifiers = 0; // No modifiers for now
        event.window = window;
        window->handler(window, &event, window->user_data);
    }
    
    // Find focused control (first with focus flag)
    for (int i = 0; i < window->control_count; i++) {
        control_t* control = window->controls[i];
        
        if (control && (control->flags & CONTROL_FLAG_FOCUSED) && control->handler) {
            event_t event;
            event.type = state ? EVENT_KEY_DOWN : EVENT_KEY_UP;
            event.data.key.key = key;
            event.data.key.scancode = scancode;
            event.data.key.modifiers = 0;
            event.window = window;
            control->handler(control, &event, NULL);
            break;
        }
    }
}