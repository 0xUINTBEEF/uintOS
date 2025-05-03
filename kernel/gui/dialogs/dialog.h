/**
 * @file dialog.h
 * @brief Dialog system for the GUI framework
 */
#ifndef DIALOG_H
#define DIALOG_H

#include <stdint.h>
#include "../window.h"
#include "../controls.h"

// Dialog result values
#define DIALOG_RESULT_NONE       0
#define DIALOG_RESULT_OK         1
#define DIALOG_RESULT_CANCEL     2
#define DIALOG_RESULT_YES        3
#define DIALOG_RESULT_NO         4
#define DIALOG_RESULT_RETRY      5
#define DIALOG_RESULT_ABORT      6

// Dialog types
typedef enum {
    DIALOG_TYPE_MESSAGE,      // Simple message with OK button
    DIALOG_TYPE_CONFIRM,      // Yes/No or OK/Cancel dialog
    DIALOG_TYPE_INPUT,        // Text input dialog
    DIALOG_TYPE_LIST,         // List selection dialog
    DIALOG_TYPE_PROGRESS,     // Progress dialog
    DIALOG_TYPE_CUSTOM        // Custom dialog with user-defined controls
} dialog_type_t;

// Dialog flags
#define DIALOG_FLAG_MODAL            (1 << 0)  // Modal dialog (blocks input to other windows)
#define DIALOG_FLAG_AUTO_CLOSE       (1 << 1)  // Auto-close when button clicked
#define DIALOG_FLAG_CENTERED         (1 << 2)  // Center on screen
#define DIALOG_FLAG_NO_CLOSE_BUTTON  (1 << 3)  // No close button in titlebar

// Dialog callback function types
typedef void (*dialog_callback_t)(window_t* dialog, int result);
typedef void (*dialog_update_t)(window_t* dialog, void* user_data);

/**
 * Create a message dialog
 * 
 * @param title Dialog title
 * @param message Message text to display
 * @param flags Dialog flags
 * @param callback Callback called when dialog is closed
 * @return Created dialog window
 */
window_t* dialog_create_message(
    const char* title,
    const char* message,
    uint32_t flags,
    dialog_callback_t callback
);

/**
 * Create a confirmation dialog with Yes/No buttons
 * 
 * @param title Dialog title
 * @param message Message text to display
 * @param flags Dialog flags
 * @param callback Callback called when dialog is closed
 * @return Created dialog window
 */
window_t* dialog_create_confirm(
    const char* title,
    const char* message,
    uint32_t flags,
    dialog_callback_t callback
);

/**
 * Create an input dialog with text field
 * 
 * @param title Dialog title
 * @param message Message/prompt text to display
 * @param default_text Default text for input field
 * @param max_length Maximum input length
 * @param flags Dialog flags
 * @param callback Callback called when dialog is closed
 * @return Created dialog window
 */
window_t* dialog_create_input(
    const char* title,
    const char* message,
    const char* default_text,
    int max_length,
    uint32_t flags,
    dialog_callback_t callback
);

/**
 * Create a list selection dialog
 * 
 * @param title Dialog title
 * @param message Message/prompt text to display
 * @param items Array of strings for list items
 * @param item_count Number of items in array
 * @param flags Dialog flags
 * @param callback Callback called when dialog is closed
 * @return Created dialog window
 */
window_t* dialog_create_list(
    const char* title,
    const char* message,
    const char** items,
    int item_count,
    uint32_t flags,
    dialog_callback_t callback
);

/**
 * Create a progress dialog
 * 
 * @param title Dialog title
 * @param message Message text to display
 * @param flags Dialog flags
 * @param update_callback Callback for updating progress
 * @param finish_callback Callback called when dialog is closed
 * @return Created dialog window
 */
window_t* dialog_create_progress(
    const char* title,
    const char* message,
    uint32_t flags,
    dialog_update_t update_callback,
    dialog_callback_t finish_callback
);

/**
 * Update progress dialog value
 * 
 * @param dialog Progress dialog window
 * @param value New progress value (0-100)
 * @param message Optional new message text (NULL to keep current)
 */
void dialog_progress_update(
    window_t* dialog,
    int value,
    const char* message
);

/**
 * Get input text from an input dialog
 * 
 * @param dialog Input dialog window
 * @return Input text string
 */
const char* dialog_input_get_text(window_t* dialog);

/**
 * Get selected index from a list dialog
 * 
 * @param dialog List dialog window
 * @return Selected item index or -1 if none selected
 */
int dialog_list_get_selected_index(window_t* dialog);

/**
 * Close a dialog
 * 
 * @param dialog Dialog window to close
 * @param result Result code to pass to callback
 */
void dialog_close(window_t* dialog, int result);

#endif /* DIALOG_H */