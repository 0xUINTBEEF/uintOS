/**
 * @file gui_demo.c
 * @brief GUI Demo Application
 */

#include "window.h"
#include "controls.h"
#include "layout.h"
#include "../logging/log.h"
#include "../keyboard.h"
#include <string.h>

/* Demo window handlers */
static void main_window_event_handler(window_t* window, event_t* event, void* user_data);
static void about_window_event_handler(window_t* window, event_t* event, void* user_data);
static void controls_window_event_handler(window_t* window, event_t* event, void* user_data);
static void layout_window_event_handler(window_t* window, event_t* event, void* user_data);
static void calc_window_event_handler(window_t* window, event_t* event, void* user_data);

/* Demo control handlers */
static void button_click_handler(control_t* button);
static void checkbox_change_handler(control_t* checkbox, int checked);
static void radio_select_handler(control_t* radio);
static void listbox_select_handler(control_t* listbox, int index);
static void button_calc_click(control_t* button);
static void update_progress_timer(void);

/* Demo windows */
static window_t* main_window = NULL;
static window_t* about_window = NULL;
static window_t* controls_window = NULL;
static window_t* layout_window = NULL;
static window_t* calc_window = NULL;

/* Demo status tracking */
static int show_about = 0;
static int theme_selection = 0;
static char calculator_display[32] = "0";
static int calculator_value = 0;
static int calculator_new_value = 1;
static char calculator_op = 0;
static control_t* progress1 = NULL;
static control_t* progress2 = NULL;
static int progress_value = 0;
static int tick_counter = 0;

/**
 * Initialize the GUI demo
 */
void gui_demo_init() {
    LOG(LOG_INFO, "Initializing GUI demo");
    
    // Initialize window manager
    if (window_manager_init() != 0) {
        LOG(LOG_ERROR, "Failed to initialize window manager");
        return;
    }
    
    // Create main demo window
    main_window = window_create(100, 80, 450, 350, "uintOS GUI Framework Demo", 
                              WINDOW_FLAG_VISIBLE | WINDOW_FLAG_MOVABLE);
    if (!main_window) {
        LOG(LOG_ERROR, "Failed to create main demo window");
        return;
    }
    
    // Set window event handler
    window_set_handler(main_window, main_window_event_handler, NULL);
    
    // Create a layout for the main window
    layout_t* main_layout = layout_create_flow(main_window, 0, 0, 
                                             main_window->client_width, 
                                             main_window->client_height,
                                             FLOW_VERTICAL, 15);
    
    // Create header
    control_t* header = label_create(0, 0, 400, 30, "Welcome to uintOS GUI Framework Demo", TEXT_ALIGN_CENTER);
    layout_flow_add_control(main_layout, header);
    
    // Create buttons in a horizontal flow layout
    layout_t* button_layout = layout_create_flow(main_window, 20, 50, 
                                              main_window->client_width - 40, 40,
                                              FLOW_HORIZONTAL, 15);
    
    layout_flow_set_alignment(button_layout, ALIGN_CENTER, ALIGN_MIDDLE);
    
    control_t* button1 = button_create(0, 0, 120, 30, "About uintOS", BUTTON_STYLE_NORMAL);
    button_set_click_handler(button1, button_click_handler);
    layout_flow_add_control(button_layout, button1);
    
    control_t* button2 = button_create(0, 0, 120, 30, "UI Controls", BUTTON_STYLE_3D);
    button_set_click_handler(button2, button_click_handler);
    layout_flow_add_control(button_layout, button2);
    
    control_t* button3 = button_create(0, 0, 120, 30, "Layout Demo", BUTTON_STYLE_FLAT);
    button_set_click_handler(button3, button_click_handler);
    layout_flow_add_control(button_layout, button3);
    
    layout_arrange(button_layout);
    
    // Create description text
    control_t* description = label_create(20, 100, 410, 120, 
                                        "This demo showcases the uintOS GUI Framework features:\n"
                                        "• Windowing system with window management\n"
                                        "• UI controls (buttons, labels, checkboxes, textboxes, etc.)\n"
                                        "• Layout management system for automatic control positioning\n"
                                        "• Event handling system for user interaction\n"
                                        "\nTry opening the different demo windows to explore the features!",
                                        TEXT_ALIGN_LEFT);
    window_add_control(main_window, description);
    
    // Create calculator button
    control_t* calc_button = button_create(20, 230, 120, 30, "Calculator", BUTTON_STYLE_3D);
    button_set_click_handler(calc_button, button_click_handler);
    window_add_control(main_window, calc_button);
    
    // Create exit button in bottom right
    control_t* exit_button = button_create(330, 280, 100, 30, "Exit Demo", BUTTON_STYLE_FLAT);
    button_set_click_handler(exit_button, button_click_handler);
    window_add_control(main_window, exit_button);
    
    LOG(LOG_INFO, "GUI demo initialized");
}

