/**
 * @file window.h
 * @brief Window management system header for uintOS GUI
 */

#ifndef _WINDOW_H
#define _WINDOW_H

#include <stdint.h>

/* Forward declarations */
typedef struct window_s window_t;
typedef struct control_s control_t;
typedef struct event_s event_t;

/* Constants */
#define WINDOW_MAX_WINDOWS      16
#define WINDOW_MAX_CONTROLS     32
#define WINDOW_TITLE_MAX_LENGTH 64
#define WINDOW_BORDER_WIDTH     2
#define WINDOW_TITLEBAR_HEIGHT  20

/* Window flags */
#define WINDOW_FLAG_VISIBLE     (1 << 0)
#define WINDOW_FLAG_BORDER      (1 << 1)
#define WINDOW_FLAG_TITLEBAR    (1 << 2)
#define WINDOW_FLAG_CLOSABLE    (1 << 3)
#define WINDOW_FLAG_MOVABLE     (1 << 4)
#define WINDOW_FLAG_RESIZABLE   (1 << 5)
#define WINDOW_FLAG_MODAL       (1 << 6)

/* Control flags */
#define CONTROL_FLAG_VISIBLE    (1 << 0)
#define CONTROL_FLAG_ENABLED    (1 << 1)
#define CONTROL_FLAG_FOCUSED    (1 << 2)

/* Event types */
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

/* Mouse buttons */
#define MOUSE_BUTTON_LEFT       1
#define MOUSE_BUTTON_RIGHT      2
#define MOUSE_BUTTON_MIDDLE     3

/* Default colors */
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
    int x;                  /* Window X position */
    int y;                  /* Window Y position */
    int width;              /* Window width */
    int height;             /* Window height */
    int client_x;           /* Client area X offset from window origin */
    int client_y;           /* Client area Y offset from window origin */
    int client_width;       /* Client area width */
    int client_height;      /* Client area height */
    uint32_t flags;         /* Window flags */
    uint32_t bg_color;      /* Background color */
    char title[WINDOW_TITLE_MAX_LENGTH]; /* Window title */
    
    control_t* controls[WINDOW_MAX_CONTROLS]; /* Controls array */
    int control_count;      /* Number of controls */
    
    void (*handler)(window_t* window, event_t* event, void* user_data);
    void* user_data;        /* User data for event handler */
};

/* Window manager functions */
int window_manager_init(void);
void window_render_all(void);
void window_process_mouse(int x, int y, int button, int state);
void window_process_key(char key, int scancode, int state);

/* Window functions */
window_t* window_create(int x, int y, int width, int height, const char* title, uint32_t flags);
void window_destroy(window_t* window);
void window_set_position(window_t* window, int x, int y);
void window_set_size(window_t* window, int width, int height);
void window_set_title(window_t* window, const char* title);
void window_bring_to_front(window_t* window);
void window_add_control(window_t* window, control_t* control);
void window_set_handler(window_t* window, void (*handler)(window_t*, event_t*, void*), void* user_data);

#endif /* _WINDOW_H */