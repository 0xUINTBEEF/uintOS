/**
 * @file controls.c
 * @brief UI controls implementation for the GUI
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "controls.h"
#include "window.h"
#include "clipboard.h"
#include "../logging/log.h"
#include "../graphics/graphics.h"

// Global control pool to avoid dynamic memory allocation
#define MAX_CONTROLS 128
static control_t controls[MAX_CONTROLS];
static int control_count = 0;

// The currently focused control
static control_t* focused_control = NULL;

/**
 * Create a new basic control
 */
static control_t* control_create_basic(
    int x, int y, int width, int height,
    uint32_t flags, control_type_t type
) {
    if (control_count >= MAX_CONTROLS) {
        log_error("CONTROL", "Cannot create control, maximum control count reached");
        return NULL;
    }
    
    control_t* control = &controls[control_count++];
    
    // Initialize control properties
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    control->flags = flags;
    control->type = type;
    control->parent = NULL;
    control->pressed = 0;
    control->render = NULL;
    control->on_click = NULL;
    control->on_key = NULL;
    control->user_data = NULL;
    
    // Clear text buffer
    memset(control->text, 0, CONTROL_TEXT_MAX_LENGTH);
    
    return control;
}

/**
 * Create a label control
 */
control_t* control_create_label(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
) {
    control_t* control = control_create_basic(x, y, width, height, flags, CONTROL_TYPE_LABEL);
    if (!control) return NULL;
    
    // Copy the text
    strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
    control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    return control;
}

/**
 * Create a button control
 */
control_t* control_create_button(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
) {
    control_t* control = control_create_basic(x, y, width, height, flags, CONTROL_TYPE_BUTTON);
    if (!control) return NULL;
    
    // Copy the text
    strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
    control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    return control;
}

/**
 * Create a textbox control
 */
control_t* control_create_textbox(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
) {
    control_t* control = control_create_basic(
        x, y, width, height, 
        flags | CONTROL_FLAG_CAN_FOCUS, 
        CONTROL_TYPE_TEXTBOX
    );
    if (!control) return NULL;
    
    // Copy the text
    strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
    control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    // Set text cursor position to end of text
    control->cursor_pos = strlen(control->text);
    
    return control;
}

/**
 * Create a checkbox control
 */
control_t* control_create_checkbox(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
) {
    control_t* control = control_create_basic(x, y, width, height, flags, CONTROL_TYPE_CHECKBOX);
    if (!control) return NULL;
    
    // Copy the text
    strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
    control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    // Initialize checkbox state
    control->state = 0; // unchecked
    
    return control;
}

/**
 * Create a custom control
 */
control_t* control_create_custom(
    int x, int y, int width, int height,
    uint32_t flags, 
    void (*render_func)(control_t*, int, int),
    void* user_data
) {
    control_t* control = control_create_basic(x, y, width, height, flags, CONTROL_TYPE_CUSTOM);
    if (!control) return NULL;
    
    // Set custom render function and user data
    control->render = render_func;
    control->user_data = user_data;
    
    return control;
}

/**
 * Create a progress bar control
 */
control_t* control_create_progress_bar(
    int x, int y, int width, int height,
    int min_value, int max_value, int current_value,
    uint32_t bar_color, uint32_t flags
) {
    control_t* control = control_create_basic(x, y, width, height, flags, CONTROL_TYPE_PROGRESS_BAR);
    if (!control) return NULL;
    
    // Initialize progress bar data
    control->data.progress.min_value = min_value;
    control->data.progress.max_value = max_value;
    control->data.progress.current_value = current_value;
    control->data.progress.bar_color = bar_color;
    
    return control;
}

/**
 * Create a list box control
 */
control_t* control_create_list_box(
    int x, int y, int width, int height,
    uint32_t flags
) {
    control_t* control = control_create_basic(
        x, y, width, height,
        flags | CONTROL_FLAG_CAN_FOCUS,
        CONTROL_TYPE_LIST_BOX
    );
    if (!control) return NULL;
    
    // Initialize list box data
    control->data.list.count = 0;
    control->data.list.selected_index = -1;
    control->data.list.scroll_offset = 0;
    control->data.list.on_selection_change = NULL;
    
    return control;
}