/**
 * Create the controls demo window
 */
static void create_controls_demo_window() {
    // Create controls window if not already created
    if (!controls_window) {
        controls_window = window_create(150, 100, 500, 380, "UI Controls Demo", 
                                     WINDOW_FLAG_VISIBLE | WINDOW_FLAG_MOVABLE);
        if (controls_window) {
            window_set_handler(controls_window, controls_window_event_handler, NULL);
            
            // Create a border layout for the main areas
            layout_t* border_layout = layout_create_border(controls_window, 0, 0, 
                                                        controls_window->client_width, 
                                                        controls_window->client_height,
                                                        5);
            
            // North region - title
            control_t* title = label_create(0, 0, 300, 30, "UI Control Gallery", TEXT_ALIGN_CENTER);
            layout_border_add_control(border_layout, title, BORDER_NORTH);
            
            // South region - status bar
            control_t* status = label_create(0, 0, 300, 20, "Status: Ready", TEXT_ALIGN_LEFT);
            layout_border_add_control(border_layout, status, BORDER_SOUTH);
            
            // West region - control selection
            layout_t* west_layout = layout_create_flow(controls_window, 0, 0, 150, 300,
                                                    FLOW_VERTICAL, 10);
            
            control_t* listbox_label = label_create(0, 0, 150, 20, "Control Categories:", TEXT_ALIGN_LEFT);
            layout_flow_add_control(west_layout, listbox_label);
            
            control_t* control_list = listbox_create(0, 0, 150, 220);
            listbox_add_item(control_list, "Basic Controls", NULL);
            listbox_add_item(control_list, "Input Controls", NULL);
            listbox_add_item(control_list, "Selection Controls", NULL);
            listbox_add_item(control_list, "Containers", NULL);
            listbox_add_item(control_list, "Indicators", NULL);
            listbox_set_selected(control_list, 0);
            listbox_set_select_handler(control_list, listbox_select_handler);
            layout_flow_add_control(west_layout, control_list);
            
            layout_arrange(west_layout);
            
            control_t* west_panel = controls_window->controls[controls_window->control_count - 1];
            layout_border_add_control(border_layout, west_panel, BORDER_WEST);
            
            // Center region - controls showcase
            layout_t* center_layout = layout_create_flow(controls_window, 0, 0, 330, 300,
                                                      FLOW_VERTICAL, 15);
            
            layout_flow_set_alignment(center_layout, ALIGN_LEFT, ALIGN_TOP);
            layout_flow_set_padding(center_layout, 10, 10, 10, 10);
            
            // Control groups
            control_t* group_label = label_create(0, 0, 300, 20, "Button Styles:", TEXT_ALIGN_LEFT);
            layout_flow_add_control(center_layout, group_label);
            
            // Button examples
            layout_t* button_layout = layout_create_flow(controls_window, 0, 0, 300, 40,
                                                      FLOW_HORIZONTAL, 10);
            
            control_t* normal_btn = button_create(0, 0, 90, 30, "Normal", BUTTON_STYLE_NORMAL);
            layout_flow_add_control(button_layout, normal_btn);
            
            control_t* flat_btn = button_create(0, 0, 90, 30, "Flat", BUTTON_STYLE_FLAT);
            layout_flow_add_control(button_layout, flat_btn);
            
            control_t* btn_3d = button_create(0, 0, 90, 30, "3D", BUTTON_STYLE_3D);
            layout_flow_add_control(button_layout, btn_3d);
            
            layout_arrange(button_layout);
            
            control_t* buttons_group = controls_window->controls[controls_window->control_count - 1];
            layout_flow_add_control(center_layout, buttons_group);
            
            // Checkbox examples
            control_t* check_label = label_create(0, 0, 300, 20, "Checkboxes:", TEXT_ALIGN_LEFT);
            layout_flow_add_control(center_layout, check_label);
            
            control_t* check1 = checkbox_create(0, 0, 300, 20, "Option 1", 1);
            layout_flow_add_control(center_layout, check1);
            
            control_t* check2 = checkbox_create(0, 0, 300, 20, "Option 2", 0);
            layout_flow_add_control(center_layout, check2);
            
            // Radio button examples
            control_t* radio_label = label_create(0, 0, 300, 20, "Radio Buttons:", TEXT_ALIGN_LEFT);
            layout_flow_add_control(center_layout, radio_label);
            
            control_t* radio1 = radiobutton_create(0, 0, 300, 20, "Light Theme", 1, 1);
            radiobutton_set_select_handler(radio1, radio_select_handler);
            layout_flow_add_control(center_layout, radio1);
            
            control_t* radio2 = radiobutton_create(0, 0, 300, 20, "Dark Theme", 1, 0);
            radiobutton_set_select_handler(radio2, radio_select_handler);
            layout_flow_add_control(center_layout, radio2);
            
            control_t* radio3 = radiobutton_create(0, 0, 300, 20, "High Contrast Theme", 1, 0);
            radiobutton_set_select_handler(radio3, radio_select_handler);
            layout_flow_add_control(center_layout, radio3);
            
            // Text input example
            control_t* text_label = label_create(0, 0, 300, 20, "Text Input:", TEXT_ALIGN_LEFT);
            layout_flow_add_control(center_layout, text_label);
            
            control_t* textbox = textbox_create(0, 0, 300, 24, 100, 0);
            textbox_set_text(textbox, "Sample text...");
            layout_flow_add_control(center_layout, textbox);
            
            // Progress bar examples
            control_t* progress_label = label_create(0, 0, 300, 20, "Progress Bars:", TEXT_ALIGN_LEFT);
            layout_flow_add_control(center_layout, progress_label);
            
            progress1 = progressbar_create(0, 0, 300, 20, 0, 100);
            progressbar_set_value(progress1, 30);
            layout_flow_add_control(center_layout, progress1);
            
            progress2 = progressbar_create(0, 0, 300, 20, 0, 100);
            progressbar_set_value(progress2, 70);
            layout_flow_add_control(center_layout, progress2);
            
            layout_arrange(center_layout);
            
            control_t* center_panel = controls_window->controls[controls_window->control_count - 1];
            layout_border_add_control(border_layout, center_panel, BORDER_CENTER);
            
            // Apply the border layout
            layout_arrange(border_layout);
            
            // Bring window to front
            window_bring_to_front(controls_window);
        }
    } else {
        // If already created, just bring to front
        window_bring_to_front(controls_window);
    }
}

