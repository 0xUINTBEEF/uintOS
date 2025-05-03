/**
 * @file controls.c
 * @brief UI Control implementations for uintOS GUI
 */

#include "controls.h"
#include "../graphics/graphics.h"
#include "../logging/log.h"
#include "../../hal/include/hal_memory.h"
#include <string.h>

/* Forward declarations of rendering functions */
static void button_render(control_t* control);
static void label_render(control_t* control);
static void checkbox_render(control_t* control);
static void textbox_render(control_t* control);
static void radiobutton_render(control_t* control);
static void listbox_render(control_t* control);
static void progressbar_render(control_t* control);

/* Forward declarations of event handlers */
static void button_handle_event(control_t* control, event_t* event, void* user_data);
static void checkbox_handle_event(control_t* control, event_t* event, void* user_data);
static void textbox_handle_event(control_t* control, event_t* event, void* user_data);
static void radiobutton_handle_event(control_t* control, event_t* event, void* user_data);
static void listbox_handle_event(control_t* control, event_t* event, void* user_data);

/* Forward declarations of cleanup functions */
static void button_destroy(control_t* control);
static void label_destroy(control_t* control);
static void checkbox_destroy(control_t* control);
static void textbox_destroy(control_t* control);
static void radiobutton_destroy(control_t* control);
static void listbox_destroy(control_t* control);
static void progressbar_destroy(control_t* control);

/* Helper functions */
static void draw_3d_rect(control_t* control, int x, int y, int width, int height, int raised);
static void draw_text_aligned(int x, int y, int width, int height, const char* text, text_align_t align, uint32_t color);

/**
 * Create a button control
 */
control_t* button_create(int x, int y, int width, int height, const char* text, int style) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for button control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = CONTROL_COLOR_BG;
    control->fg_color = CONTROL_COLOR_FG;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_TABSTOP;
    control->render = button_render;
    control->handler = button_handle_event;
    control->destroy = button_destroy;
    
    // Allocate button-specific data
    button_data_t* data = hal_memory_alloc(sizeof(button_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for button data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize button data
    memset(data, 0, sizeof(button_data_t));
    strncpy(data->text, text, sizeof(data->text) - 1);
    data->text_align = TEXT_ALIGN_CENTER;
    data->style = style;
    data->state = BUTTON_STATE_NORMAL;
    
    // Set user data to button data
    control->user_data = data;
    
    return control;
}

/**
 * Set button click handler
 */
void button_set_click_handler(control_t* button, void (*on_click)(control_t*)) {
    if (!button || !button->user_data) return;
    
    button_data_t* data = (button_data_t*)button->user_data;
    data->on_click = on_click;
}

/**
 * Set button text
 */
void button_set_text(control_t* button, const char* text) {
    if (!button || !button->user_data || !text) return;
    
    button_data_t* data = (button_data_t*)button->user_data;
    strncpy(data->text, text, sizeof(data->text) - 1);
    data->text[sizeof(data->text) - 1] = '\0';
    
    // Redraw the button
    if (button->parent && button->render) {
        button->render(button);
    }
}

/**
 * Handle button events
 */
static void button_handle_event(control_t* control, event_t* event, void* user_data) {
    if (!control || !control->user_data || !event) return;
    
    button_data_t* data = (button_data_t*)control->user_data;
    
    // Handle only if enabled
    if (!(control->flags & CONTROL_FLAG_ENABLED)) return;
    
    switch (event->type) {
        case EVENT_MOUSE_MOVE:
            // Handle hover state
            data->state = BUTTON_STATE_HOVER;
            if (control->render) control->render(control);
            break;
            
        case EVENT_MOUSE_DOWN:
            if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
                // Handle press state
                data->state = BUTTON_STATE_PRESSED;
                if (control->render) control->render(control);
            }
            break;
            
        case EVENT_MOUSE_UP:
            if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
                // Check if mouse is still over the button
                if (event->data.mouse.x >= 0 && event->data.mouse.x < control->width &&
                    event->data.mouse.y >= 0 && event->data.mouse.y < control->height) {
                    
                    // Handle click event
                    if (data->on_click) {
                        data->on_click(control);
                    }
                }
                
                // Reset state
                data->state = BUTTON_STATE_NORMAL;
                if (control->render) control->render(control);
            }
            break;
            
        default:
            break;
    }
}

/**
 * Render button control
 */