/**
 * Create a dropdown control
 */
control_t* control_create_dropdown(
    int x, int y, int width, int height,
    const char* default_text,
    uint32_t flags
) {
    control_t* control = control_create_basic(
        x, y, width, height,
        flags | CONTROL_FLAG_CAN_FOCUS,
        CONTROL_TYPE_DROPDOWN
    );
    if (!control) return NULL;
    
    // Initialize dropdown data
    control->data.list.count = 0;
    control->data.list.selected_index = -1;
    control->data.list.scroll_offset = 0;
    control->data.list.on_selection_change = NULL;
    
    // Set default text
    strncpy(control->text, default_text, CONTROL_TEXT_MAX_LENGTH - 1);
    control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    // Dropdowns start in closed state
    control->state = 0; // 0 = closed, 1 = open
    
    return control;
}

/**
 * Set progress bar value
 */
void control_progress_bar_set_value(control_t* control, int value) {
    if (!control || control->type != CONTROL_TYPE_PROGRESS_BAR) return;
    
    // Clamp the value to the progress bar range
    if (value < control->data.progress.min_value) {
        control->data.progress.current_value = control->data.progress.min_value;
    } else if (value > control->data.progress.max_value) {
        control->data.progress.current_value = control->data.progress.max_value;
    } else {
        control->data.progress.current_value = value;
    }
}

/**
 * Set control click handler
 */
void control_set_click_handler(control_t* control, void (*handler)(control_t*)) {
    if (!control) return;
    control->on_click = handler;
}

/**
 * Set control key handler
 */
void control_set_key_handler(control_t* control, void (*handler)(control_t*, int, int, int)) {
    if (!control) return;
    control->on_key = handler;
}

/**
 * Handle mouse events for a control
 */
