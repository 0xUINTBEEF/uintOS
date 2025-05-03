/**
 * @file context_menu.h
 * @brief Context menu system for the GUI framework
 */
#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include <stdint.h>
#include "../window.h"
#include "../controls.h"

// Maximum number of menu items in a context menu
#define CONTEXT_MENU_MAX_ITEMS 16

// Menu item flags
#define MENU_ITEM_FLAG_ENABLED   (1 << 0)  // Item is enabled
#define MENU_ITEM_FLAG_SEPARATOR (1 << 1)  // Item is a separator
#define MENU_ITEM_FLAG_CHECKED   (1 << 2)  // Item is checked
#define MENU_ITEM_FLAG_SUBMENU   (1 << 3)  // Item has submenu

// Menu item structure
typedef struct menu_item {
    char text[CONTROL_TEXT_MAX_LENGTH];  // Menu item text
    uint32_t flags;                      // Item flags
    void (*on_select)(struct menu_item*); // Selection handler
    void* user_data;                     // User data for callback
    struct context_menu* submenu;        // Submenu (if any)
} menu_item_t;

// Context menu structure
typedef struct context_menu {
    window_t* window;                    // Menu window
    menu_item_t items[CONTEXT_MENU_MAX_ITEMS]; // Menu items
    int item_count;                      // Number of items
    int selected_index;                  // Currently selected item
    struct context_menu* parent;         // Parent menu (if submenu)
    void (*on_dismiss)(struct context_menu*); // Dismiss callback
} context_menu_t;

/**
 * Create a new context menu
 * 
 * @return Newly created context menu
 */
context_menu_t* context_menu_create(void);

/**
 * Add an item to a context menu
 * 
 * @param menu Context menu
 * @param text Menu item text
 * @param flags Item flags
 * @param on_select Selection callback
 * @param user_data User data for callback
 * @return Added menu item, or NULL on failure
 */
menu_item_t* context_menu_add_item(
    context_menu_t* menu,
    const char* text,
    uint32_t flags,
    void (*on_select)(menu_item_t*),
    void* user_data
);

/**
 * Add a separator to a context menu
 * 
 * @param menu Context menu
 * @return Added separator item, or NULL on failure
 */
menu_item_t* context_menu_add_separator(context_menu_t* menu);

/**
 * Add a submenu to a context menu item
 * 
 * @param parent_item Parent menu item
 * @return Newly created submenu, or NULL on failure
 */
context_menu_t* context_menu_add_submenu(menu_item_t* parent_item);

/**
 * Show a context menu at the specified position
 * 
 * @param menu Context menu
 * @param x X position
 * @param y Y position
 * @param on_dismiss Callback called when menu is dismissed
 */
void context_menu_show(
    context_menu_t* menu,
    int x, int y,
    void (*on_dismiss)(context_menu_t*)
);

/**
 * Dismiss a context menu
 * 
 * @param menu Context menu to dismiss
 */
void context_menu_dismiss(context_menu_t* menu);

/**
 * Destroy a context menu and free its resources
 * 
 * @param menu Context menu to destroy
 */
void context_menu_destroy(context_menu_t* menu);

/**
 * Get the selected menu item
 * 
 * @param menu Context menu
 * @return Selected menu item, or NULL if none
 */
menu_item_t* context_menu_get_selected(context_menu_t* menu);

/**
 * Set a menu item as checked/unchecked
 * 
 * @param item Menu item
 * @param checked Whether item should be checked
 */
void menu_item_set_checked(menu_item_t* item, int checked);

/**
 * Set a menu item as enabled/disabled
 * 
 * @param item Menu item
 * @param enabled Whether item should be enabled
 */
void menu_item_set_enabled(menu_item_t* item, int enabled);

#endif /* CONTEXT_MENU_H */