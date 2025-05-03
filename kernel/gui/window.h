/**
 * @file window.h
 * @brief Window management system for the GUI
 */
#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

// Forward declarations
typedef struct window_s window_t;
typedef struct control_s control_t;
typedef struct event_s event_t;

// Maximum length for window titles
#define WINDOW_TITLE_MAX_LENGTH 64

// Window flag bits
#define WINDOW_FLAG_VISIBLE    (1 << 0)
#define WINDOW_FLAG_BORDER     (1 << 1)
#define WINDOW_FLAG_TITLEBAR   (1 << 2)
#define WINDOW_FLAG_CLOSABLE   (1 << 3)
#define WINDOW_FLAG_MOVABLE    (1 << 4)
#define WINDOW_FLAG_RESIZABLE  (1 << 5)
#define WINDOW_FLAG_MODAL      (1 << 6)

// Window drag states
typedef enum {
    WINDOW_DRAG_NONE,
    WINDOW_DRAG_MOVE
} window_drag_state_t;

// Window resize states
typedef enum {
    WINDOW_RESIZE_NONE,
    WINDOW_RESIZE_RIGHT,
    WINDOW_RESIZE_BOTTOM,
    WINDOW_RESIZE_BOTTOM_RIGHT
} window_resize_state_t;

// Control flags
#define CONTROL_FLAG_VISIBLE    (1 << 0)
#define CONTROL_FLAG_ENABLED    (1 << 1)
#define CONTROL_FLAG_FOCUSED    (1 << 2)

// Event types
#define EVENT_MOUSE_DOWN        1
#define EVENT_MOUSE_UP          2
#define EVENT_MOUSE_MOVE        3
#define EVENT_KEY_DOWN          4
#define EVENT_KEY_UP            5
#define EVENT_WINDOW_CLOSE      6
#define EVENT_BUTTON_CLICK      7
#define EVENT_CHECKBOX_CHANGE   8
#define EVENT_TEXTBOX_CHANGE    9
#define EVENT_LISTBOX_SELECT    10

// Mouse buttons
#define MOUSE_BUTTON_LEFT       1
#define MOUSE_BUTTON_RIGHT      2
#define MOUSE_BUTTON_MIDDLE     3

// Default colors
#define WINDOW_COLOR_BACKGROUND      0xF0F0F0
#define WINDOW_COLOR_BORDER          0x000080
#define WINDOW_COLOR_TITLEBAR        0x000080
#define WINDOW_COLOR_TITLEBAR_TEXT   0xFFFFFF
#define WINDOW_COLOR_INACTIVE        0x808080
#define WINDOW_COLOR_CONTROL_BG      0xE0E0E0
#define WINDOW_COLOR_CONTROL_TEXT    0x000000
#define WINDOW_COLOR_CONTROL_BORDER  0x808080

/**
 * Mouse event data structure
 */
typedef struct {
    int x;          /* X coordinate relative to control/window */
    int y;          /* Y coordinate relative to control/window */
    int button;     /* Mouse button (MOUSE_BUTTON_*) */
} mouse_event_t;

/**
 * Keyboard event data structure
 */
typedef struct {
    char key;       /* ASCII character */
    int scancode;   /* Hardware scancode */
    int modifiers;  /* Modifier keys (Shift, Ctrl, Alt) */
} key_event_t;

/**
 * Event data union
 */
typedef union {
    mouse_event_t mouse;
    key_event_t key;
    int value;      /* Generic value for control events */
} event_data_t;

/**
 * Event structure
 */
struct event_s {
    int type;               /* Event type (EVENT_*) */
    event_data_t data;      /* Event data */
    window_t* window;       /* Window that received the event */
    control_t* control;     /* Control that received the event (if applicable) */
};

/**
 * Control structure
 */
struct control_s {
    int type;               /* Control type */
    int x;                  /* X position relative to window client area */
    int y;                  /* Y position relative to window client area */
    int width;              /* Control width */
    int height;             /* Control height */
    uint32_t flags;         /* Control flags */
    window_t* parent;       /* Parent window */
    void* data;             /* Type-specific data */
    
    /* Control methods */
    void (*render)(control_t* control);
    void (*handler)(control_t* control, event_t* event, void* user_data);
    void (*destroy)(control_t* control);
};

/**
 * Window structure
 */
struct window_s {
    int x, y;                    // Position
    int width, height;           // Size
    int min_width, min_height;   // Minimum size
    char title[WINDOW_TITLE_MAX_LENGTH];  // Window title
    uint32_t flags;              // Window flags
    window_drag_state_t drag_state;  // Dragging state
    window_resize_state_t resize_state;  // Resizing state
    int last_mouse_x, last_mouse_y;  // Last mouse position for dragging/resizing
    control_t* controls[32];     // Controls in this window
    int control_count;           // Number of controls
    uint32_t bg_color;           // Background color
    int client_x;                // Client area X offset from window origin
    int client_y;                // Client area Y offset from window origin
    int client_width;            // Client area width
    int client_height;           // Client area height
    void (*handler)(window_t* window, event_t* event, void* user_data);
    void* user_data;             // User data for event handler
};

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
);

/**
 * Create a new window
 */
window_t* window_create(
    int x, int y, int width, int height,
    const char* title,
    uint32_t flags
);

/**
 * Destroy a window
 */
void window_destroy(window_t* window);

/**
 * Add a control to a window
 */
int window_add_control(window_t* window, control_t* control);

/**
 * Remove a control from a window
 */
int window_remove_control(window_t* window, control_t* control);

/**
 * Bring a window to the front
 */
void window_bring_to_front(window_t* window);

/**
 * Render a window and its controls
 */
void window_render(window_t* window);

/**
 * Render all windows in the correct Z-order
 */
void window_render_all(void);

/**
 * Process a mouse event for the window manager
 */
void window_process_mouse(int x, int y, int button, int press);

/**
 * Process a keyboard event for the window manager
 */
void window_process_key(int key, int scancode, int press);

#endif /* WINDOW_H */