/**
 * Create the layout demo window
 */
static void create_layout_demo_window() {
    // Create layout window if not already created
    if (!layout_window) {
        layout_window = window_create(200, 150, 450, 380, "Layout Management Demo", 
                                   WINDOW_FLAG_VISIBLE | WINDOW_FLAG_MOVABLE);
        if (layout_window) {
            window_set_handler(layout_window, layout_window_event_handler, NULL);
            
            // Create tabs for different layout types
            control_t* title = label_create(10, 10, 430, 30, 
                                         "Layout Manager Demonstrations", TEXT_ALIGN_CENTER);
            window_add_control(layout_window, title);
            
            // Flow layout demo
            control_t* flow_label = label_create(10, 50, 430, 20, 
                                             "Flow Layout - Horizontal", TEXT_ALIGN_LEFT);
            window_add_control(layout_window, flow_label);
            
            layout_t* flow_layout = layout_create_flow(layout_window, 10, 75, 430, 60,
                                                    FLOW_HORIZONTAL, 10);
            
            for (int i = 0; i < 5; i++) {
                char btn_text[20];
                sprintf(btn_text, "Button %d", i+1);
                control_t* btn = button_create(0, 0, 80, 30, btn_text, BUTTON_STYLE_NORMAL);
                layout_flow_add_control(flow_layout, btn);
            }
            
            layout_arrange(flow_layout);
            
            // Flow layout with alignment
            control_t* center_flow_label = label_create(10, 145, 430, 20, 
                                                    "Flow Layout - Centered", TEXT_ALIGN_LEFT);
            window_add_control(layout_window, center_flow_label);
            
            layout_t* center_flow = layout_create_flow(layout_window, 10, 170, 430, 60,
                                                    FLOW_HORIZONTAL, 10);
            layout_flow_set_alignment(center_flow, ALIGN_CENTER, ALIGN_MIDDLE);
            
            for (int i = 0; i < 3; i++) {
                char btn_text[20];
                sprintf(btn_text, "Button %d", i+1);
                control_t* btn = button_create(0, 0, 80, 30, btn_text, BUTTON_STYLE_FLAT);
                layout_flow_add_control(center_flow, btn);
            }
            
            layout_arrange(center_flow);
            
            // Grid layout demo
            control_t* grid_label = label_create(10, 240, 430, 20, 
                                             "Grid Layout (3x3)", TEXT_ALIGN_LEFT);
            window_add_control(layout_window, grid_label);
            
            layout_t* grid_layout = layout_create_grid(layout_window, 10, 265, 430, 90,
                                                    3, 3, 5, 5);
            
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    char cell_text[10];
                    sprintf(cell_text, "%d,%d", row, col);
                    control_t* cell = button_create(0, 0, 0, 0, cell_text, BUTTON_STYLE_FLAT);
                    layout_grid_add_control(grid_layout, cell, row, col, 1, 1);
                }
            }
            
            layout_arrange(grid_layout);
            
            // Bring window to front
            window_bring_to_front(layout_window);
        }
    } else {
        // If already created, just bring to front
        window_bring_to_front(layout_window);
    }
}

