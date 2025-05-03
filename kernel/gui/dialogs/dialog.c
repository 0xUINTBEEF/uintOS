/**
 * @file dialog.c
 * @brief Dialog system implementation for GUI framework
 */
#include <string.h>
#include "dialog.h"
#include "../layout.h"
#include "../../logging/log.h"
#include "../../../hal/include/hal_memory.h"
#include "../../../hal/include/hal_io.h"

// Dialog private data structure
typedef struct {
    dialog_type_t type;             // Dialog type
    dialog_callback_t callback;     // Callback function
    dialog_update_t update_callback; // Progress update callback
    int result;                     // Dialog result
    void* user_data;                // User data for callbacks
    
    // Type-specific data
    union {
        // Input dialog data
        struct {
            control_t* input_field;
            int max_length;
        } input;
        
        // List dialog data
        struct {
            control_t* list_box;
        } list;
        
        // Progress dialog data
        struct {
            control_t* progress_bar;
            control_t* message_label;
        } progress;
    } data;
} dialog_data_t;

// Forward declarations for private functions
static void dialog_button_click(control_t* button);
static void on_dialog_window_close(window_t* window);
static void center_window_on_screen(window_t* window);

/**
 * Create a message dialog
 */
window_t* dialog_create_message(
    const char* title,
    const char* message,
    uint32_t flags,
    dialog_callback_t callback
) {
    // Get the screen dimensions to properly position and size the dialog
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        log_error("DIALOG", "Failed to get framebuffer for dialog creation");
        return NULL;
    }
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Calculate dialog dimensions
    int dialog_width = 350; // Default width
    int dialog_height = 150; // Default height
    
    // Calculate maximum message lines and adjust height if necessary
    int message_len = strlen(message);
    int chars_per_line = (dialog_width - 40) / 8; // Assuming 8 pixels per character
    int line_count = (message_len / chars_per_line) + 1;
    if (line_count > 1) {
        dialog_height += (line_count - 1) * 16; // 16 pixels per extra line
    }
    
    // Position dialog in the center of the screen if requested
    int dialog_x = (screen_width - dialog_width) / 2;
    int dialog_y = (screen_height - dialog_height) / 2;
    
    // Create window with appropriate flags
    uint32_t window_flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | 
                           WINDOW_FLAG_TITLEBAR | WINDOW_FLAG_MOVABLE;
    
    // Add close button unless specifically requested not to
    if (!(flags & DIALOG_FLAG_NO_CLOSE_BUTTON)) {
        window_flags |= WINDOW_FLAG_CLOSABLE;
    }
    
    // Make dialog modal if requested
    if (flags & DIALOG_FLAG_MODAL) {
        window_flags |= WINDOW_FLAG_MODAL;
    }
    
    // Create the dialog window
    window_t* dialog = window_create(
        dialog_x, dialog_y, dialog_width, dialog_height,
        title, window_flags
    );
    
    if (!dialog) {
        log_error("DIALOG", "Failed to create dialog window");
        return NULL;
    }
    
    // Create dialog private data
    dialog_data_t* dialog_data = hal_memory_alloc(sizeof(dialog_data_t));
    if (!dialog_data) {
        log_error("DIALOG", "Failed to allocate dialog data");
        window_destroy(dialog);
        return NULL;
    }
    
    // Initialize dialog data
    memset(dialog_data, 0, sizeof(dialog_data_t));
    dialog_data->type = DIALOG_TYPE_MESSAGE;
    dialog_data->callback = callback;
    dialog_data->result = DIALOG_RESULT_NONE;
    
    // Attach data to dialog
    dialog->user_data = dialog_data;
    
    // Use a flow layout for controls
    layout_t* layout = layout_create_flow(
        dialog, 10, 10, dialog_width - 20, dialog_height - 20,
        FLOW_VERTICAL, 10
    );
    
    if (!layout) {
        log_error("DIALOG", "Failed to create layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment
    layout_flow_set_padding(layout, 10, 10, 10, 10);
    layout_flow_set_alignment(layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Add message label
    control_t* message_label = control_create_label(
        0, 0, dialog_width - 40, line_count * 16,
        message, CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    layout_flow_add_control(layout, message_label);
    
    // Add OK button
    control_t* ok_button = control_create_button(
        0, 0, 80, 30,
        "OK", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(ok_button, dialog_button_click);
    ok_button->user_data = (void*)DIALOG_RESULT_OK;
    
    layout_flow_add_control(layout, ok_button);
    
    // Arrange controls according to the layout
    layout_arrange(layout);
    
    return dialog;
}

/**
 * Create a confirmation dialog with Yes/No buttons
 */
window_t* dialog_create_confirm(
    const char* title,
    const char* message,
    uint32_t flags,
    dialog_callback_t callback
) {
    // Get the screen dimensions to properly position and size the dialog
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        log_error("DIALOG", "Failed to get framebuffer for dialog creation");
        return NULL;
    }
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Calculate dialog dimensions
    int dialog_width = 350; // Default width
    int dialog_height = 160; // Default height for confirmation dialog
    
    // Calculate maximum message lines and adjust height if necessary
    int message_len = strlen(message);
    int chars_per_line = (dialog_width - 40) / 8; // Assuming 8 pixels per character
    int line_count = (message_len / chars_per_line) + 1;
    if (line_count > 1) {
        dialog_height += (line_count - 1) * 16; // 16 pixels per extra line
    }
    
    // Position dialog in the center of the screen if requested
    int dialog_x = (screen_width - dialog_width) / 2;
    int dialog_y = (screen_height - dialog_height) / 2;
    
    // Create window with appropriate flags
    uint32_t window_flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | 
                           WINDOW_FLAG_TITLEBAR | WINDOW_FLAG_MOVABLE;
    
    // Add close button unless specifically requested not to
    if (!(flags & DIALOG_FLAG_NO_CLOSE_BUTTON)) {
        window_flags |= WINDOW_FLAG_CLOSABLE;
    }
    
    // Make dialog modal if requested
    if (flags & DIALOG_FLAG_MODAL) {
        window_flags |= WINDOW_FLAG_MODAL;
    }
    
    // Create the dialog window
    window_t* dialog = window_create(
        dialog_x, dialog_y, dialog_width, dialog_height,
        title, window_flags
    );
    
    if (!dialog) {
        log_error("DIALOG", "Failed to create dialog window");
        return NULL;
    }
    
    // Create dialog private data
    dialog_data_t* dialog_data = hal_memory_alloc(sizeof(dialog_data_t));
    if (!dialog_data) {
        log_error("DIALOG", "Failed to allocate dialog data");
        window_destroy(dialog);
        return NULL;
    }
    
    // Initialize dialog data
    memset(dialog_data, 0, sizeof(dialog_data_t));
    dialog_data->type = DIALOG_TYPE_CONFIRM;
    dialog_data->callback = callback;
    dialog_data->result = DIALOG_RESULT_NONE;
    
    // Attach data to dialog
    dialog->user_data = dialog_data;
    
    // Use a flow layout for message
    layout_t* message_layout = layout_create_flow(
        dialog, 10, 10, dialog_width - 20, dialog_height - 70,
        FLOW_VERTICAL, 10
    );
    
    if (!message_layout) {
        log_error("DIALOG", "Failed to create message layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment
    layout_flow_set_padding(message_layout, 10, 10, 10, 10);
    layout_flow_set_alignment(message_layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Add message label
    control_t* message_label = control_create_label(
        0, 0, dialog_width - 40, line_count * 16,
        message, CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    layout_flow_add_control(message_layout, message_label);
    
    // Arrange message layout
    layout_arrange(message_layout);
    
    // Use a flow layout for buttons
    layout_t* button_layout = layout_create_flow(
        dialog, 10, dialog_height - 60, dialog_width - 20, 50,
        FLOW_HORIZONTAL, 20
    );
    
    if (!button_layout) {
        log_error("DIALOG", "Failed to create button layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment for buttons
    layout_flow_set_padding(button_layout, 10, 10, 10, 10);
    layout_flow_set_alignment(button_layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Add Yes button
    control_t* yes_button = control_create_button(
        0, 0, 80, 30,
        "Yes", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(yes_button, dialog_button_click);
    yes_button->user_data = (void*)DIALOG_RESULT_YES;
    
    layout_flow_add_control(button_layout, yes_button);
    
    // Add No button
    control_t* no_button = control_create_button(
        0, 0, 80, 30,
        "No", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(no_button, dialog_button_click);
    no_button->user_data = (void*)DIALOG_RESULT_NO;
    
    layout_flow_add_control(button_layout, no_button);
    
    // Arrange button layout
    layout_arrange(button_layout);
    
    return dialog;
}

/**
 * Create an input dialog with text field
 */
window_t* dialog_create_input(
    const char* title,
    const char* message,
    const char* default_text,
    int max_length,
    uint32_t flags,
    dialog_callback_t callback
) {
    // Get the screen dimensions to properly position and size the dialog
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        log_error("DIALOG", "Failed to get framebuffer for dialog creation");
        return NULL;
    }
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Calculate dialog dimensions
    int dialog_width = 400; // Default width for input dialog
    int dialog_height = 180; // Default height for input dialog
    
    // Position dialog in the center of the screen if requested
    int dialog_x = (screen_width - dialog_width) / 2;
    int dialog_y = (screen_height - dialog_height) / 2;
    
    // Create window with appropriate flags
    uint32_t window_flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | 
                           WINDOW_FLAG_TITLEBAR | WINDOW_FLAG_MOVABLE;
    
    // Add close button unless specifically requested not to
    if (!(flags & DIALOG_FLAG_NO_CLOSE_BUTTON)) {
        window_flags |= WINDOW_FLAG_CLOSABLE;
    }
    
    // Make dialog modal if requested
    if (flags & DIALOG_FLAG_MODAL) {
        window_flags |= WINDOW_FLAG_MODAL;
    }
    
    // Create the dialog window
    window_t* dialog = window_create(
        dialog_x, dialog_y, dialog_width, dialog_height,
        title, window_flags
    );
    
    if (!dialog) {
        log_error("DIALOG", "Failed to create dialog window");
        return NULL;
    }
    
    // Create dialog private data
    dialog_data_t* dialog_data = hal_memory_alloc(sizeof(dialog_data_t));
    if (!dialog_data) {
        log_error("DIALOG", "Failed to allocate dialog data");
        window_destroy(dialog);
        return NULL;
    }
    
    // Initialize dialog data
    memset(dialog_data, 0, sizeof(dialog_data_t));
    dialog_data->type = DIALOG_TYPE_INPUT;
    dialog_data->callback = callback;
    dialog_data->result = DIALOG_RESULT_NONE;
    dialog_data->data.input.max_length = max_length;
    
    // Attach data to dialog
    dialog->user_data = dialog_data;
    
    // Use a flow layout for content
    layout_t* layout = layout_create_flow(
        dialog, 10, 10, dialog_width - 20, dialog_height - 20,
        FLOW_VERTICAL, 10
    );
    
    if (!layout) {
        log_error("DIALOG", "Failed to create layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment
    layout_flow_set_padding(layout, 10, 10, 10, 10);
    layout_flow_set_alignment(layout, ALIGN_LEFT, ALIGN_MIDDLE);
    
    // Add message label
    control_t* message_label = control_create_label(
        0, 0, dialog_width - 40, 20,
        message, CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    layout_flow_add_control(layout, message_label);
    
    // Add input text box
    control_t* input_field = control_create_textbox(
        0, 0, dialog_width - 40, 30,
        default_text ? default_text : "",
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_CAN_FOCUS
    );
    
    layout_flow_add_control(layout, input_field);
    
    // Store input field reference in dialog data
    dialog_data->data.input.input_field = input_field;
    
    // Create a flow layout for buttons
    layout_t* button_layout = layout_create_flow(
        dialog, 10, dialog_height - 60, dialog_width - 20, 50,
        FLOW_HORIZONTAL, 20
    );
    
    if (!button_layout) {
        log_error("DIALOG", "Failed to create button layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment for buttons
    layout_flow_set_padding(button_layout, 10, 10, 10, 10);
    layout_flow_set_alignment(button_layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Add OK button
    control_t* ok_button = control_create_button(
        0, 0, 80, 30,
        "OK", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(ok_button, dialog_button_click);
    ok_button->user_data = (void*)DIALOG_RESULT_OK;
    
    layout_flow_add_control(button_layout, ok_button);
    
    // Add Cancel button
    control_t* cancel_button = control_create_button(
        0, 0, 80, 30,
        "Cancel", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(cancel_button, dialog_button_click);
    cancel_button->user_data = (void*)DIALOG_RESULT_CANCEL;
    
    layout_flow_add_control(button_layout, cancel_button);
    
    // Arrange layouts
    layout_arrange(layout);
    layout_arrange(button_layout);
    
    return dialog;
}

/**
 * Create a list selection dialog
 */
window_t* dialog_create_list(
    const char* title,
    const char* message,
    const char** items,
    int item_count,
    uint32_t flags,
    dialog_callback_t callback
) {
    // Get the screen dimensions to properly position and size the dialog
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        log_error("DIALOG", "Failed to get framebuffer for dialog creation");
        return NULL;
    }
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Calculate dialog dimensions
    int dialog_width = 400; // Default width for list dialog
    int dialog_height = 300; // Default height for list dialog
    
    // Position dialog in the center of the screen if requested
    int dialog_x = (screen_width - dialog_width) / 2;
    int dialog_y = (screen_height - dialog_height) / 2;
    
    // Create window with appropriate flags
    uint32_t window_flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | 
                           WINDOW_FLAG_TITLEBAR | WINDOW_FLAG_MOVABLE;
    
    // Add close button unless specifically requested not to
    if (!(flags & DIALOG_FLAG_NO_CLOSE_BUTTON)) {
        window_flags |= WINDOW_FLAG_CLOSABLE;
    }
    
    // Make dialog modal if requested
    if (flags & DIALOG_FLAG_MODAL) {
        window_flags |= WINDOW_FLAG_MODAL;
    }
    
    // Create the dialog window
    window_t* dialog = window_create(
        dialog_x, dialog_y, dialog_width, dialog_height,
        title, window_flags
    );
    
    if (!dialog) {
        log_error("DIALOG", "Failed to create dialog window");
        return NULL;
    }
    
    // Create dialog private data
    dialog_data_t* dialog_data = hal_memory_alloc(sizeof(dialog_data_t));
    if (!dialog_data) {
        log_error("DIALOG", "Failed to allocate dialog data");
        window_destroy(dialog);
        return NULL;
    }
    
    // Initialize dialog data
    memset(dialog_data, 0, sizeof(dialog_data_t));
    dialog_data->type = DIALOG_TYPE_LIST;
    dialog_data->callback = callback;
    dialog_data->result = DIALOG_RESULT_NONE;
    
    // Attach data to dialog
    dialog->user_data = dialog_data;
    
    // Use a flow layout for content
    layout_t* layout = layout_create_flow(
        dialog, 10, 10, dialog_width - 20, dialog_height - 20,
        FLOW_VERTICAL, 10
    );
    
    if (!layout) {
        log_error("DIALOG", "Failed to create layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment
    layout_flow_set_padding(layout, 10, 10, 10, 10);
    layout_flow_set_alignment(layout, ALIGN_LEFT, ALIGN_MIDDLE);
    
    // Add message label if provided
    if (message && strlen(message) > 0) {
        control_t* message_label = control_create_label(
            0, 0, dialog_width - 40, 20,
            message, CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
        );
        
        layout_flow_add_control(layout, message_label);
    }
    
    // Add list box
    control_t* list_box = control_create_list_box(
        0, 0, dialog_width - 40, dialog_height - 100,
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED | CONTROL_FLAG_CAN_FOCUS
    );
    
    // Store list box reference in dialog data
    dialog_data->data.list.list_box = list_box;
    
    // Add items to the list box
    for (int i = 0; i < item_count; i++) {
        control_list_add_item(list_box, items[i], NULL);
    }
    
    // Select the first item by default
    if (item_count > 0) {
        control_list_set_selected_index(list_box, 0);
    }
    
    layout_flow_add_control(layout, list_box);
    
    // Create a flow layout for buttons
    layout_t* button_layout = layout_create_flow(
        dialog, 10, dialog_height - 60, dialog_width - 20, 50,
        FLOW_HORIZONTAL, 20
    );
    
    if (!button_layout) {
        log_error("DIALOG", "Failed to create button layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment for buttons
    layout_flow_set_padding(button_layout, 10, 10, 10, 10);
    layout_flow_set_alignment(button_layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Add OK button
    control_t* ok_button = control_create_button(
        0, 0, 80, 30,
        "OK", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(ok_button, dialog_button_click);
    ok_button->user_data = (void*)DIALOG_RESULT_OK;
    
    layout_flow_add_control(button_layout, ok_button);
    
    // Add Cancel button
    control_t* cancel_button = control_create_button(
        0, 0, 80, 30,
        "Cancel", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    // Set button click handler
    control_set_click_handler(cancel_button, dialog_button_click);
    cancel_button->user_data = (void*)DIALOG_RESULT_CANCEL;
    
    layout_flow_add_control(button_layout, cancel_button);
    
    // Arrange layouts
    layout_arrange(layout);
    layout_arrange(button_layout);
    
    return dialog;
}

/**
 * Create a progress dialog
 */
window_t* dialog_create_progress(
    const char* title,
    const char* message,
    uint32_t flags,
    dialog_update_t update_callback,
    dialog_callback_t finish_callback
) {
    // Get the screen dimensions to properly position and size the dialog
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) {
        log_error("DIALOG", "Failed to get framebuffer for dialog creation");
        return NULL;
    }
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    // Calculate dialog dimensions
    int dialog_width = 350; // Default width
    int dialog_height = 150; // Default height
    
    // Position dialog in the center of the screen if requested
    int dialog_x = (screen_width - dialog_width) / 2;
    int dialog_y = (screen_height - dialog_height) / 2;
    
    // Create window with appropriate flags (progress dialogs often don't have close buttons)
    uint32_t window_flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | 
                           WINDOW_FLAG_TITLEBAR | WINDOW_FLAG_MOVABLE;
    
    // Add close button unless specifically requested not to
    if (!(flags & DIALOG_FLAG_NO_CLOSE_BUTTON)) {
        window_flags |= WINDOW_FLAG_CLOSABLE;
    }
    
    // Make dialog modal if requested
    if (flags & DIALOG_FLAG_MODAL) {
        window_flags |= WINDOW_FLAG_MODAL;
    }
    
    // Create the dialog window
    window_t* dialog = window_create(
        dialog_x, dialog_y, dialog_width, dialog_height,
        title, window_flags
    );
    
    if (!dialog) {
        log_error("DIALOG", "Failed to create dialog window");
        return NULL;
    }
    
    // Create dialog private data
    dialog_data_t* dialog_data = hal_memory_alloc(sizeof(dialog_data_t));
    if (!dialog_data) {
        log_error("DIALOG", "Failed to allocate dialog data");
        window_destroy(dialog);
        return NULL;
    }
    
    // Initialize dialog data
    memset(dialog_data, 0, sizeof(dialog_data_t));
    dialog_data->type = DIALOG_TYPE_PROGRESS;
    dialog_data->callback = finish_callback;
    dialog_data->update_callback = update_callback;
    dialog_data->result = DIALOG_RESULT_NONE;
    
    // Attach data to dialog
    dialog->user_data = dialog_data;
    
    // Use a flow layout for controls
    layout_t* layout = layout_create_flow(
        dialog, 10, 10, dialog_width - 20, dialog_height - 20,
        FLOW_VERTICAL, 10
    );
    
    if (!layout) {
        log_error("DIALOG", "Failed to create layout for dialog");
        hal_memory_free(dialog_data);
        window_destroy(dialog);
        return NULL;
    }
    
    // Set padding and alignment
    layout_flow_set_padding(layout, 10, 10, 10, 10);
    layout_flow_set_alignment(layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Add message label
    control_t* message_label = control_create_label(
        0, 0, dialog_width - 40, 20,
        message, CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    layout_flow_add_control(layout, message_label);
    dialog_data->data.progress.message_label = message_label;
    
    // Add progress bar
    control_t* progress_bar = control_create_progress_bar(
        0, 0, dialog_width - 40, 20,
        0, 100, 0, 0x0078D7, // Blue progress color
        CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
    );
    
    layout_flow_add_control(layout, progress_bar);
    dialog_data->data.progress.progress_bar = progress_bar;
    
    // Only add Cancel button if the dialog is not auto-close
    if (!(flags & DIALOG_FLAG_AUTO_CLOSE)) {
        // Add Cancel button
        control_t* cancel_button = control_create_button(
            0, 0, 80, 30,
            "Cancel", CONTROL_FLAG_VISIBLE | CONTROL_FLAG_ENABLED
        );
        
        // Set button click handler
        control_set_click_handler(cancel_button, dialog_button_click);
        cancel_button->user_data = (void*)DIALOG_RESULT_CANCEL;
        
        layout_flow_add_control(layout, cancel_button);
    }
    
    // Arrange controls according to the layout
    layout_arrange(layout);
    
    return dialog;
}

/**
 * Get input text from an input dialog
 */
const char* dialog_input_get_text(window_t* dialog) {
    if (!dialog || !dialog->user_data) {
        return NULL;
    }
    
    dialog_data_t* data = (dialog_data_t*)dialog->user_data;
    if (data->type != DIALOG_TYPE_INPUT || !data->data.input.input_field) {
        return NULL;
    }
    
    return data->data.input.input_field->text;
}

/**
 * Get selected index from a list dialog
 */
int dialog_list_get_selected_index(window_t* dialog) {
    if (!dialog || !dialog->user_data) {
        return -1;
    }
    
    dialog_data_t* data = (dialog_data_t*)dialog->user_data;
    if (data->type != DIALOG_TYPE_LIST || !data->data.list.list_box) {
        return -1;
    }
    
    return control_list_get_selected_index(data->data.list.list_box);
}

/**
 * Update progress dialog value
 */
void dialog_progress_update(
    window_t* dialog,
    int value,
    const char* message
) {
    if (!dialog || !dialog->user_data) {
        return;
    }
    
    dialog_data_t* data = (dialog_data_t*)dialog->user_data;
    if (data->type != DIALOG_TYPE_PROGRESS) {
        return;
    }
    
    // Update progress bar value
    if (data->data.progress.progress_bar) {
        control_progress_bar_set_value(data->data.progress.progress_bar, value);
    }
    
    // Update message if provided
    if (message && data->data.progress.message_label) {
        strncpy(data->data.progress.message_label->text, message, 
                CONTROL_TEXT_MAX_LENGTH - 1);
        data->data.progress.message_label->text[CONTROL_TEXT_MAX_LENGTH - 1] = '\0';
    }
}

/**
 * Close a dialog
 */
void dialog_close(window_t* dialog, int result) {
    if (!dialog || !dialog->user_data) {
        return;
    }
    
    dialog_data_t* data = (dialog_data_t*)dialog->user_data;
    data->result = result;
    
    // Call the callback if provided
    if (data->callback) {
        data->callback(dialog, result);
    }
    
    // Destroy the window
    window_destroy(dialog);
}

/* Private helper functions */

/**
 * Button click handler for dialog buttons
 */
static void dialog_button_click(control_t* button) {
    if (!button || !button->parent || !button->parent->user_data) {
        return;
    }
    
    window_t* dialog = button->parent;
    dialog_data_t* data = (dialog_data_t*)dialog->user_data;
    
    // Get the result from the button's user data
    int result = (int)button->user_data;
    
    // Store the result
    data->result = result;
    
    // Call the callback if provided
    if (data->callback) {
        data->callback(dialog, result);
    }
    
    // Check if dialog should auto-close
    uint32_t flags = dialog->flags;
    if (flags & DIALOG_FLAG_AUTO_CLOSE) {
        // Destroy the window
        window_destroy(dialog);
    }
}

/**
 * Center a window on the screen
 */
static void center_window_on_screen(window_t* window) {
    if (!window) return;
    
    extern framebuffer_t* graphics_get_framebuffer(void);
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb) return;
    
    int screen_width = fb->width;
    int screen_height = fb->height;
    
    window->x = (screen_width - window->width) / 2;
    window->y = (screen_height - window->height) / 2;
}