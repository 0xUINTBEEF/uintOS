/**
 * @file controls.h
 * @brief UI controls for the GUI
 */
#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdint.h>
#include "window.h"

// Maximum length for control text
#define CONTROL_TEXT_MAX_LENGTH 256

// Control flag bits
#define CONTROL_FLAG_VISIBLE    (1 << 0)
#define CONTROL_FLAG_ENABLED    (1 << 1)
#define CONTROL_FLAG_CAN_FOCUS  (1 << 2)

// Maximum number of items in a list box or dropdown
#define CONTROL_MAX_ITEMS 32

// Control types
typedef enum {
    CONTROL_TYPE_LABEL,
    CONTROL_TYPE_BUTTON,
    CONTROL_TYPE_CHECKBOX,
    CONTROL_TYPE_TEXTBOX,
    CONTROL_TYPE_CUSTOM,
    CONTROL_TYPE_PROGRESS_BAR,
    CONTROL_TYPE_LIST_BOX,
    CONTROL_TYPE_DROPDOWN
} control_type_t;

// List item structure
typedef struct {
    char text[CONTROL_TEXT_MAX_LENGTH];
    void* user_data;
} list_item_t;

// List box data structure
typedef struct {
    list_item_t items[CONTROL_MAX_ITEMS];
    int count;                       // Number of items
    int selected_index;              // Currently selected item index
    int scroll_offset;               // Scrolling offset for long lists
    void (*on_selection_change)(control_t*, int); // Selection change callback
} list_box_data_t;

// Progress bar data structure
typedef struct {
    int min_value;                   // Minimum value (usually 0)
    int max_value;                   // Maximum value (e.g. 100)
    int current_value;               // Current progress value
    uint32_t bar_color;              // Color of the progress bar
} progress_bar_data_t;

// Control structure
typedef struct control_s {
    int x, y;                        // Position relative to window
    int width, height;               // Size
    uint32_t flags;                  // Control flags
    control_type_t type;             // Control type
    window_t* parent;                // Parent window
    char text[CONTROL_TEXT_MAX_LENGTH];  // Text content
    int pressed;                     // Button press state
    int state;                       // State for toggle controls
    int cursor_pos;                  // Text cursor position for editable controls
    
    // Function pointers for custom controls
    void (*render)(struct control_s*, int, int);  // Custom render function
    void (*on_click)(struct control_s*);         // Click handler
    void (*on_key)(struct control_s*, int, int, int);  // Key handler
    
    // Control-specific data
    union {
        list_box_data_t list;        // List box or dropdown data
        progress_bar_data_t progress; // Progress bar data
    } data;
    
    void* user_data;                 // User-provided data
} control_t;

/**
 * Create a label control
 */
control_t* control_create_label(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
);

/**
 * Create a button control
 */
control_t* control_create_button(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
);

/**
 * Create a textbox control
 */
control_t* control_create_textbox(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
);

/**
 * Create a checkbox control
 */
control_t* control_create_checkbox(
    int x, int y, int width, int height,
    const char* text, uint32_t flags
);

/**
 * Create a custom control
 */
control_t* control_create_custom(
    int x, int y, int width, int height,
    uint32_t flags,
    void (*render_func)(control_t*, int, int),
    void* user_data
);

/**
 * Create a progress bar control
 */
control_t* control_create_progress_bar(
    int x, int y, int width, int height,
    int min_value, int max_value, int current_value,
    uint32_t bar_color, uint32_t flags
);

/**
 * Set progress bar value
 */
void control_progress_bar_set_value(control_t* control, int value);

/**
 * Create a list box control
 */
control_t* control_create_list_box(
    int x, int y, int width, int height,
    uint32_t flags
);

/**
 * Create a dropdown control
 */
control_t* control_create_dropdown(
    int x, int y, int width, int height,
    const char* default_text,
    uint32_t flags
);

/**
 * Add an item to a list box or dropdown control
 */
int control_list_add_item(control_t* control, const char* text, void* user_data);

/**
 * Remove an item from a list box or dropdown control
 */
int control_list_remove_item(control_t* control, int index);

/**
 * Clear all items from a list box or dropdown control
 */
void control_list_clear(control_t* control);

/**
 * Get the selected item index from a list box or dropdown
 */
int control_list_get_selected_index(control_t* control);

/**
 * Set the selected item in a list box or dropdown
 */
void control_list_set_selected_index(control_t* control, int index);

/**
 * Set selection change handler for list box or dropdown
 */
void control_list_set_selection_handler(
    control_t* control, 
    void (*handler)(control_t*, int)
);

/**
 * Set control click handler
 */
void control_set_click_handler(control_t* control, void (*handler)(control_t*));

/**
 * Set control key handler
 */
void control_set_key_handler(control_t* control, void (*handler)(control_t*, int, int, int));

/**
 * Handle mouse events for a control
 */
void control_handle_mouse(control_t* control, int x, int y, int button, int press);

/**
 * Handle keyboard events for a control
 */
void control_handle_key(control_t* control, int key, int scancode, int press);

/**
 * Render a control at the specified position
 */
void control_render(control_t* control, int x, int y);

#endif /* CONTROLS_H */