/**
 * Run the GUI demo
 */
void gui_demo_run() {
    LOG(LOG_INFO, "Running GUI demo main loop");
    
    // Simple event loop for demo
    int running = 1;
    while (running) {
        // Update progress bars periodically
        update_progress_timer();
        
        // Handle keyboard input
        if (keyboard_is_key_available()) {
            char key = keyboard_get_last_key();
            int scancode = keyboard_get_last_scancode();
            
            // Global exit key - ESC
            if (scancode == 0x01) {
                running = 0;
                break;
            }
            
            // Pass key to window manager
            window_process_key(key, scancode, 1);
            
            // Key up event
            window_process_key(key, scancode, 0);
        }
        
        // Handle mouse input (simulated for demo)
        // In a real OS, you would get mouse events from the mouse driver
        
        // Render all windows
        window_render_all();
    }
    
    // Clean up
    if (main_window) {
        window_destroy(main_window);
        main_window = NULL;
    }
    
    if (about_window) {
        window_destroy(about_window);
        about_window = NULL;
    }
    
    if (controls_window) {
        window_destroy(controls_window);
        controls_window = NULL;
    }
    
    if (layout_window) {
        window_destroy(layout_window);
        layout_window = NULL;
    }
    
    if (calc_window) {
        window_destroy(calc_window);
        calc_window = NULL;
    }
    
    LOG(LOG_INFO, "GUI demo finished");
}

/**
 * Update the progress bars in a timer-like fashion
 */
static void update_progress_timer() {
    // Only update if progress bars exist and visible
    if (!progress1 || !progress2 || !controls_window) {
        return;
    }
    
    // Slow down updates - only update every 50 ticks
    tick_counter++;
    if (tick_counter < 50) {
        return;
    }
    
    tick_counter = 0;
    
    // Update first progress bar (cycles 0-100)
    progress_value = (progress_value + 5) % 105;
    if (progress_value > 100) progress_value = 0;
    
    progressbar_set_value(progress1, progress_value);
    
    // Update second progress bar (back and forth)
    static int progress2_value = 70;
    static int direction = -1;
    
    progress2_value += 5 * direction;
    if (progress2_value <= 0 || progress2_value >= 100) {
        direction *= -1;
    }
    
    progressbar_set_value(progress2, progress2_value);
}

/**
 * Main window event handler
 */
static void main_window_event_handler(window_t* window, event_t* event, void* user_data) {
    if (!window || !event) return;
    
    switch (event->type) {
        case EVENT_WINDOW_CLOSE:
            // Handle window close event
            window_destroy(window);
            main_window = NULL;
            break;
            
        default:
            break;
    }
}

/**
 * Button click handler
 */