void control_handle_mouse(control_t* control, int x, int y, int button, int press) {
    if (!control || !(control->flags & CONTROL_FLAG_ENABLED)) return;
    
    // Handle button press
    if (press && button) {
        // Focus the control if it can receive focus
        if (control->flags & CONTROL_FLAG_CAN_FOCUS) {
            focused_control = control;
        }
        
        // Set the control as pressed
        control->pressed = 1;
        
        // Handle based on control type
        switch (control->type) {
            case CONTROL_TYPE_BUTTON:
                // Buttons trigger on release, so just mark as pressed
                break;
                
            case CONTROL_TYPE_CHECKBOX:
                // Toggle checkbox state
                control->state = !control->state;
                if (control->on_click) {
                    control->on_click(control);
                }
                break;
                
            case CONTROL_TYPE_TEXTBOX:
                // Handle textbox click (move cursor)
                if (x >= 0 && x < control->width) {
                    // Calculate cursor position based on click position
                    // Simplistic implementation - assumes fixed width font
                    int char_width = 8; // assuming 8 pixels per character
                    int click_pos = x / char_width;
                    
                    // Clamp to text length
                    if (click_pos > strlen(control->text)) {
                        click_pos = strlen(control->text);
                    }
                    
                    control->cursor_pos = click_pos;
                }
                break;
                
            case CONTROL_TYPE_LIST_BOX:
                {
                    list_box_data_t* list = &control->data.list;
                    
                    // Calculate if click is in the scrollbar area
                    int item_height = 16; // Fixed item height
                    int visible_items = control->height / item_height;
                    int draw_scrollbar = list->count > visible_items;
                    int scrollbar_width = 12;
                    
                    // Check if click is on the scrollbar
                    if (draw_scrollbar && x > control->width - scrollbar_width) {
                        // Handle scrollbar click
                        int max_scroll = list->count - visible_items;
                        if (max_scroll <= 0) max_scroll = 1;
                        
                        // Calculate relative position in scrollbar
                        int scroll_pos = (y * max_scroll) / control->height;
                        if (scroll_pos < 0) scroll_pos = 0;
                        if (scroll_pos > max_scroll) scroll_pos = max_scroll;
                        
                        // Update scroll offset
                        list->scroll_offset = scroll_pos;
                    } 
                    else if (list->count > 0) {
                        // Calculate which item was clicked
                        int item_index = list->scroll_offset + (y / item_height);
                        
                        // Make sure it's a valid index
                        if (item_index >= 0 && item_index < list->count) {
                            // Update selected index
                            int old_selection = list->selected_index;
                            list->selected_index = item_index;
                            
                            // Call selection handler if defined and selection changed
                            if (list->on_selection_change && old_selection != item_index) {
                                list->on_selection_change(control, item_index);
                            }
                            
                            // Trigger click handler
                            if (control->on_click) {
                                control->on_click(control);
                            }
                        }
                    }
                }
                break;
                
            case CONTROL_TYPE_DROPDOWN:
                {
                    // Check if dropdown is open
                    if (control->state) {
                        // If click is outside the dropdown area, close it
                        if (y < 0 || y >= control->height) {
                            // Calculate dropdown height
                            list_box_data_t* list = &control->data.list;
                            int item_height = 16;
                            int dropdown_height = list->count * item_height + 2;
                            if (dropdown_height > 200) dropdown_height = 200; // max height
                            
                            // Check if click is within the dropdown list
                            if (y >= control->height && y < control->height + dropdown_height && x >= 0 && x < control->width) {
                                // Calculate which item was clicked
                                int item_index = (y - control->height) / item_height;
                                
                                // Make sure it's a valid index
                                if (item_index >= 0 && item_index < list->count) {
                                    // Update selected index
                                    int old_selection = list->selected_index;
                                    list->selected_index = item_index;
                                    
                                    // Update display text
                                    strncpy(control->text, list->items[item_index].text, CONTROL_TEXT_MAX_LENGTH - 1);
                                    control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
                                    
                                    // Call selection handler if defined and selection changed
                                    if (list->on_selection_change && old_selection != item_index) {
                                        list->on_selection_change(control, item_index);
                                    }
                                }
                            }
                            
                            // Close dropdown regardless of where clicked
                            control->state = 0;
                            
                            // Trigger click handler
                            if (control->on_click) {
                                control->on_click(control);
                            }
                        }
                    } 
                    else {
                        // Open the dropdown
                        control->state = 1;
                    }
                }
                break;
                
            case CONTROL_TYPE_CUSTOM:
                // Call the click handler if defined
                if (control->on_click) {
                    control->on_click(control);
                }
                break;
                
            default:
                break;
        }
    }
    // Handle button release
    else if (!press && control->pressed) {
        control->pressed = 0;
        
        // Handle based on control type
        switch (control->type) {
            case CONTROL_TYPE_BUTTON:
                // Trigger the click handler for buttons on release
                if (control->on_click) {
                    control->on_click(control);
                }
                break;
                
            default:
                break;
        }
    }
}

/**
 * Handle keyboard events for a control
 */
