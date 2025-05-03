/**
 * @file controls.h
 * @brief UI Control definitions for uintOS GUI
 */

#ifndef _CONTROLS_H
#define _CONTROLS_H

#include "window.h"

/* Color definitions */
#define CONTROL_COLOR_BG        0xD0D0D0
#define CONTROL_COLOR_FG        0x000000
#define CONTROL_COLOR_BORDER    0x808080
#define CONTROL_COLOR_HIGHLIGHT 0x0000FF
#define CONTROL_COLOR_DISABLED  0x808080

/* Control flags */
#define CONTROL_FLAG_VISIBLE    (1 << 0)
#define CONTROL_FLAG_ENABLED    (1 << 1)
#define CONTROL_FLAG_FOCUSED    (1 << 2)
#define CONTROL_FLAG_TABSTOP    (1 << 3)
#define CONTROL_FLAG_BORDER     (1 << 4)
#define CONTROL_FLAG_TRANSPARENT (1 << 5)

/* Button styles */
#define BUTTON_STYLE_NORMAL     0
#define BUTTON_STYLE_FLAT       1
#define BUTTON_STYLE_3D         2

/* Button states */
#define BUTTON_STATE_NORMAL     0
#define BUTTON_STATE_HOVER      1
#define BUTTON_STATE_PRESSED    2

/* Control types */
typedef enum {
    CONTROL_BUTTON,
    CONTROL_LABEL,
    CONTROL_CHECKBOX,
    CONTROL_TEXTBOX,
    CONTROL_LISTBOX,
    CONTROL_RADIOBUTTON,
    CONTROL_PROGRESSBAR,
    CONTROL_SCROLLBAR,
    CONTROL_CANVAS,
    CONTROL_PANEL
} control_type_t;

/* Text alignment options */
typedef enum {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
} text_align_t;

/* Forward declarations */
struct _control;
typedef struct _control control_t;

/* Control structure */
struct _control {
    control_type_t type;        /* Control type */
    int x;                      /* X position relative to parent's client area */
    int y;                      /* Y position relative to parent's client area */
    int width;                  /* Control width */
    int height;                 /* Control height */
    uint32_t flags;             /* Control flags */
    uint32_t bg_color;          /* Background color */
    uint32_t fg_color;          /* Foreground color */
    window_t* parent;           /* Parent window */
    void* user_data;            /* Control-specific data */
    
    /* Function pointers */
    void (*render)(control_t* control);
    void (*handler)(control_t* control, event_t* event, void* user_data);
    void (*destroy)(control_t* control);
};

/* Button data structure */
typedef struct {
    char text[64];              /* Button text */
    text_align_t text_align;    /* Text alignment */
    int style;                  /* Button style */
    int state;                  /* Button state */
    void (*on_click)(control_t* button); /* Click handler */
} button_data_t;

/* Label data structure */
typedef struct {
    char text[256];             /* Label text */
    text_align_t text_align;    /* Text alignment */
} label_data_t;

/* Checkbox data structure */
typedef struct {
    char text[64];              /* Checkbox text */
    int checked;                /* Checked state */
    void (*on_change)(control_t* checkbox, int checked); /* Change handler */
} checkbox_data_t;

/* Radio button data structure */
typedef struct {
    char text[64];              /* Radio button text */
    int selected;               /* Selection state */
    int group_id;               /* Group ID - only one radio button in a group can be selected */
    void (*on_select)(control_t* radio); /* Selection handler */
} radiobutton_data_t;

/* Textbox data structure */
typedef struct {
    char* text;                 /* Textbox text buffer */
    int max_len;                /* Maximum text length */
    int text_len;               /* Current text length */
    int cursor_pos;             /* Cursor position */
    int selection_start;        /* Selection start position */
    int selection_end;          /* Selection end position */
    int is_multiline;           /* Whether textbox is multiline */
    int is_password;            /* Whether textbox is a password field */
    void (*on_change)(control_t* textbox); /* Change handler */
} textbox_data_t;

/* Listbox item structure */
typedef struct _listbox_item {
    char text[128];             /* Item text */
    void* data;                 /* Item associated data */
    struct _listbox_item* next; /* Next item in list */
} listbox_item_t;

/* Listbox data structure */
typedef struct {
    listbox_item_t* items;      /* List of items */
    int item_count;             /* Number of items */
    int selected_index;         /* Index of selected item (-1 if none) */
    int scroll_pos;             /* Current scroll position (for long lists) */
    int visible_items;          /* Number of visible items */
    int item_height;            /* Height of each item */
    void (*on_select)(control_t* listbox, int index); /* Selection handler */
} listbox_data_t;

/* Progress bar data structure */
typedef struct {
    int min;                    /* Minimum value */
    int max;                    /* Maximum value */
    int value;                  /* Current value */
    int style;                  /* Progress bar style */
} progressbar_data_t;

/* Button functions */
control_t* button_create(int x, int y, int width, int height, const char* text, int style);
void button_set_text(control_t* button, const char* text);
void button_set_click_handler(control_t* button, void (*on_click)(control_t*));

/* Label functions */
control_t* label_create(int x, int y, int width, int height, const char* text, text_align_t align);
void label_set_text(control_t* label, const char* text);

/* Checkbox functions */
control_t* checkbox_create(int x, int y, int width, int height, const char* text, int checked);
int checkbox_get_checked(control_t* checkbox);
void checkbox_set_checked(control_t* checkbox, int checked);
void checkbox_set_change_handler(control_t* checkbox, void (*on_change)(control_t*, int));

/* Radio button functions */
control_t* radiobutton_create(int x, int y, int width, int height, const char* text, int group_id, int selected);
int radiobutton_get_selected(control_t* radio);
void radiobutton_set_selected(control_t* radio, int selected);
void radiobutton_set_select_handler(control_t* radio, void (*on_select)(control_t*));

/* Textbox functions */
control_t* textbox_create(int x, int y, int width, int height, int max_len, int is_multiline);
const char* textbox_get_text(control_t* textbox);
void textbox_set_text(control_t* textbox, const char* text);
void textbox_set_password(control_t* textbox, int is_password);
void textbox_set_change_handler(control_t* textbox, void (*on_change)(control_t*));

/* Listbox functions */
control_t* listbox_create(int x, int y, int width, int height);
int listbox_add_item(control_t* listbox, const char* text, void* data);
void listbox_remove_item(control_t* listbox, int index);
void listbox_clear(control_t* listbox);
int listbox_get_selected(control_t* listbox);
void listbox_set_selected(control_t* listbox, int index);
void* listbox_get_item_data(control_t* listbox, int index);
void listbox_set_select_handler(control_t* listbox, void (*on_select)(control_t*, int));

/* Progress bar functions */
control_t* progressbar_create(int x, int y, int width, int height, int min, int max);
void progressbar_set_range(control_t* progressbar, int min, int max);
void progressbar_set_value(control_t* progressbar, int value);
int progressbar_get_value(control_t* progressbar);

#endif /* _CONTROLS_H */