static void button_click_handler(control_t* button) {
    if (!button || !button->user_data) return;
    
    button_data_t* data = (button_data_t*)button->user_data;
    
    // Check which button was clicked
    if (strcmp(data->text, "About uintOS") == 0) {
        // Create about window if not already created
        if (!about_window) {
            about_window = window_create(150, 150, 320, 200, "About uintOS", 
                                      WINDOW_FLAG_VISIBLE | WINDOW_FLAG_MOVABLE);
            if (about_window) {
                window_set_handler(about_window, about_window_event_handler, NULL);
                
                // Add controls to about window
                control_t* label1 = label_create(20, 20, 280, 20, "uintOS - A Sample Operating System", TEXT_ALIGN_CENTER);
                window_add_control(about_window, label1);
                
                control_t* label2 = label_create(20, 50, 280, 60, 
                                           "uintOS is an educational operating system\n"
                                           "with HAL, memory management, multitasking,\n"
                                           "filesystem, networking and GUI features.", 
                                           TEXT_ALIGN_LEFT);
                window_add_control(about_window, label2);
                
                control_t* label3 = label_create(20, 120, 280, 20, "Version: 1.0.0 (May 2025)", TEXT_ALIGN_CENTER);
                window_add_control(about_window, label3);
                
                control_t* close_button = button_create(120, 150, 80, 30, "Close", BUTTON_STYLE_NORMAL);
                button_set_click_handler(close_button, button_click_handler);
                window_add_control(about_window, close_button);
                
                // Bring to front
                window_bring_to_front(about_window);
            }
        } else {
            // If already created, just bring to front
            window_bring_to_front(about_window);
        }
    } else if (strcmp(data->text, "UI Controls") == 0) {
        create_controls_demo_window();
    } else if (strcmp(data->text, "Layout Demo") == 0) {
        create_layout_demo_window();
    } else if (strcmp(data->text, "Calculator") == 0) {
        // Create calculator window if not already created
        if (!calc_window) {
            calc_window = window_create(200, 120, 240, 300, "Calculator", 
                                     WINDOW_FLAG_VISIBLE | WINDOW_FLAG_MOVABLE);
            if (calc_window) {
                window_set_handler(calc_window, calc_window_event_handler, NULL);
                
                // Reset calculator state
                strcpy(calculator_display, "0");
                calculator_value = 0;
                calculator_new_value = 1;
                calculator_op = 0;
                
                // Add display textbox
                control_t* display = textbox_create(20, 20, 200, 30, 30, 0);
                textbox_set_text(display, calculator_display);
                window_add_control(calc_window, display);
                
                // Add number buttons (0-9)
                int row, col;
                char btn_text[2] = "0";
                
                for (int i = 1; i <= 9; i++) {
                    row = 60 + ((i-1) / 3) * 40;
                    col = 20 + ((i-1) % 3) * 50;
                    
                    btn_text[0] = '0' + i;
                    control_t* num_btn = button_create(col, row, 40, 30, btn_text, BUTTON_STYLE_NORMAL);
                    button_set_click_handler(num_btn, button_calc_click);
                    window_add_control(calc_window, num_btn);
                }
                
                // Add zero button
                control_t* zero_btn = button_create(70, 180, 40, 30, "0", BUTTON_STYLE_NORMAL);
                button_set_click_handler(zero_btn, button_calc_click);
                window_add_control(calc_window, zero_btn);
                
                // Add operator buttons
                control_t* add_btn = button_create(170, 60, 40, 30, "+", BUTTON_STYLE_NORMAL);
                button_set_click_handler(add_btn, button_calc_click);
                window_add_control(calc_window, add_btn);
                
                control_t* sub_btn = button_create(170, 100, 40, 30, "-", BUTTON_STYLE_NORMAL);
                button_set_click_handler(sub_btn, button_calc_click);
                window_add_control(calc_window, sub_btn);
                
                control_t* mul_btn = button_create(170, 140, 40, 30, "*", BUTTON_STYLE_NORMAL);
                button_set_click_handler(mul_btn, button_calc_click);
                window_add_control(calc_window, mul_btn);
                
                control_t* div_btn = button_create(170, 180, 40, 30, "/", BUTTON_STYLE_NORMAL);
                button_set_click_handler(div_btn, button_calc_click);
                window_add_control(calc_window, div_btn);
                
                // Add equals button
                control_t* eq_btn = button_create(120, 180, 40, 30, "=", BUTTON_STYLE_NORMAL);
                button_set_click_handler(eq_btn, button_calc_click);
                window_add_control(calc_window, eq_btn);
                
                // Add clear button
                control_t* clr_btn = button_create(20, 180, 40, 30, "C", BUTTON_STYLE_NORMAL);
                button_set_click_handler(clr_btn, button_calc_click);
                window_add_control(calc_window, clr_btn);
                
                // Add close button
                control_t* close_btn = button_create(80, 240, 80, 30, "Close", BUTTON_STYLE_NORMAL);
                button_set_click_handler(close_btn, button_click_handler);
                window_add_control(calc_window, close_btn);
                
                // Bring to front
                window_bring_to_front(calc_window);
            }
        } else {
            // If already created, just bring to front
            window_bring_to_front(calc_window);
        }
    } else if (strcmp(data->text, "Close") == 0) {
        // Close button in About window
        if (button->parent == about_window) {
            window_destroy(about_window);
            about_window = NULL;
        }
        // Close button in Calculator window
        else if (button->parent == calc_window) {
            window_destroy(calc_window);
            calc_window = NULL;
        }
        // Close button in Controls window
        else if (button->parent == controls_window) {
            window_destroy(controls_window);
            controls_window = NULL;
            progress1 = NULL;
            progress2 = NULL;
        }
        // Close button in Layout window
        else if (button->parent == layout_window) {
            window_destroy(layout_window);
            layout_window = NULL;
        }
    } else if (strcmp(data->text, "Exit Demo") == 0) {
        // Exit button in main window
        if (about_window) {
            window_destroy(about_window);
            about_window = NULL;
        }
        if (controls_window) {
            window_destroy(controls_window);
            controls_window = NULL;
            progress1 = NULL;
            progress2 = NULL;
        }
        if (layout_window) {
            window_destroy(layout_window);
            layout_window = NULL;
        }
        if (calc_window) {
            window_destroy(calc_window);
            calc_window = NULL;
        }
        if (main_window) {
            window_destroy(main_window);
            main_window = NULL;
        }
    }
}