void control_handle_key(control_t* control, int key, int scancode, int press) {
    if (!control || !(control->flags & CONTROL_FLAG_ENABLED) || 
        !(control->flags & CONTROL_FLAG_CAN_FOCUS) || 
        control != focused_control) {
        return;
    }
    
    // Only process key press events for now
    if (!press) return;
    
    // Handle based on control type
    switch (control->type) {
        case CONTROL_TYPE_TEXTBOX:
            // Check for clipboard shortcuts (Ctrl+X, Ctrl+C, Ctrl+V)
            if (key == 'x' - 96 || key == 'X' - 64) {  // Ctrl+X (Cut)
                // Get the text from the control
                const char* text = control->text;
                uint32_t text_len = strlen(text);
                
                // Copy to clipboard
                clipboard_set_text(text, text_len);
                
                // Clear the text
                control->text[0] = '\0';
                control->cursor_pos = 0;
                
                return;
            }
            else if (key == 'c' - 96 || key == 'C' - 64) {  // Ctrl+C (Copy)
                // Get the text from the control
                const char* text = control->text;
                uint32_t text_len = strlen(text);
                
                // Copy to clipboard
                clipboard_set_text(text, text_len);
                
                return;
            }
            else if (key == 'v' - 96 || key == 'V' - 64) {  // Ctrl+V (Paste)
                // Get text from clipboard
                const char* clipboard_text = clipboard_get_text();
                if (!clipboard_text) {
                    return; // Nothing to paste
                }
                
                uint32_t clipboard_len = clipboard_get_text_length();
                uint32_t current_len = strlen(control->text);
                
                // Check if there's enough room
                if (current_len + clipboard_len >= CONTROL_TEXT_MAX_LENGTH - 1) {
                    // Not enough room, truncate the clipboard text
                    clipboard_len = CONTROL_TEXT_MAX_LENGTH - 1 - current_len;
                }
                
                if (clipboard_len > 0) {
                    // Make space at cursor position
                    memmove(
                        &control->text[control->cursor_pos + clipboard_len],
                        &control->text[control->cursor_pos],
                        current_len - control->cursor_pos + 1
                    );
                    
                    // Insert the clipboard text
                    memcpy(
                        &control->text[control->cursor_pos],
                        clipboard_text,
                        clipboard_len
                    );
                    
                    // Update cursor position
                    control->cursor_pos += clipboard_len;
                }
                
                return;
            }
            
            // Handle regular textbox input
            if (key >= 32 && key <= 126) { // Printable ASCII
                // Check if there's room in the buffer
                if (strlen(control->text) < CONTROL_TEXT_MAX_LENGTH - 1) {
                    // Make space by shifting text right from cursor position
                    memmove(
                        &control->text[control->cursor_pos + 1],
                        &control->text[control->cursor_pos],
                        strlen(&control->text[control->cursor_pos]) + 1
                    );
                    
                    // Insert the character
                    control->text[control->cursor_pos] = key;
                    control->cursor_pos++;
                }
            }
            else if (key == 8) { // Backspace
                // If there's a character before the cursor
                if (control->cursor_pos > 0) {
                    // Remove the character by shifting text left
                    memmove(
                        &control->text[control->cursor_pos - 1],
                        &control->text[control->cursor_pos],
                        strlen(&control->text[control->cursor_pos]) + 1
                    );
                    
                    control->cursor_pos--;
                }
            }
            else if (key == 127) { // Delete
                // If there's a character at/after the cursor
                if (control->cursor_pos < strlen(control->text)) {
                    // Remove the character by shifting text left
                    memmove(
                        &control->text[control->cursor_pos],
                        &control->text[control->cursor_pos + 1],
                        strlen(&control->text[control->cursor_pos + 1]) + 1
                    );
                }
            }
            else if (key == 0x1B) { // Escape - unfocus
                focused_control = NULL;
            }
            else if (key == 0x0D) { // Enter - trigger event
                if (control->on_key) {
                    control->on_key(control, key, scancode, press);
                }
            }
            else if (key == 0x25) { // Left Arrow
                if (control->cursor_pos > 0) {
                    control->cursor_pos--;
                }
            }
            else if (key == 0x27) { // Right Arrow
                if (control->cursor_pos < strlen(control->text)) {
                    control->cursor_pos++;
                }
            }
            else if (key == 0x24) { // Home
                control->cursor_pos = 0;
            }
            else if (key == 0x23) { // End
                control->cursor_pos = strlen(control->text);
            }
            break;
            
        default:
            // Call the key handler if defined
            if (control->on_key) {
                control->on_key(control, key, scancode, press);
            }
            break;
    }
}

/**
 * Render a control at the specified position
 */
