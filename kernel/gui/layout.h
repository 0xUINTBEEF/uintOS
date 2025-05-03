/**
 * @file layout.h
 * @brief Layout management system for uintOS GUI
 */

#ifndef _LAYOUT_H
#define _LAYOUT_H

#include "window.h"
#include "controls.h"

/* Layout types */
typedef enum {
    LAYOUT_FLOW,             // Flow layout (horizontal or vertical)
    LAYOUT_GRID,             // Grid layout
    LAYOUT_BORDER,           // Border layout (North, South, East, West, Center)
    LAYOUT_ABSOLUTE          // Absolute positioning
} layout_type_t;

/* Flow layout direction */
typedef enum {
    FLOW_HORIZONTAL,         // Left to right
    FLOW_VERTICAL            // Top to bottom
} flow_direction_t;

/* Layout alignment */
typedef enum {
    ALIGN_LEFT,              // Left alignment
    ALIGN_CENTER,            // Center alignment
    ALIGN_RIGHT,             // Right alignment
    ALIGN_TOP,               // Top alignment
    ALIGN_MIDDLE,            // Middle alignment
    ALIGN_BOTTOM             // Bottom alignment
} layout_alignment_t;

/* Border layout regions */
typedef enum {
    BORDER_NORTH,            // Top region
    BORDER_SOUTH,            // Bottom region
    BORDER_EAST,             // Right region
    BORDER_WEST,             // Left region
    BORDER_CENTER            // Center region
} border_region_t;

/* Flow layout data */
typedef struct {
    flow_direction_t direction;     // Flow direction
    int spacing;                    // Spacing between controls
    layout_alignment_t h_align;     // Horizontal alignment
    layout_alignment_t v_align;     // Vertical alignment
    int padding_left;               // Left padding
    int padding_right;              // Right padding
    int padding_top;                // Top padding
    int padding_bottom;             // Bottom padding
} flow_layout_t;

/* Grid layout data */
typedef struct {
    int rows;                       // Number of rows
    int cols;                       // Number of columns
    int* row_heights;               // Row heights (NULL for uniform)
    int* col_widths;                // Column widths (NULL for uniform)
    int h_spacing;                  // Horizontal spacing between cells
    int v_spacing;                  // Vertical spacing between cells
    int padding;                    // Cell padding
} grid_layout_t;

/* Border layout data */
typedef struct {
    control_t* north;               // North control
    control_t* south;               // South control
    control_t* east;                // East control
    control_t* west;                // West control
    control_t* center;              // Center control
    int spacing;                    // Spacing between regions
} border_layout_t;

/* Generic layout container */
typedef struct {
    layout_type_t type;             // Layout type
    window_t* parent;               // Parent window
    int x, y;                       // Position
    int width, height;              // Size
    union {
        flow_layout_t flow;         // Flow layout data
        grid_layout_t grid;         // Grid layout data
        border_layout_t border;     // Border layout data
    } data;
    control_t** controls;           // Array of controls in layout
    int* control_constraints;       // Array of constraints (e.g., grid positions)
    int control_count;              // Number of controls
    int control_capacity;           // Capacity of control array
} layout_t;

/* Create a flow layout */
layout_t* layout_create_flow(window_t* parent, int x, int y, int width, int height,
                            flow_direction_t direction, int spacing);

/* Create a grid layout */
layout_t* layout_create_grid(window_t* parent, int x, int y, int width, int height,
                            int rows, int cols, int h_spacing, int v_spacing);

/* Create a border layout */
layout_t* layout_create_border(window_t* parent, int x, int y, int width, int height,
                              int spacing);

/* Set custom row heights for grid layout */
void layout_grid_set_row_heights(layout_t* layout, int* row_heights, int count);

/* Set custom column widths for grid layout */
void layout_grid_set_column_widths(layout_t* layout, int* col_widths, int count);

/* Set padding for flow layout */
void layout_flow_set_padding(layout_t* layout, int left, int right, int top, int bottom);

/* Set alignment for flow layout */
void layout_flow_set_alignment(layout_t* layout, layout_alignment_t h_align, layout_alignment_t v_align);

/* Add a control to a flow layout */
void layout_flow_add_control(layout_t* layout, control_t* control);

/* Add a control to a grid layout */
void layout_grid_add_control(layout_t* layout, control_t* control, int row, int col,
                            int row_span, int col_span);

/* Add a control to a border layout */
void layout_border_add_control(layout_t* layout, control_t* control, border_region_t region);

/* Arrange controls according to the layout */
void layout_arrange(layout_t* layout);

/* Destroy layout and free resources */
void layout_destroy(layout_t* layout);

#endif /* _LAYOUT_H */