static void button_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    button_data_t* data = (button_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Draw button background
    uint32_t bg_color = control->bg_color;
    
    // Adjust color based on state
    if (!(control->flags & CONTROL_FLAG_ENABLED)) {
        // Disabled
        bg_color = CONTROL_COLOR_DISABLED;
    } else if (data->state == BUTTON_STATE_PRESSED) {
        // Pressed - darken
        uint8_t r = ((bg_color >> 16) & 0xFF) * 0.8;
        uint8_t g = ((bg_color >> 8) & 0xFF) * 0.8;
        uint8_t b = (bg_color & 0xFF) * 0.8;
        bg_color = (r << 16) | (g << 8) | b;
    } else if (data->state == BUTTON_STATE_HOVER) {
        // Hover - lighten
        uint8_t r = ((bg_color >> 16) & 0xFF) * 1.1;
        if (r > 255) r = 255;
        uint8_t g = ((bg_color >> 8) & 0xFF) * 1.1;
        if (g > 255) g = 255;
        uint8_t b = (bg_color & 0xFF) * 1.1;
        if (b > 255) b = 255;
        bg_color = (r << 16) | (g << 8) | b;
    }
    
    // Draw button based on style
    switch (data->style) {
        case BUTTON_STYLE_FLAT:
            // Simple filled rectangle
            graphics_draw_rect(abs_x, abs_y, control->width, control->height, bg_color, 1);
            
            // Draw border
            graphics_draw_rect(abs_x, abs_y, control->width, control->height, CONTROL_COLOR_BORDER, 0);
            break;
            
        case BUTTON_STYLE_3D:
            // Fill background
            graphics_draw_rect(abs_x, abs_y, control->width, control->height, bg_color, 1);
            
            // Draw 3D effect
            draw_3d_rect(control, abs_x, abs_y, control->width, control->height, 
                        data->state != BUTTON_STATE_PRESSED);
            break;
            
        case BUTTON_STYLE_NORMAL:
        default:
            // Fill background
            graphics_draw_rect(abs_x, abs_y, control->width, control->height, bg_color, 1);
            
            // Draw border
            graphics_draw_rect(abs_x, abs_y, control->width, control->height, CONTROL_COLOR_BORDER, 0);
            
            // Draw slight 3D effect
            if (data->state == BUTTON_STATE_PRESSED) {
                // Pressed - darker bottom-right edge
                graphics_draw_line(abs_x + 1, abs_y + control->height - 1, 
                                abs_x + control->width - 1, abs_y + control->height - 1, 
                                CONTROL_COLOR_BORDER);
                graphics_draw_line(abs_x + control->width - 1, abs_y + 1, 
                                abs_x + control->width - 1, abs_y + control->height - 1, 
                                CONTROL_COLOR_BORDER);
            } else {
                // Normal - highlight top-left edge
                graphics_draw_line(abs_x + 1, abs_y + 1, 
                                abs_x + control->width - 2, abs_y + 1, 
                                COLOR_WHITE);
                graphics_draw_line(abs_x + 1, abs_y + 1, 
                                abs_x + 1, abs_y + control->height - 2, 
                                COLOR_WHITE);
            }
            break;
    }
    
    // Draw text
    int text_y_offset = data->state == BUTTON_STATE_PRESSED ? 1 : 0;
    draw_text_aligned(abs_x + 2, abs_y + 2 + text_y_offset, 
                    control->width - 4, control->height - 4, 
                    data->text, data->text_align, 
                    control->flags & CONTROL_FLAG_ENABLED ? control->fg_color : CONTROL_COLOR_DISABLED);
}

/**
 * Clean up button resources
 */
static void button_destroy(control_t* control) {
    if (!control) return;
    
    // Free button-specific data
    if (control->user_data) {
        hal_memory_free(control->user_data);
        control->user_data = NULL;
    }
}

/**
 * Create a label control
 */