void control_render(control_t* control, int x, int y) {
    if (!control || !(control->flags & CONTROL_FLAG_VISIBLE)) return;
    
    // Get theme colors from window manager
    extern uint32_t window_bg_color;
    extern uint32_t control_bg_color;
    extern uint32_t control_text_color;
    extern uint32_t control_border_color;
    
    // Use a custom render function if provided
    if (control->render) {
        control->render(control, x, y);
        return;
    }
    
    // Default rendering based on control type
    switch (control->type) {
        case CONTROL_TYPE_LABEL:
            // Labels just draw text
            graphics_draw_string(
                x + 2, y + (control->height - 8) / 2, // Center text vertically
                control->text,
                control->flags & CONTROL_FLAG_ENABLED ? control_text_color : 0x808080,
                1  // Scale
            );
            break;
            
        case CONTROL_TYPE_PROGRESS_BAR:
            {
                progress_bar_data_t* progress = &control->data.progress;
                
                // Calculate the progress percentage
                int range = progress->max_value - progress->min_value;
                int current = progress->current_value - progress->min_value;
                int fill_width = 0;
                
                if (range > 0) {
                    fill_width = (current * (control->width - 4)) / range;
                    if (fill_width < 0) fill_width = 0;
                    if (fill_width > control->width - 4) fill_width = control->width - 4;
                }
                
                // Draw progress bar background
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_bg_color,
                    1  // Filled
                );
                
                // Draw progress bar border
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_border_color,
                    0  // Not filled
                );
                
                // Draw progress bar fill
                if (fill_width > 0) {
                    graphics_draw_rect(
                        x + 2, y + 2,
                        fill_width, control->height - 4,
                        progress->bar_color,
                        1  // Filled
                    );
                }
                
                // Draw percentage text if there's space
                if (control->width >= 40) {
                    char percent_text[8];
                    int percent = range > 0 ? (current * 100) / range : 0;
                    sprintf(percent_text, "%d%%", percent);
                    
                    graphics_draw_string(
                        x + (control->width - strlen(percent_text) * 8) / 2,
                        y + (control->height - 8) / 2,
                        percent_text,
                        0xFFFFFF,
                        1  // Scale
                    );
                }
            }
            break;
            
        case CONTROL_TYPE_BUTTON:
            {
                // Draw button background with 3D effect
                uint32_t top_color = control->pressed ? 0x808080 : 0xFFFFFF;
                uint32_t bottom_color = control->pressed ? 0xFFFFFF : 0x808080;
                
                // Button background
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_bg_color,
                    1  // Filled
                );
                
                // Button border (3D effect)
                graphics_draw_line(x, y, x + control->width - 1, y, top_color); // Top
                graphics_draw_line(x, y, x, y + control->height - 1, top_color); // Left
                graphics_draw_line(x, y + control->height - 1, x + control->width - 1, y + control->height - 1, bottom_color); // Bottom
                graphics_draw_line(x + control->width - 1, y, x + control->width - 1, y + control->height - 1, bottom_color); // Right
                
                // Button text (shifted if pressed)
                int text_x = x + (control->width - strlen(control->text) * 8) / 2; // Center text horizontally
                int text_y = y + (control->height - 8) / 2; // Center text vertically
                
                if (control->pressed) {
                    text_x += 1;
                    text_y += 1;
                }
                
                graphics_draw_string(
                    text_x, text_y,
                    control->text,
                    control->flags & CONTROL_FLAG_ENABLED ? control_text_color : 0x808080,
                    1  // Scale
                );
            }
            break;
            
        case CONTROL_TYPE_CHECKBOX:
            {
                // Draw checkbox box
                int box_size = control->height - 4;
                if (box_size > 16) box_size = 16;
                
                graphics_draw_rect(
                    x + 2, y + (control->height - box_size) / 2,
                    box_size, box_size,
                    control_border_color,
                    0  // Not filled
                );
                
                // Draw checkbox background
                graphics_draw_rect(
                    x + 3, y + (control->height - box_size) / 2 + 1,
                    box_size - 2, box_size - 2,
                    control->flags & CONTROL_FLAG_ENABLED ? 0xFFFFFF : 0xE0E0E0,
                    1  // Filled
                );
                
                // Draw check mark if checked
                if (control->state) {
                    int check_x = x + 3;
                    int check_y = y + (control->height - box_size) / 2 + 1;
                    
                    graphics_draw_line(
                        check_x + 2, check_y + box_size / 2,
                        check_x + box_size / 2, check_y + box_size - 4,
                        control_text_color
                    );
                    graphics_draw_line(
                        check_x + box_size / 2, check_y + box_size - 4,
                        check_x + box_size - 3, check_y + 2,
                        control_text_color
                    );
                }
                
                // Draw checkbox text
                graphics_draw_string(
                    x + box_size + 6, y + (control->height - 8) / 2,
                    control->text,
                    control->flags & CONTROL_FLAG_ENABLED ? control_text_color : 0x808080,
                    1  // Scale
                );
            }
            break;
            
        case CONTROL_TYPE_TEXTBOX:
            {
                // Draw textbox background
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control->flags & CONTROL_FLAG_ENABLED ? 0xFFFFFF : 0xE0E0E0,
                    1  // Filled
                );
                
                // Draw textbox border
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_border_color,
                    0  // Not filled
                );
                
                // Draw text
                graphics_draw_string(
                    x + 3, y + (control->height - 8) / 2,
                    control->text,
                    control->flags & CONTROL_FLAG_ENABLED ? control_text_color : 0x808080,
                    1  // Scale
                );
                
                // Draw cursor if focused
                if (control == focused_control && (control->flags & CONTROL_FLAG_ENABLED)) {
                    int cursor_x = x + 3 + control->cursor_pos * 8;
                    int cursor_y = y + (control->height - 10) / 2;
                    
                    // Flash the cursor (very basic implementation)
                    static int cursor_flash = 0;
                    cursor_flash = (cursor_flash + 1) % 20;
                    
                    if (cursor_flash < 10) {
                        graphics_draw_line(
                            cursor_x, cursor_y,
                            cursor_x, cursor_y + 10,
                            control_text_color
                        );
                    }
                }
            }
            break;
            
        case CONTROL_TYPE_CUSTOM:
            // Custom controls should have their own render function
            // Here we just draw a placeholder
            graphics_draw_rect(
                x, y, control->width, control->height,
                control_bg_color,
                1  // Filled
            );
            graphics_draw_rect(
                x, y, control->width, control->height,
                control_border_color,
                0  // Not filled
            );
            break;
            
        case CONTROL_TYPE_LIST_BOX:
            {
                list_box_data_t* list = &control->data.list;
                
                // Draw list box background
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control->flags & CONTROL_FLAG_ENABLED ? 0xFFFFFF : 0xE0E0E0,
                    1  // Filled
                );
                
                // Draw list box border
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_border_color,
                    0  // Not filled
                );
                
                // Calculate how many items can fit in the visible area
                int item_height = 16; // Fixed item height
                int visible_items = control->height / item_height;
                if (visible_items <= 0) visible_items = 1;
                
                // Calculate scrollbar position if needed
                int draw_scrollbar = list->count > visible_items;
                int scrollbar_width = 12;
                int item_area_width = control->width - (draw_scrollbar ? scrollbar_width : 0);
                
                // Make sure scroll offset is within bounds
                if (list->scroll_offset > list->count - visible_items)
                    list->scroll_offset = list->count - visible_items;
                if (list->scroll_offset < 0)
                    list->scroll_offset = 0;
                
                // Draw visible items
                for (int i = list->scroll_offset; 
                     i < list->count && i < list->scroll_offset + visible_items; 
                     i++) {
                    
                    int item_y = y + (i - list->scroll_offset) * item_height;
                    
                    // Draw selection highlight if this is the selected item
                    if (i == list->selected_index) {
                        graphics_draw_rect(
                            x + 1, item_y,
                            item_area_width - 2, item_height,
                            control == focused_control ? 0x0078D7 : 0xD0D0D0,
                            1  // Filled
                        );
                        
                        // Draw selected item text
                        graphics_draw_string(
                            x + 4, item_y + (item_height - 8) / 2,
                            list->items[i].text,
                            0xFFFFFF, // white text for selected item
                            1  // Scale
                        );
                    } else {
                        // Draw item text
                        graphics_draw_string(
                            x + 4, item_y + (item_height - 8) / 2,
                            list->items[i].text,
                            control_text_color,
                            1  // Scale
                        );
                    }
                }
                
                // Draw scrollbar if needed
                if (draw_scrollbar) {
                    // Draw scrollbar background
                    graphics_draw_rect(
                        x + control->width - scrollbar_width, y,
                        scrollbar_width, control->height,
                        0xE0E0E0,
                        1  // Filled
                    );
                    
                    // Calculate thumb size and position
                    int thumb_height = (visible_items * control->height) / list->count;
                    if (thumb_height < 10) thumb_height = 10;
                    
                    int max_scroll = list->count - visible_items;
                    int thumb_y = 0;
                    if (max_scroll > 0) {
                        thumb_y = (list->scroll_offset * (control->height - thumb_height)) / max_scroll;
                    }
                    
                    // Draw scrollbar thumb
                    graphics_draw_rect(
                        x + control->width - scrollbar_width + 1, y + thumb_y,
                        scrollbar_width - 2, thumb_height,
                        0x808080,
                        1  // Filled
                    );
                }
            }
            break;
            
        case CONTROL_TYPE_DROPDOWN:
            {
                list_box_data_t* list = &control->data.list;
                
                // Draw dropdown button
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_bg_color,
                    1  // Filled
                );
                
                // Draw dropdown border
                graphics_draw_rect(
                    x, y, control->width, control->height,
                    control_border_color,
                    0  // Not filled
                );
                
                // Draw selected item text or placeholder
                graphics_draw_string(
                    x + 5, y + (control->height - 8) / 2,
                    control->text[0] != '\0' ? control->text : "<Select>",
                    control->flags & CONTROL_FLAG_ENABLED ? control_text_color : 0x808080,
                    1  // Scale
                );
                
                // Draw dropdown arrow
                int arrow_x = x + control->width - 16;
                int arrow_y = y + (control->height - 10) / 2;
                
                graphics_draw_line(arrow_x, arrow_y, arrow_x + 10, arrow_y, control_text_color);
                graphics_draw_line(arrow_x + 1, arrow_y + 1, arrow_x + 9, arrow_y + 1, control_text_color);
                graphics_draw_line(arrow_x + 2, arrow_y + 2, arrow_x + 8, arrow_y + 2, control_text_color);
                graphics_draw_line(arrow_x + 3, arrow_y + 3, arrow_x + 7, arrow_y + 3, control_text_color);
                graphics_draw_line(arrow_x + 4, arrow_y + 4, arrow_x + 6, arrow_y + 4, control_text_color);
                graphics_draw_line(arrow_x + 5, arrow_y + 5, arrow_x + 5, arrow_y + 5, control_text_color);
                
                // Draw dropdown list if open
                if (control->state) {
                    int item_height = 16; // Fixed item height
                    int dropdown_height = list->count * item_height + 2; // +2 for border
                    
                    if (dropdown_height < 10) dropdown_height = 10;
                    if (dropdown_height > 200) dropdown_height = 200; // Limit height
                    
                    // Draw dropdown list background
                    graphics_draw_rect(
                        x, y + control->height,
                        control->width, dropdown_height,
                        0xFFFFFF,
                        1  // Filled
                    );
                    
                    // Draw dropdown list border
                    graphics_draw_rect(
                        x, y + control->height,
                        control->width, dropdown_height,
                        control_border_color,
                        0  // Not filled
                    );
                    
                    // Calculate how many items can fit in the dropdown
                    int visible_items = dropdown_height / item_height;
                    if (visible_items > list->count) visible_items = list->count;
                    
                    // Draw visible items
                    for (int i = 0; i < visible_items; i++) {
                        int item_y = y + control->height + i * item_height;
                        
                        // Draw selection highlight if this is the selected item
                        if (i == list->selected_index) {
                            graphics_draw_rect(
                                x + 1, item_y,
                                control->width - 2, item_height,
                                0x0078D7,
                                1  // Filled
                            );
                            
                            // Draw selected item text
                            graphics_draw_string(
                                x + 5, item_y + (item_height - 8) / 2,
                                list->items[i].text,
                                0xFFFFFF,
                                1  // Scale
                            );
                        } else {
                            // Draw item text
                            graphics_draw_string(
                                x + 5, item_y + (item_height - 8) / 2,
                                list->items[i].text,
                                control_text_color,
                                1  // Scale
                            );
                        }
                    }
                }
            }
            break;
            
        default:
            // Unknown control type, just draw a placeholder
            graphics_draw_rect(
                x, y, control->width, control->height,
                0xC0C0C0,
                1  // Filled
            );
            graphics_draw_rect(
                x, y, control->width, control->height,
                0x800080,
                0  // Not filled
            );
            break;
    }
}

