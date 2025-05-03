/**
 * @file context_menu.c
 * @brief Context menu system implementation
 */
#include <string.h>
#include "context_menu.h"
#include "../layout.h"
#include "../../logging/log.h"
#include "../../../hal/include/hal_memory.h"

// Static variables for menu management
static context_menu_t* active_menu = NULL;

// Forward declarations for private functions
static void context_menu_render(control_t* control, int x, int y);
static void context_menu_mouse_handler(control_t* control);
static void context_menu_window_close(window_t* window);
static void context_menu_dismiss_all(context_menu_t* menu);
static void context_menu_adjust_position(window_t* window, int x, int y);

/**
 * Create a new context menu
 */
context_menu_t* context_menu_create(void) {
    // Allocate memory for the menu
    context_menu_t* menu = (context_menu_t*)hal_memory_alloc(sizeof(context_menu_t));
    if (!menu) {
        log_error("MENU", "Failed to allocate memory for context menu");
        return NULL;
    }
    
    // Initialize menu structure
    memset(menu, 0, sizeof(context_menu_t));
    menu->selected_index = -1;
    
    return menu;
}

/**
 * Add an item to a context menu
 */
menu_item_t* context_menu_add_item(
    context_menu_t* menu,
    const char* text,
    uint32_t flags,
    void (*on_select)(menu_item_t*),
    void* user_data
) {
    if (!menu || menu->item_count >= CONTEXT_MENU_MAX_ITEMS) {
        return NULL;
    }
    
    // Get next available item slot
    menu_item_t* item = &menu->items[menu->item_count++];
    
    // Initialize item
    memset(item, 0, sizeof(menu_item_t));
    strncpy(item->text, text, CONTROL_TEXT_MAX_LENGTH - 1);
    item->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    
    item->flags = flags | MENU_ITEM_FLAG_ENABLED; // Default to enabled
    item->on_select = on_select;
    item->user_data = user_data;
    
    return item;
}

/**
 * Add a separator to a context menu
 */
menu_item_t* context_menu_add_separator(context_menu_t* menu) {
    if (!menu || menu->item_count >= CONTEXT_MENU_MAX_ITEMS) {
        return NULL;
    }
    
    // Get next available item slot
    menu_item_t* item = &menu->items[menu->item_count++];
    
    // Initialize as separator
    memset(item, 0, sizeof(menu_item_t));
    item->flags = MENU_ITEM_FLAG_SEPARATOR;
    
    return item;
}

/**
 * Add a submenu to a context menu item
 */
context_menu_t* context_menu_add_submenu(menu_item_t* parent_item) {
    if (!parent_item) {
        return NULL;
    }
    
    // Create a new submenu
    context_menu_t* submenu = context_menu_create();
    if (!submenu) {
        return NULL;
    }
    
    // Link parent and child
    parent_item->submenu = submenu;
    parent_item->flags |= MENU_ITEM_FLAG_SUBMENU;
    submenu->parent = (context_menu_t*)((char*)parent_item - offsetof(context_menu_t, items[0]));
    
    return submenu;
}

/**
 * Show a context menu at the specified position
 */
void context_menu_show(
    context_menu_t* menu,
    int x, int y,
    void (*on_dismiss)(context_menu_t*)
) {
    if (!menu || menu->window) {
        return; // Already showing or invalid menu
    }
    
    // If another menu is active and this isn't a submenu of the active menu,
    // dismiss the active menu first
    if (active_menu && menu->parent != active_menu) {
        context_menu_dismiss_all(active_menu);
    }
    
    // Calculate window dimensions
    int menu_width = 180;  // Default width
    int item_height = 24;  // Height per item
    int menu_height = menu->item_count * item_height + 4; // Add some padding
    
    // Create a window for the menu
    uint32_t window_flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER;
    menu->window = window_create(
        x, y, menu_width, menu_height,
        "", window_flags
    );
    
    if (!menu->window) {
        log_error("MENU", "Failed to create window for context menu");
        return;
    }
    
    // Adjust position to ensure menu is fully visible on screen
    context_menu_adjust_position(menu->window, x, y);
    
    // Create custom control for rendering menu items
    control_t* menu_control = control_create_custom(
        0, 0, menu_width, menu_height,
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED,
        context_menu_render,
        menu
    );
    
    // Set click handler
    control_set_click_handler(menu_control, context_menu_mouse_handler);
    
    // Add control to the window
    window_add_control(menu->window, menu_control);
    
    // Set dismiss callback
    menu->on_dismiss = on_dismiss;
    
    // Make this the active menu
    active_menu = menu;
}