/**
 * Checkbox change handler
 */
static void checkbox_change_handler(control_t* checkbox, int checked) {
    // In a real application, this would enable/disable features
    LOG(LOG_INFO, "Checkbox changed: %s", checked ? "checked" : "unchecked");
}

/**
 * Radio button selection handler
 */
static void radio_select_handler(control_t* radio) {
    if (!radio || !radio->user_data) return;
    
    radiobutton_data_t* data = (radiobutton_data_t*)radio->user_data;
    
    if (strcmp(data->text, "Light Theme") == 0) {
        theme_selection = 0;
        LOG(LOG_INFO, "Theme changed to Light");
    } else if (strcmp(data->text, "Dark Theme") == 0) {
        theme_selection = 1;
        LOG(LOG_INFO, "Theme changed to Dark");
    } else if (strcmp(data->text, "High Contrast Theme") == 0) {
        theme_selection = 2;
        LOG(LOG_INFO, "Theme changed to High Contrast");
    }
}

/**
 * Listbox selection handler
 */
static void listbox_select_handler(control_t* listbox, int index) {
    LOG(LOG_INFO, "Listbox selection changed: item %d", index);
    
    // Find status label in controls window
    if (!controls_window) return;
    
    for (int i = 0; i < controls_window->control_count; i++) {
        control_t* control = controls_window->controls[i];
        if (control && control->render == label_render) {
            label_data_t* data = (label_data_t*)control->user_data;
            if (data && strncmp(data->text, "Status:", 7) == 0) {
                char new_status[64];
                sprintf(new_status, "Status: Selected category %d", index);
                label_set_text(control, new_status);
                break;
            }
        }
    }
}

/**
 * About window event handler
 */
static void about_window_event_handler(window_t* window, event_t* event, void* user_data) {
    if (!window || !event) return;
    
    switch (event->type) {
        case EVENT_WINDOW_CLOSE:
            // Handle window close event
            window_destroy(window);
            about_window = NULL;
            break;
            
        default:
            break;
    }
}

/**
 * Controls window event handler
 */
static void controls_window_event_handler(window_t* window, event_t* event, void* user_data) {
    if (!window || !event) return;
    
    switch (event->type) {
        case EVENT_WINDOW_CLOSE:
            // Handle window close event
            window_destroy(window);
            controls_window = NULL;
            progress1 = NULL;
            progress2 = NULL;
            break;
            
        default:
            break;
    }
}