control_t* label_create(int x, int y, int width, int height, const char* text, text_align_t align) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for label control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = CONTROL_COLOR_BG;
    control->fg_color = CONTROL_COLOR_FG;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_TRANSPARENT;
    control->render = label_render;
    control->destroy = label_destroy;
    
    // Allocate label-specific data
    label_data_t* data = hal_memory_alloc(sizeof(label_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for label data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize label data
    memset(data, 0, sizeof(label_data_t));
    strncpy(data->text, text, sizeof(data->text) - 1);
    data->text_align = align;
    
    // Set user data to label data
    control->user_data = data;
    
    return control;
}

/**
 * Set label text
 */
void label_set_text(control_t* label, const char* text) {
    if (!label || !label->user_data || !text) return;
    
    label_data_t* data = (label_data_t*)label->user_data;
    strncpy(data->text, text, sizeof(data->text) - 1);
    data->text[sizeof(data->text) - 1] = '\0';
    
    // Redraw the label
    if (label->parent && label->render) {
        label->render(label);
    }
}

/**
 * Render label control
 */
static void label_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    label_data_t* data = (label_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Draw background if not transparent
    if (!(control->flags & CONTROL_FLAG_TRANSPARENT)) {
        graphics_draw_rect(abs_x, abs_y, control->width, control->height, control->bg_color, 1);
    }
    
    // Draw text
    draw_text_aligned(abs_x, abs_y, control->width, control->height, data->text, data->text_align, control->fg_color);
}

/**
 * Clean up label resources
 */
static void label_destroy(control_t* control) {
    if (!control) return;
    
    // Free label-specific data
    if (control->user_data) {
        hal_memory_free(control->user_data);
        control->user_data = NULL;
    }
}

/**
 * Create a checkbox control
 */
control_t* checkbox_create(int x, int y, int width, int height, const char* text, int checked) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for checkbox control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = CONTROL_COLOR_BG;
    control->fg_color = CONTROL_COLOR_FG;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_TABSTOP;
    control->render = checkbox_render;
    control->handler = checkbox_handle_event;
    control->destroy = checkbox_destroy;
    
    // Allocate checkbox-specific data
    checkbox_data_t* data = hal_memory_alloc(sizeof(checkbox_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for checkbox data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize checkbox data
    memset(data, 0, sizeof(checkbox_data_t));
    strncpy(data->text, text, sizeof(data->text) - 1);
    data->checked = checked;
    
    // Set user data to checkbox data
    control->user_data = data;
    
    return control;
}

/**
 * Get checkbox state
 */
int checkbox_get_checked(control_t* checkbox) {
    if (!checkbox || !checkbox->user_data) return 0;
    
    checkbox_data_t* data = (checkbox_data_t*)checkbox->user_data;
    return data->checked;
}

/**
 * Set checkbox state
 */
void checkbox_set_checked(control_t* checkbox, int checked) {
    if (!checkbox || !checkbox->user_data) return;
    
    checkbox_data_t* data = (checkbox_data_t*)checkbox->user_data;
    if (data->checked != checked) {
        data->checked = checked;
        
        // Call change handler if available
        if (data->on_change) {
            data->on_change(checkbox, checked);
        }
        
        // Redraw the checkbox
        if (checkbox->parent && checkbox->render) {
            checkbox->render(checkbox);
        }
    }
}

/**
 * Set checkbox change handler
 */
void checkbox_set_change_handler(control_t* checkbox, void (*on_change)(control_t*, int)) {
    if (!checkbox || !checkbox->user_data) return;
    
    checkbox_data_t* data = (checkbox_data_t*)checkbox->user_data;
    data->on_change = on_change;
}

/**
 * Handle checkbox events
 */
static void checkbox_handle_event(control_t* control, event_t* event, void* user_data) {
    if (!control || !control->user_data || !event) return;
    
    // Handle only if enabled
    if (!(control->flags & CONTROL_FLAG_ENABLED)) return;
    
    checkbox_data_t* data = (checkbox_data_t*)control->user_data;
    
    if (event->type == EVENT_MOUSE_UP && event->data.mouse.button == MOUSE_BUTTON_LEFT) {
        // Toggle checkbox state
        data->checked = !data->checked;
        
        // Call change handler if available
        if (data->on_change) {
            data->on_change(control, data->checked);
        }
        
        // Redraw the checkbox
        if (control->render) {
            control->render(control);
        }
    }
}

/**
 * Render checkbox control
 */
static void checkbox_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    checkbox_data_t* data = (checkbox_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Calculate checkbox box size and position
    int box_size = control->height - 4;
    if (box_size < 8) box_size = 8;
    if (box_size > 16) box_size = 16;
    
    int box_x = abs_x + 2;
    int box_y = abs_y + (control->height - box_size) / 2;
    
    // Draw checkbox box
    graphics_draw_rect(box_x, box_y, box_size, box_size, COLOR_WHITE, 1);
    graphics_draw_rect(box_x, box_y, box_size, box_size, CONTROL_COLOR_BORDER, 0);
    
    // Draw checkbox mark if checked
    if (data->checked) {
        for (int i = 0; i < box_size - 4; i++) {
            graphics_draw_line(box_x + 2 + i, box_y + 2 + i, 
                             box_x + 2 + i, box_y + box_size - 2 - i, 
                             control->flags & CONTROL_FLAG_ENABLED ? control->fg_color : CONTROL_COLOR_DISABLED);
            graphics_draw_line(box_x + 2 + i, box_y + box_size - 2 - i, 
                             box_x + box_size - 2, box_y + 2, 
                             control->flags & CONTROL_FLAG_ENABLED ? control->fg_color : CONTROL_COLOR_DISABLED);
        }
    }
    
    // Draw text
    int text_x = box_x + box_size + 4;
    int text_width = control->width - (text_x - abs_x);
    draw_text_aligned(text_x, abs_y, text_width, control->height, 
                    data->text, TEXT_ALIGN_LEFT, 
                    control->flags & CONTROL_FLAG_ENABLED ? control->fg_color : CONTROL_COLOR_DISABLED);
}

/**
 * Clean up checkbox resources
 */
static void checkbox_destroy(control_t* control) {
    if (!control) return;
    
    // Free checkbox-specific data
    if (control->user_data) {
        hal_memory_free(control->user_data);
        control->user_data = NULL;
    }
}

/**
 * Create a textbox control
 */
control_t* textbox_create(int x, int y, int width, int height, int max_len, int is_multiline) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for textbox control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = COLOR_WHITE;
    control->fg_color = CONTROL_COLOR_FG;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_TABSTOP | CONTROL_FLAG_BORDER;
    control->render = textbox_render;
    control->handler = textbox_handle_event;
    control->destroy = textbox_destroy;
    
    // Allocate textbox-specific data
    textbox_data_t* data = hal_memory_alloc(sizeof(textbox_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for textbox data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize textbox data
    memset(data, 0, sizeof(textbox_data_t));
    data->max_len = max_len > 0 ? max_len : 1024;
    data->is_multiline = is_multiline;
    data->cursor_pos = 0;
    data->selection_start = -1;
    data->selection_end = -1;
    
    // Allocate text buffer
    data->text = hal_memory_alloc(data->max_len + 1);
    if (!data->text) {
        LOG(LOG_ERROR, "Failed to allocate textbox text buffer");
        hal_memory_free(data);
        hal_memory_free(control);
        return NULL;
    }
    
    // Clear text buffer
    memset(data->text, 0, data->max_len + 1);
    
    // Set user data to textbox data
    control->user_data = data;
    
    return control;
}

/**
 * Get textbox text
 */
const char* textbox_get_text(control_t* textbox) {
    if (!textbox || !textbox->user_data) return NULL;
    
    textbox_data_t* data = (textbox_data_t*)textbox->user_data;
    return data->text;
}

/**
 * Set textbox text
 */
void textbox_set_text(control_t* textbox, const char* text) {
    if (!textbox || !textbox->user_data || !text) return;
    
    textbox_data_t* data = (textbox_data_t*)textbox->user_data;
    
    // Copy text, ensuring it doesn't exceed buffer size
    strncpy(data->text, text, data->max_len);
    data->text[data->max_len] = '\0';
    
    // Update text length
    data->text_len = strlen(data->text);
    
    // Reset cursor and selection
    data->cursor_pos = data->text_len;
    data->selection_start = -1;
    data->selection_end = -1;
    
    // Call change handler if available
    if (data->on_change) {
        data->on_change(textbox);
    }
    
    // Redraw the textbox
    if (textbox->parent && textbox->render) {
        textbox->render(textbox);
    }
}

/**
 * Set whether textbox is a password field
 */
void textbox_set_password(control_t* textbox, int is_password) {
    if (!textbox || !textbox->user_data) return;
    
    textbox_data_t* data = (textbox_data_t*)textbox->user_data;
    data->is_password = is_password;
    
    // Redraw the textbox
    if (textbox->parent && textbox->render) {
        textbox->render(textbox);
    }
}

/**
 * Set textbox change handler
 */
void textbox_set_change_handler(control_t* textbox, void (*on_change)(control_t*)) {
    if (!textbox || !textbox->user_data) return;
    
    textbox_data_t* data = (textbox_data_t*)textbox->user_data;
    data->on_change = on_change;
}

/**
 * Handle textbox events
 */
static void textbox_handle_event(control_t* control, event_t* event, void* user_data) {
    if (!control || !control->user_data || !event) return;
    
    // Handle only if enabled
    if (!(control->flags & CONTROL_FLAG_ENABLED)) return;
    
    textbox_data_t* data = (textbox_data_t*)control->user_data;
    
    switch (event->type) {
        case EVENT_MOUSE_DOWN:
            if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
                // Set focus to this control
                if (control->parent) {
                    for (int i = 0; i < control->parent->control_count; i++) {
                        control_t* other = control->parent->controls[i];
                        if (other && other != control && (other->flags & CONTROL_FLAG_FOCUSED)) {
                            other->flags &= ~CONTROL_FLAG_FOCUSED;
                            if (other->render) other->render(other);
                        }
                    }
                }
                
                control->flags |= CONTROL_FLAG_FOCUSED;
                
                // Set cursor position based on click position
                // TODO: Implement more accurate cursor positioning based on text metrics
                int click_x = event->data.mouse.x;
                data->cursor_pos = (click_x * data->text_len) / (control->width - 8);
                if (data->cursor_pos > data->text_len) {
                    data->cursor_pos = data->text_len;
                }
                
                // Clear selection
                data->selection_start = -1;
                data->selection_end = -1;
                
                // Redraw
                if (control->render) control->render(control);
            }
            break;
            
        case EVENT_KEY_DOWN:
            if (control->flags & CONTROL_FLAG_FOCUSED) {
                char key = event->data.key.key;
                int scancode = event->data.key.scancode;
                
                // Handle special keys
                switch (scancode) {
                    case 0x0E: // Backspace
                        if (data->cursor_pos > 0) {
                            // Remove character before cursor
                            for (int i = data->cursor_pos - 1; i < data->text_len; i++) {
                                data->text[i] = data->text[i + 1];
                            }
                            data->text_len--;
                            data->cursor_pos--;
                            
                            // Call change handler
                            if (data->on_change) {
                                data->on_change(control);
                            }
                            
                            // Redraw
                            if (control->render) control->render(control);
                        }
                        break;
                        
                    case 0x53: // Delete
                        if (data->cursor_pos < data->text_len) {
                            // Remove character at cursor
                            for (int i = data->cursor_pos; i < data->text_len; i++) {
                                data->text[i] = data->text[i + 1];
                            }
                            data->text_len--;
                            
                            // Call change handler
                            if (data->on_change) {
                                data->on_change(control);
                            }
                            
                            // Redraw
                            if (control->render) control->render(control);
                        }
                        break;
                        
                    case 0x4B: // Left arrow
                        if (data->cursor_pos > 0) {
                            data->cursor_pos--;
                            if (control->render) control->render(control);
                        }
                        break;
                        
                    case 0x4D: // Right arrow
                        if (data->cursor_pos < data->text_len) {
                            data->cursor_pos++;
                            if (control->render) control->render(control);
                        }
                        break;
                        
                    case 0x47: // Home
                        data->cursor_pos = 0;
                        if (control->render) control->render(control);
                        break;
                        
                    case 0x4F: // End
                        data->cursor_pos = data->text_len;
                        if (control->render) control->render(control);
                        break;
                        
                    default:
                        // Regular character
                        if (key >= 32 && key < 127 && data->text_len < data->max_len) {
                            // Insert character at cursor
                            for (int i = data->text_len; i > data->cursor_pos; i--) {
                                data->text[i] = data->text[i - 1];
                            }
                            data->text[data->cursor_pos] = key;
                            data->text_len++;
                            data->cursor_pos++;
                            
                            // Call change handler
                            if (data->on_change) {
                                data->on_change(control);
                            }
                            
                            // Redraw
                            if (control->render) control->render(control);
                        }
                        break;
                }
            }
            break;
            
        default:
            break;
    }
}

/**
 * Render textbox control
 */
static void textbox_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    textbox_data_t* data = (textbox_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb || !data->text) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Draw textbox background
    graphics_draw_rect(abs_x, abs_y, control->width, control->height, control->bg_color, 1);
    
    // Draw border
    if (control->flags & CONTROL_FLAG_BORDER) {
        uint32_t border_color = (control->flags & CONTROL_FLAG_FOCUSED) ? 
                              CONTROL_COLOR_HIGHLIGHT : CONTROL_COLOR_BORDER;
        graphics_draw_rect(abs_x, abs_y, control->width, control->height, border_color, 0);
    }
    
    // Draw text
    // TODO: Implement scrolling for long text
    if (data->is_password) {
        // Draw asterisks for password
        char password_text[256];
        int len = data->text_len;
        if (len > 255) len = 255;
        
        for (int i = 0; i < len; i++) {
            password_text[i] = '*';
        }
        password_text[len] = '\0';
        
        graphics_draw_string(abs_x + 4, abs_y + (control->height - 8) / 2, 
                          password_text, control->fg_color, 1);
    } else {
        // Draw normal text
        graphics_draw_string(abs_x + 4, abs_y + (control->height - 8) / 2, 
                          data->text, control->fg_color, 1);
    }
    
    // Draw cursor if focused
    if (control->flags & CONTROL_FLAG_FOCUSED) {
        // Calculate cursor position
        // TODO: Implement more accurate cursor positioning based on text metrics
        int cursor_x;
        if (data->is_password) {
            cursor_x = abs_x + 4 + data->cursor_pos * 8;
        } else {
            cursor_x = abs_x + 4 + data->cursor_pos * 8;
        }
        
        int cursor_y1 = abs_y + 2;
        int cursor_y2 = abs_y + control->height - 3;
        
        // Draw cursor line
        graphics_draw_line(cursor_x, cursor_y1, cursor_x, cursor_y2, control->fg_color);
    }
}

/**
 * Clean up textbox resources
 */
static void textbox_destroy(control_t* control) {
    if (!control) return;
    
    // Free textbox-specific data
    textbox_data_t* data = (textbox_data_t*)control->user_data;
    if (data) {
        if (data->text) {
            hal_memory_free(data->text);
        }
        hal_memory_free(data);
    }
    
    control->user_data = NULL;
}

/**
 * Create a radio button control
 */
control_t* radiobutton_create(int x, int y, int width, int height, const char* text, int group_id, int selected) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for radio button control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->type = CONTROL_RADIOBUTTON;
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = CONTROL_COLOR_BG;
    control->fg_color = CONTROL_COLOR_FG;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_TABSTOP;
    control->render = radiobutton_render;
    control->handler = radiobutton_handle_event;
    control->destroy = radiobutton_destroy;
    
    // Allocate radio button-specific data
    radiobutton_data_t* data = hal_memory_alloc(sizeof(radiobutton_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for radio button data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize radio button data
    memset(data, 0, sizeof(radiobutton_data_t));
    strncpy(data->text, text, sizeof(data->text) - 1);
    data->group_id = group_id;
    data->selected = selected;
    
    // Set user data to radio button data
    control->user_data = data;
    
    return control;
}

/**
 * Get radio button state
 */
int radiobutton_get_selected(control_t* radio) {
    if (!radio || !radio->user_data) return 0;
    
    radiobutton_data_t* data = (radiobutton_data_t*)radio->user_data;
    return data->selected;
}

/**
 * Set radio button state and unselect others in the same group
 */
void radiobutton_set_selected(control_t* radio, int selected) {
    if (!radio || !radio->user_data || !radio->parent) return;
    
    radiobutton_data_t* data = (radiobutton_data_t*)radio->user_data;
    if (data->selected != selected) {
        // If selecting this radio button, deselect other radio buttons in the same group
        if (selected) {
            for (int i = 0; i < radio->parent->control_count; i++) {
                control_t* other = radio->parent->controls[i];
                if (other && other != radio && other->type == CONTROL_RADIOBUTTON) {
                    radiobutton_data_t* other_data = (radiobutton_data_t*)other->user_data;
                    if (other_data && other_data->group_id == data->group_id && other_data->selected) {
                        other_data->selected = 0;
                        if (other->render) other->render(other);
                    }
                }
            }
        }
        
        data->selected = selected;
        
        // Call select handler if available
        if (data->on_select) {
            data->on_select(radio);
        }
        
        // Redraw the radio button
        if (radio->render) {
            radio->render(radio);
        }
    }
}

/**
 * Set radio button select handler
 */
void radiobutton_set_select_handler(control_t* radio, void (*on_select)(control_t*)) {
    if (!radio || !radio->user_data) return;
    
    radiobutton_data_t* data = (radiobutton_data_t*)radio->user_data;
    data->on_select = on_select;
}

/**
 * Handle radio button events
 */
static void radiobutton_handle_event(control_t* control, event_t* event, void* user_data) {
    if (!control || !control->user_data || !event) return;
    
    // Handle only if enabled
    if (!(control->flags & CONTROL_FLAG_ENABLED)) return;
    
    radiobutton_data_t* data = (radiobutton_data_t*)control->user_data;
    
    if (event->type == EVENT_MOUSE_UP && event->data.mouse.button == MOUSE_BUTTON_LEFT) {
        // Radio buttons can only be selected, not deselected
        if (!data->selected) {
            radiobutton_set_selected(control, 1);
        }
    }
}

/**
 * Render radio button control
 */
static void radiobutton_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    radiobutton_data_t* data = (radiobutton_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Calculate radio circle size and position
    int circle_radius = (control->height - 6) / 2;
    if (circle_radius < 4) circle_radius = 4;
    if (circle_radius > 8) circle_radius = 8;
    
    int circle_x = abs_x + circle_radius + 2;
    int circle_y = abs_y + control->height / 2;
    
    // Draw radio circle
    graphics_draw_circle(circle_x, circle_y, circle_radius, CONTROL_COLOR_BORDER, 0);
    graphics_draw_circle(circle_x, circle_y, circle_radius - 1, COLOR_WHITE, 1);
    
    // Draw selection dot if selected
    if (data->selected) {
        graphics_draw_circle(circle_x, circle_y, circle_radius / 2, 
                          control->flags & CONTROL_FLAG_ENABLED ? control->fg_color : CONTROL_COLOR_DISABLED, 
                          1);
    }
    
    // Draw text
    int text_x = circle_x + circle_radius + 4;
    int text_width = control->width - (text_x - abs_x);
    draw_text_aligned(text_x, abs_y, text_width, control->height, 
                    data->text, TEXT_ALIGN_LEFT, 
                    control->flags & CONTROL_FLAG_ENABLED ? control->fg_color : CONTROL_COLOR_DISABLED);
}

/**
 * Clean up radio button resources
 */
static void radiobutton_destroy(control_t* control) {
    if (!control) return;
    
    // Free radio button-specific data
    if (control->user_data) {
        hal_memory_free(control->user_data);
        control->user_data = NULL;
    }
}

/**
 * Create a listbox control
 */
control_t* listbox_create(int x, int y, int width, int height) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for listbox control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->type = CONTROL_LISTBOX;
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = COLOR_WHITE;
    control->fg_color = CONTROL_COLOR_FG;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_TABSTOP | CONTROL_FLAG_BORDER;
    control->render = listbox_render;
    control->handler = listbox_handle_event;
    control->destroy = listbox_destroy;
    
    // Allocate listbox-specific data
    listbox_data_t* data = hal_memory_alloc(sizeof(listbox_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for listbox data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize listbox data
    memset(data, 0, sizeof(listbox_data_t));
    data->selected_index = -1;
    data->scroll_pos = 0;
    data->item_height = 16; // Default item height
    data->visible_items = (height - 4) / data->item_height;
    
    // Set user data to listbox data
    control->user_data = data;
    
    return control;
}

/**
 * Add an item to the listbox
 */
int listbox_add_item(control_t* listbox, const char* text, void* item_data) {
    if (!listbox || !listbox->user_data || !text) return -1;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    
    // Create new listbox item
    listbox_item_t* item = hal_memory_alloc(sizeof(listbox_item_t));
    if (!item) {
        LOG(LOG_ERROR, "Failed to allocate memory for listbox item");
        return -1;
    }
    
    // Initialize item
    memset(item, 0, sizeof(listbox_item_t));
    strncpy(item->text, text, sizeof(item->text) - 1);
    item->data = item_data;
    item->next = NULL;
    
    // Add to the end of the list
    if (data->items == NULL) {
        // First item
        data->items = item;
    } else {
        // Find the last item
        listbox_item_t* last = data->items;
        while (last->next != NULL) {
            last = last->next;
        }
        
        // Add to end
        last->next = item;
    }
    
    // Increment item count
    data->item_count++;
    
    // Select first item if none selected
    if (data->selected_index < 0 && data->item_count == 1) {
        data->selected_index = 0;
    }
    
    // Redraw the listbox
    if (listbox->parent && listbox->render) {
        listbox->render(listbox);
    }
    
    return data->item_count - 1;
}

/**
 * Remove an item from the listbox
 */
void listbox_remove_item(control_t* listbox, int index) {
    if (!listbox || !listbox->user_data || index < 0) return;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    
    // Check if index is valid
    if (index >= data->item_count) return;
    
    // Find the item to remove
    listbox_item_t* item = data->items;
    listbox_item_t* prev = NULL;
    int current = 0;
    
    while (item && current < index) {
        prev = item;
        item = item->next;
        current++;
    }
    
    if (item) {
        // Remove the item
        if (prev) {
            prev->next = item->next;
        } else {
            data->items = item->next;
        }
        
        // Free the item
        hal_memory_free(item);
        
        // Update item count
        data->item_count--;
        
        // Update selected index
        if (data->selected_index == index) {
            // Selection was removed, select next item or none
            if (data->item_count > 0) {
                if (data->selected_index >= data->item_count) {
                    data->selected_index = data->item_count - 1;
                }
            } else {
                data->selected_index = -1;
            }
        } else if (data->selected_index > index) {
            // Selection was after the removed item, decrement
            data->selected_index--;
        }
        
        // Update scroll position
        if (data->scroll_pos > 0 && data->scroll_pos + data->visible_items > data->item_count) {
            data->scroll_pos = data->item_count - data->visible_items;
            if (data->scroll_pos < 0) data->scroll_pos = 0;
        }
        
        // Redraw the listbox
        if (listbox->parent && listbox->render) {
            listbox->render(listbox);
        }
    }
}

/**
 * Clear all items from the listbox
 */
void listbox_clear(control_t* listbox) {
    if (!listbox || !listbox->user_data) return;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    
    // Free all items
    listbox_item_t* item = data->items;
    while (item) {
        listbox_item_t* next = item->next;
        hal_memory_free(item);
        item = next;
    }
    
    // Reset listbox data
    data->items = NULL;
    data->item_count = 0;
    data->selected_index = -1;
    data->scroll_pos = 0;
    
    // Redraw the listbox
    if (listbox->parent && listbox->render) {
        listbox->render(listbox);
    }
}

/**
 * Get selected item index
 */
int listbox_get_selected(control_t* listbox) {
    if (!listbox || !listbox->user_data) return -1;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    return data->selected_index;
}

/**
 * Set selected item index
 */
void listbox_set_selected(control_t* listbox, int index) {
    if (!listbox || !listbox->user_data) return;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    
    // Validate index
    if (index < -1 || (index >= data->item_count && data->item_count > 0)) return;
    
    if (data->selected_index != index) {
        data->selected_index = index;
        
        // Adjust scroll position to ensure the selected item is visible
        if (index >= 0) {
            if (index < data->scroll_pos) {
                data->scroll_pos = index;
            } else if (index >= data->scroll_pos + data->visible_items) {
                data->scroll_pos = index - data->visible_items + 1;
                if (data->scroll_pos < 0) data->scroll_pos = 0;
            }
        }
        
        // Call select handler if available
        if (data->on_select) {
            data->on_select(listbox, index);
        }
        
        // Redraw the listbox
        if (listbox->parent && listbox->render) {
            listbox->render(listbox);
        }
    }
}

/**
 * Get item data for the specified index
 */
void* listbox_get_item_data(control_t* listbox, int index) {
    if (!listbox || !listbox->user_data || index < 0) return NULL;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    
    // Check if index is valid
    if (index >= data->item_count) return NULL;
    
    // Find the item
    listbox_item_t* item = data->items;
    int current = 0;
    
    while (item && current < index) {
        item = item->next;
        current++;
    }
    
    return item ? item->data : NULL;
}

/**
 * Set listbox select handler
 */
void listbox_set_select_handler(control_t* listbox, void (*on_select)(control_t*, int)) {
    if (!listbox || !listbox->user_data) return;
    
    listbox_data_t* data = (listbox_data_t*)listbox->user_data;
    data->on_select = on_select;
}

/**
 * Handle listbox events
 */
static void listbox_handle_event(control_t* control, event_t* event, void* user_data) {
    if (!control || !control->user_data || !event) return;
    
    // Handle only if enabled
    if (!(control->flags & CONTROL_FLAG_ENABLED)) return;
    
    listbox_data_t* data = (listbox_data_t*)control->user_data;
    
    switch (event->type) {
        case EVENT_MOUSE_DOWN:
            if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
                // Set focus to this control
                if (control->parent) {
                    for (int i = 0; i < control->parent->control_count; i++) {
                        control_t* other = control->parent->controls[i];
                        if (other && other != control && (other->flags & CONTROL_FLAG_FOCUSED)) {
                            other->flags &= ~CONTROL_FLAG_FOCUSED;
                            if (other->render) other->render(other);
                        }
                    }
                }
                
                control->flags |= CONTROL_FLAG_FOCUSED;
                
                // Calculate which item was clicked
                int y = event->data.mouse.y;
                int index = data->scroll_pos + (y - 2) / data->item_height;
                
                // Select the clicked item
                if (index >= 0 && index < data->item_count) {
                    listbox_set_selected(control, index);
                }
            }
            break;
            
        case EVENT_KEY_DOWN:
            if (control->flags & CONTROL_FLAG_FOCUSED) {
                int scancode = event->data.key.scancode;
                
                switch (scancode) {
                    case 0x48: // Up arrow
                        if (data->selected_index > 0) {
                            listbox_set_selected(control, data->selected_index - 1);
                        }
                        break;
                        
                    case 0x50: // Down arrow
                        if (data->selected_index < data->item_count - 1) {
                            listbox_set_selected(control, data->selected_index + 1);
                        }
                        break;
                        
                    case 0x49: // Page up
                        if (data->selected_index > 0) {
                            int new_index = data->selected_index - data->visible_items;
                            if (new_index < 0) new_index = 0;
                            listbox_set_selected(control, new_index);
                        }
                        break;
                        
                    case 0x51: // Page down
                        if (data->selected_index < data->item_count - 1) {
                            int new_index = data->selected_index + data->visible_items;
                            if (new_index >= data->item_count) new_index = data->item_count - 1;
                            listbox_set_selected(control, new_index);
                        }
                        break;
                        
                    case 0x47: // Home
                        if (data->item_count > 0) {
                            listbox_set_selected(control, 0);
                        }
                        break;
                        
                    case 0x4F: // End
                        if (data->item_count > 0) {
                            listbox_set_selected(control, data->item_count - 1);
                        }
                        break;
                }
            }
            break;
            
        default:
            break;
    }
}

/**
 * Render listbox control
 */
static void listbox_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    listbox_data_t* data = (listbox_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Draw listbox background
    graphics_draw_rect(abs_x, abs_y, control->width, control->height, control->bg_color, 1);
    
    // Draw border
    if (control->flags & CONTROL_FLAG_BORDER) {
        uint32_t border_color = (control->flags & CONTROL_FLAG_FOCUSED) ? 
                              CONTROL_COLOR_HIGHLIGHT : CONTROL_COLOR_BORDER;
        graphics_draw_rect(abs_x, abs_y, control->width, control->height, border_color, 0);
    }
    
    // Draw items
    int y = abs_y + 2; // Start 2 pixels from top
    listbox_item_t* item = data->items;
    int index = 0;
    
    // Skip items before scroll position
    while (item && index < data->scroll_pos) {
        item = item->next;
        index++;
    }
    
    // Draw visible items
    while (item && y < abs_y + control->height - 2) {
        // Determine item background color
        uint32_t bg_color = control->bg_color;
        uint32_t text_color = control->fg_color;
        
        // Highlight selected item
        if (index == data->selected_index) {
            bg_color = CONTROL_COLOR_HIGHLIGHT;
            text_color = COLOR_WHITE;
        }
        
        // Draw item background
        graphics_draw_rect(abs_x + 2, y, control->width - 4, data->item_height, bg_color, 1);
        
        // Draw item text
        graphics_draw_string(abs_x + 4, y + (data->item_height - 8) / 2, item->text, text_color, 1);
        
        // Move to next item
        y += data->item_height;
        item = item->next;
        index++;
    }
    
    // Draw a simple scrollbar if needed
    if (data->item_count > data->visible_items) {
        int scrollbar_width = 8;
        int scrollbar_x = abs_x + control->width - scrollbar_width - 2;
        int scrollbar_y = abs_y + 2;
        int scrollbar_height = control->height - 4;
        
        // Draw scrollbar background
        graphics_draw_rect(scrollbar_x, scrollbar_y, scrollbar_width, scrollbar_height, CONTROL_COLOR_BG, 1);
        
        // Calculate thumb position and size
        int thumb_height = scrollbar_height * data->visible_items / data->item_count;
        if (thumb_height < 8) thumb_height = 8;
        
        int thumb_y = scrollbar_y + (scrollbar_height - thumb_height) * data->scroll_pos / 
                     (data->item_count - data->visible_items);
        
        // Draw thumb
        graphics_draw_rect(scrollbar_x, thumb_y, scrollbar_width, thumb_height, CONTROL_COLOR_BORDER, 1);
    }
}

/**
 * Clean up listbox resources
 */
static void listbox_destroy(control_t* control) {
    if (!control) return;
    
    // Free listbox-specific data
    listbox_data_t* data = (listbox_data_t*)control->user_data;
    if (data) {
        // Free all items
        listbox_item_t* item = data->items;
        while (item) {
            listbox_item_t* next = item->next;
            hal_memory_free(item);
            item = next;
        }
        
        hal_memory_free(data);
    }
    
    control->user_data = NULL;
}

/**
 * Create a progress bar control
 */
control_t* progressbar_create(int x, int y, int width, int height, int min, int max) {
    // Allocate control structure
    control_t* control = hal_memory_alloc(sizeof(control_t));
    if (!control) {
        LOG(LOG_ERROR, "Failed to allocate memory for progress bar control");
        return NULL;
    }
    
    // Clear the control data
    memset(control, 0, sizeof(control_t));
    
    // Set control properties
    control->type = CONTROL_PROGRESSBAR;
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->bg_color = CONTROL_COLOR_BG;
    control->fg_color = CONTROL_COLOR_HIGHLIGHT;
    control->flags = CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_BORDER;
    control->render = progressbar_render;
    control->destroy = progressbar_destroy;
    
    // Allocate progress bar-specific data
    progressbar_data_t* data = hal_memory_alloc(sizeof(progressbar_data_t));
    if (!data) {
        LOG(LOG_ERROR, "Failed to allocate memory for progress bar data");
        hal_memory_free(control);
        return NULL;
    }
    
    // Initialize progress bar data
    memset(data, 0, sizeof(progressbar_data_t));
    data->min = min;
    data->max = (max > min) ? max : min + 1; // Ensure max > min
    data->value = min;
    
    // Set user data to progress bar data
    control->user_data = data;
    
    return control;
}

/**
 * Set progress bar range
 */
void progressbar_set_range(control_t* progressbar, int min, int max) {
    if (!progressbar || !progressbar->user_data) return;
    
    progressbar_data_t* data = (progressbar_data_t*)progressbar->user_data;
    
    // Validate range
    if (min >= max) return;
    
    data->min = min;
    data->max = max;
    
    // Adjust current value if needed
    if (data->value < min) data->value = min;
    if (data->value > max) data->value = max;
    
    // Redraw the progress bar
    if (progressbar->parent && progressbar->render) {
        progressbar->render(progressbar);
    }
}

/**
 * Set progress bar value
 */
void progressbar_set_value(control_t* progressbar, int value) {
    if (!progressbar || !progressbar->user_data) return;
    
    progressbar_data_t* data = (progressbar_data_t*)progressbar->user_data;
    
    // Clamp value to range
    if (value < data->min) value = data->min;
    if (value > data->max) value = data->max;
    
    if (data->value != value) {
        data->value = value;
        
        // Redraw the progress bar
        if (progressbar->parent && progressbar->render) {
            progressbar->render(progressbar);
        }
    }
}

/**
 * Get progress bar value
 */
int progressbar_get_value(control_t* progressbar) {
    if (!progressbar || !progressbar->user_data) return 0;
    
    progressbar_data_t* data = (progressbar_data_t*)progressbar->user_data;
    return data->value;
}

/**
 * Render progress bar control
 */
static void progressbar_render(control_t* control) {
    if (!control || !control->user_data || !control->parent) return;
    
    progressbar_data_t* data = (progressbar_data_t*)control->user_data;
    framebuffer_t* fb = graphics_get_framebuffer();
    
    if (!fb) return;
    
    // Skip if not visible
    if (!(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Calculate absolute position
    int abs_x = control->parent->x + control->parent->client_x + control->x;
    int abs_y = control->parent->y + control->parent->client_y + control->y;
    
    // Draw progress bar background
    graphics_draw_rect(abs_x, abs_y, control->width, control->height, control->bg_color, 1);
    
    // Draw border
    if (control->flags & CONTROL_FLAG_BORDER) {
        graphics_draw_rect(abs_x, abs_y, control->width, control->height, CONTROL_COLOR_BORDER, 0);
    }
    
    // Calculate filled portion
    int range = data->max - data->min;
    int value = data->value - data->min;
    int filled_width = 0;
    
    if (range > 0 && value > 0) {
        filled_width = (control->width - 4) * value / range;
    }
    
    // Draw filled portion
    if (filled_width > 0) {
        graphics_draw_rect(abs_x + 2, abs_y + 2, filled_width, control->height - 4, control->fg_color, 1);
    }
}

/**
 * Clean up progress bar resources
 */
static void progressbar_destroy(control_t* control) {
    if (!control) return;
    
    // Free progress bar-specific data
    if (control->user_data) {
        hal_memory_free(control->user_data);
        control->user_data = NULL;
    }
}

/* Helper function implementations */

/**
 * Draw a 3D effect rectangle
 */
static void draw_3d_rect(control_t* control, int x, int y, int width, int height, int raised) {
    uint32_t light_color = COLOR_WHITE;
    uint32_t dark_color = COLOR_DARK_GRAY;
    
    if (raised) {
        // Draw light edges (top, left)
        graphics_draw_line(x, y, x + width - 1, y, light_color);
        graphics_draw_line(x, y, x, y + height - 1, light_color);
        
        // Draw dark edges (bottom, right)
        graphics_draw_line(x, y + height - 1, x + width - 1, y + height - 1, dark_color);
        graphics_draw_line(x + width - 1, y, x + width - 1, y + height - 1, dark_color);
    } else {
        // Draw dark edges (top, left)
        graphics_draw_line(x, y, x + width - 1, y, dark_color);
        graphics_draw_line(x, y, x, y + height - 1, dark_color);
        
        // Draw light edges (bottom, right)
        graphics_draw_line(x, y + height - 1, x + width - 1, y + height - 1, light_color);
        graphics_draw_line(x + width - 1, y, x + width - 1, y + height - 1, light_color);
    }
}

/**
 * Draw text with the specified alignment
 */
static void draw_text_aligned(int x, int y, int width, int height, const char* text, text_align_t align, uint32_t color) {
    if (!text) return;
    
    int text_len = strlen(text);
    int text_width = text_len * 8; // Assuming fixed-width font, 8 pixels per character
    int text_x;
    
    switch (align) {
        case TEXT_ALIGN_LEFT:
            text_x = x;
            break;
            
        case TEXT_ALIGN_CENTER:
            text_x = x + (width - text_width) / 2;
            break;
            
        case TEXT_ALIGN_RIGHT:
            text_x = x + width - text_width;
            break;
            
        default:
            text_x = x;
            break;
    }
    
    // Center vertically
    int text_y = y + (height - 8) / 2; // Assuming 8 pixel character height
    
    // Clip text if it exceeds the width
    int max_chars = (x + width - text_x) / 8;
    if (max_chars < text_len) {
        // TODO: Implement better text truncation with ellipsis
        graphics_draw_string(text_x, text_y, text, color, 1);
    } else {
        graphics_draw_string(text_x, text_y, text, color, 1);
    }
}