/**
 * Add an item to a list box or dropdown control
 */
int control_list_add_item(control_t* control, const char* text, void* user_data) {
    if (!control || (control->type != CONTROL_TYPE_LIST_BOX && control->type != CONTROL_TYPE_DROPDOWN)) {
        return -1;
    }
    
    // Check if the list is full
    if (control->data.list.count >= CONTROL_MAX_ITEMS) {
        return -1;
    }
    
    // Get the next available item slot
    int index = control->data.list.count;
    list_item_t* item = &control->data.list.items[index];
    
    // Copy the item text
    strncpy(item->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
    item->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    // Set the user data
    item->user_data = user_data;
    
    // Increment the item count
    control->data.list.count++;
    
    // If this is the first item, select it by default
    if (control->data.list.count == 1) {
        control->data.list.selected_index = 0;
        
        // For dropdowns, also update the text
        if (control->type == CONTROL_TYPE_DROPDOWN) {
            strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
            control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
        }
        
        // Call the selection change handler if defined
        if (control->data.list.on_selection_change) {
            control->data.list.on_selection_change(control, 0);
        }
    }
    
    return index;
}

/**
 * Remove an item from a list box or dropdown control
 */
int control_list_remove_item(control_t* control, int index) {
    if (!control || (control->type != CONTROL_TYPE_LIST_BOX && control->type != CONTROL_TYPE_DROPDOWN)) {
        return 0;
    }
    
    // Check if index is valid
    if (index < 0 || index >= control->data.list.count) {
        return 0;
    }
    
    // Shift all items after the removed one
    for (int i = index; i < control->data.list.count - 1; i++) {
        memcpy(&control->data.list.items[i], &control->data.list.items[i + 1], sizeof(list_item_t));
    }
    
    // Decrement the item count
    control->data.list.count--;
    
    // Adjust the selected index if necessary
    if (control->data.list.selected_index >= control->data.list.count) {
        control->data.list.selected_index = control->data.list.count > 0 ? control->data.list.count - 1 : -1;
    } else if (control->data.list.selected_index == index) {
        // Selection was removed, select the next item or -1 if none
        control->data.list.selected_index = index < control->data.list.count ? index : -1;
    }
    
    // For dropdowns, update the text if selection changed
    if (control->type == CONTROL_TYPE_DROPDOWN && control->data.list.selected_index >= 0) {
        const char* text = control->data.list.items[control->data.list.selected_index].text;
        strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
        control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    }
    
    // Call the selection change handler if defined
    if (control->data.list.on_selection_change) {
        control->data.list.on_selection_change(control, control->data.list.selected_index);
    }
    
    return 1;
}

/**
 * Clear all items from a list box or dropdown control
 */
void control_list_clear(control_t* control) {
    if (!control || (control->type != CONTROL_TYPE_LIST_BOX && control->type != CONTROL_TYPE_DROPDOWN)) {
        return;
    }
    
    // Reset the item count and selection
    control->data.list.count = 0;
    control->data.list.selected_index = -1;
    control->data.list.scroll_offset = 0;
    
    // For dropdowns, reset the text to empty
    if (control->type == CONTROL_TYPE_DROPDOWN) {
        control->text[0] = '\0';
    }
}

/**
 * Get the selected item index from a list box or dropdown
 */
int control_list_get_selected_index(control_t* control) {
    if (!control || (control->type != CONTROL_TYPE_LIST_BOX && control->type != CONTROL_TYPE_DROPDOWN)) {
        return -1;
    }
    
    return control->data.list.selected_index;
}

/**
 * Set the selected item in a list box or dropdown
 */
void control_list_set_selected_index(control_t* control, int index) {
    if (!control || (control->type != CONTROL_TYPE_LIST_BOX && control->type != CONTROL_TYPE_DROPDOWN)) {
        return;
    }
    
    // Validate index
    if (index < -1 || index >= control->data.list.count) {
        return;
    }
    
    // Only update if selection changed
    if (control->data.list.selected_index != index) {
        control->data.list.selected_index = index;
        
        // For dropdowns, update the text
        if (control->type == CONTROL_TYPE_DROPDOWN && index >= 0) {
            const char* text = control->data.list.items[index].text;
            strncpy(control->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
            control->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
        }
        
        // Call the selection change handler if defined
        if (control->data.list.on_selection_change) {
            control->data.list.on_selection_change(control, index);
        }
    }
}

/**
 * Set selection change handler for list box or dropdown
 */
void control_list_set_selection_handler(control_t* control, void (*handler)(control_t*, int)) {
    if (!control || (control->type != CONTROL_TYPE_LIST_BOX && control->type != CONTROL_TYPE_DROPDOWN)) {
        return;
    }
    
    control->data.list.on_selection_change = handler;
}