/**
 * Dismiss a context menu
 */
void context_menu_dismiss(context_menu_t* menu) {
    if (!menu || !menu->window) {
        return;
    }
    
    // First dismiss any child submenu
    for (int i = 0; i < menu->item_count; i++) {
        menu_item_t* item = &menu->items[i];
        if ((item->flags & MENU_ITEM_FLAG_SUBMENU) && item->submenu && item->submenu->window) {
            context_menu_dismiss(item->submenu);
        }
    }
    
    // Call the dismiss callback if provided
    if (menu->on_dismiss) {
        menu->on_dismiss(menu);
    }
    
    // Destroy the window
    window_destroy(menu->window);
    menu->window = NULL;
    
    // Clear active menu reference if this is the active menu
    if (active_menu == menu) {
        active_menu = menu->parent;
    }
}

/**
 * Destroy a context menu and free its resources
 */
void context_menu_destroy(context_menu_t* menu) {
    if (!menu) {
        return;
    }
    
    // Dismiss if showing
    if (menu->window) {
        context_menu_dismiss(menu);
    }
    
    // Destroy any submenus
    for (int i = 0; i < menu->item_count; i++) {
        menu_item_t* item = &menu->items[i];
        if (item->submenu) {
            context_menu_destroy(item->submenu);
            item->submenu = NULL;
        }
    }
    
    // Free the memory
    hal_memory_free(menu);
}

/**
 * Get the selected menu item
 */
menu_item_t* context_menu_get_selected(context_menu_t* menu) {
    if (!menu || menu->selected_index < 0 || menu->selected_index >= menu->item_count) {
        return NULL;
    }
    
    return &menu->items[menu->selected_index];
}

/**
 * Set a menu item as checked/unchecked
 */
void menu_item_set_checked(menu_item_t* item, int checked) {
    if (!item) {
        return;
    }
    
    if (checked) {
        item->flags |= MENU_ITEM_FLAG_CHECKED;
    } else {
        item->flags &= ~MENU_ITEM_FLAG_CHECKED;
    }
}

/**
 * Set a menu item as enabled/disabled
 */
void menu_item_set_enabled(menu_item_t* item, int enabled) {
    if (!item) {
        return;
    }
    
    if (enabled) {
        item->flags |= MENU_ITEM_FLAG_ENABLED;
    } else {
        item->flags &= ~MENU_ITEM_FLAG_ENABLED;
    }
}

/* Private helper functions */

/**
 * Render the context menu
 */