/**
 * Layout window event handler
 */
static void layout_window_event_handler(window_t* window, event_t* event, void* user_data) {
    if (!window || !event) return;
    
    switch (event->type) {
        case EVENT_WINDOW_CLOSE:
            // Handle window close event
            window_destroy(window);
            layout_window = NULL;
            break;
            
        default:
            break;
    }
}

/**
 * Calculator window event handler
 */
static void calc_window_event_handler(window_t* window, event_t* event, void* user_data) {
    if (!window || !event) return;
    
    switch (event->type) {
        case EVENT_WINDOW_CLOSE:
            // Handle window close event
            window_destroy(window);
            calc_window = NULL;
            break;
            
        default:
            break;
    }
}

/**
 * Calculator button click handler
 */
static void button_calc_click(control_t* button) {
    if (!button || !button->user_data || !calc_window) return;
    
    button_data_t* data = (button_data_t*)button->user_data;
    
    // Find the display textbox
    control_t* display = NULL;
    for (int i = 0; i < calc_window->control_count; i++) {
        if (calc_window->controls[i]->render == textbox_render) {
            display = calc_window->controls[i];
            break;
        }
    }
    
    if (!display) return;
    
    // Handle different calculator buttons
    if (strcmp(data->text, "C") == 0) {
        // Clear
        strcpy(calculator_display, "0");
        calculator_value = 0;
        calculator_new_value = 1;
        calculator_op = 0;
    } else if (data->text[0] >= '0' && data->text[0] <= '9') {
        // Number button
        if (calculator_new_value) {
            calculator_display[0] = data->text[0];
            calculator_display[1] = '\0';
            calculator_new_value = 0;
        } else {
            // Don't allow leading zeros
            if (calculator_display[0] == '0' && calculator_display[1] == '\0') {
                calculator_display[0] = data->text[0];
            } else {
                // Append digit if there's room
                int len = strlen(calculator_display);
                if (len < 15) { // Limit to 15 digits
                    calculator_display[len] = data->text[0];
                    calculator_display[len + 1] = '\0';
                }
            }
        }
    } else if (strcmp(data->text, "+") == 0 || 
               strcmp(data->text, "-") == 0 ||
               strcmp(data->text, "*") == 0 ||
               strcmp(data->text, "/") == 0) {
        // Operator button
        // First convert display to value
        int display_value = 0;
        sscanf(calculator_display, "%d", &display_value);
        
        // If there was a previous operation pending, perform it
        if (!calculator_new_value && calculator_op) {
            switch (calculator_op) {
                case '+': calculator_value += display_value; break;
                case '-': calculator_value -= display_value; break;
                case '*': calculator_value *= display_value; break;
                case '/': 
                    if (display_value != 0) {
                        calculator_value /= display_value;
                    } else {
                        strcpy(calculator_display, "Error: Div by 0");
                        calculator_value = 0;
                        calculator_new_value = 1;
                        calculator_op = 0;
                        textbox_set_text(display, calculator_display);
                        return;
                    }
                    break;
            }
            
            // Update display with result
            sprintf(calculator_display, "%d", calculator_value);
        } else {
            // No pending operation, just save the current value
            calculator_value = display_value;
        }
        
        // Save the new operator and prepare for next input
        calculator_op = data->text[0];
        calculator_new_value = 1;
    } else if (strcmp(data->text, "=") == 0) {
        // Equals button
        if (calculator_op) {
            // Get the current display value
            int display_value = 0;
            sscanf(calculator_display, "%d", &display_value);
            
            // Perform the pending operation
            switch (calculator_op) {
                case '+': calculator_value += display_value; break;
                case '-': calculator_value -= display_value; break;
                case '*': calculator_value *= display_value; break;
                case '/': 
                    if (display_value != 0) {
                        calculator_value /= display_value;
                    } else {
                        strcpy(calculator_display, "Error: Div by 0");
                        calculator_value = 0;
                        calculator_new_value = 1;
                        calculator_op = 0;
                        textbox_set_text(display, calculator_display);
                        return;
                    }
                    break;
            }
            
            // Update display with result
            sprintf(calculator_display, "%d", calculator_value);
            
            // Clear operator and prepare for next input
            calculator_op = 0;
            calculator_new_value = 1;
        }
    }
    
    // Update display
    textbox_set_text(display, calculator_display);
}