static void context_menu_render(control_t* control, int x, int y) {
    if (!control || !control->user_data) {
        return;
    }
    
    context_menu_t* menu = (context_menu_t*)control->user_data;
    int item_height = 24;
    
    // Draw menu background
    graphics_draw_rect(
        x, y, control->width, control->height,
        0xF8F8F8, // Light background
        1  // Filled
    );
    
    // Draw border
    graphics_draw_rect(
        x, y, control->width, control->height,
        0xA0A0A0, // Gray border
        0  // Not filled
    );
    
    // Draw each menu item
    for (int i = 0; i < menu->item_count; i++) {
        menu_item_t* item = &menu->items[i];
        int item_y = y + i * item_height;
        
        if (item->flags & MENU_ITEM_FLAG_SEPARATOR) {
            // Draw separator (horizontal line)
            graphics_draw_line(
                x + 2, item_y + item_height / 2,
                x + control->width - 3, item_y + item_height / 2,
                0xD0D0D0 // Light gray
            );
        } else {
            // Draw selection highlight if this is the hovered item
            if (i == menu->selected_index) {
                graphics_draw_rect(
                    x + 1, item_y + 1,
                    control->width - 2, item_height - 2,
                    0xDAF0FF, // Light blue highlight
                    1  // Filled
                );
            }
            
            uint32_t text_color;
            
            // Determine text color based on enabled state
            if (item->flags & MENU_ITEM_FLAG_ENABLED) {
                text_color = 0x000000; // Black text for enabled items
            } else {
                text_color = 0xA0A0A0; // Gray text for disabled items
            }
            
            // Draw item text
            graphics_draw_string(
                x + 28, item_y + (item_height - 8) / 2,
                item->text,
                text_color,
                1
            );
            
            // Draw checkbox or radio button if item is checkable
            if (item->flags & MENU_ITEM_FLAG_CHECKED) {
                // Draw checkmark
                int check_x = x + 8;
                int check_y = item_y + (item_height - 10) / 2;
                
                graphics_draw_rect(
                    check_x, check_y,
                    10, 10,
                    0xA0A0A0, // Gray border
                    0  // Not filled
                );
                
                if (item->flags & MENU_ITEM_FLAG_ENABLED) {
                    // Draw checkmark inside the box
                    graphics_draw_line(
                        check_x + 2, check_y + 5,
                        check_x + 4, check_y + 7,
                        0x000000
                    );
                    graphics_draw_line(
                        check_x + 4, check_y + 7,
                        check_x + 8, check_y + 2,
                        0x000000
                    );
                }
            }
            
            // Draw arrow for submenu items
            if (item->flags & MENU_ITEM_FLAG_SUBMENU) {
                int arrow_x = x + control->width - 15;
                int arrow_y = item_y + (item_height / 2);
                
                // Draw right-pointing triangle
                for (int j = 0; j < 5; j++) {
                    graphics_draw_line(
                        arrow_x, arrow_y - j,
                        arrow_x + j, arrow_y,
                        text_color
                    );
                    graphics_draw_line(
                        arrow_x, arrow_y + j,
                        arrow_x + j, arrow_y,
                        text_color
                    );
                }
            }
        }
    }
}

/**
 * Handle mouse events for context menu
 */
static void context_menu_mouse_handler(control_t* control) {
    if (!control || !control->user_data) {
        return;
    }
    
    // This is called after a click on the menu control
    context_menu_t* menu = (context_menu_t*)control->user_data;
    int item_height = 24;
    
    // Calculate which item was clicked (y position relative to control)
    int rel_y = control->pressed_y;
    int item_index = rel_y / item_height;
    
    // Validate index
    if (item_index < 0 || item_index >= menu->item_count) {
        return;
    }
    
    menu_item_t* item = &menu->items[item_index];
    
    // Ignore clicks on separators or disabled items
    if ((item->flags & MENU_ITEM_FLAG_SEPARATOR) ||
        !(item->flags & MENU_ITEM_FLAG_ENABLED)) {
        return;
    }
    
    // If this item has a submenu, show it
    if ((item->flags & MENU_ITEM_FLAG_SUBMENU) && item->submenu) {
        // Calculate position for submenu (to the right of this menu)
        int submenu_x = menu->window->x + menu->window->width;
        int submenu_y = menu->window->y + item_index * item_height;
        
        // Show the submenu
        context_menu_show(item->submenu, submenu_x, submenu_y, NULL);
        return;
    }
    
    // If this is a regular item, call the handler and dismiss the menu
    if (item->on_select) {
        item->on_select(item);
    }
    
    // Dismiss all menus
    context_menu_dismiss_all(menu);
}

/**
 * Dismiss all menus in a chain (parent and child menus)
 */
static void context_menu_dismiss_all(context_menu_t* menu) {
    if (!menu) {
        return;
    }
    
    // Find the root menu
    context_menu_t* root = menu;
    while (root->parent) {
        root = root->parent;
    }
    
    // Dismiss the root menu (which will cascade to all children)
    context_menu_dismiss(root);
}

/**
 * Adjust position of menu window to ensure it's fully visible on screen
 */
static void context_menu_adjust_position(window_t* window, int x, int y) {
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !window) return;
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Adjust x position if menu would go off right edge
    if (x + window->width > screen_width) {
        window->x = screen_width - window->width;
    } else {
        window->x = x;
    }
    
    // Adjust y position if menu would go off bottom edge
    if (y + window->height > screen_height) {
        window->y = screen_height - window->height;
    } else {
        window->y = y;
    }
}