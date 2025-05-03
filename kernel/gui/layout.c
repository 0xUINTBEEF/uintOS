/**
 * @file layout.c
 * @brief Layout management system implementation for uintOS GUI
 */

#include "layout.h"
#include "../logging/log.h"
#include "../../hal/include/hal_memory.h"
#include <string.h>

/* Initial capacity for control arrays */
#define INITIAL_CONTROL_CAPACITY 8

/* Helper functions for layouts */
static void resize_control_array(layout_t* layout);
static void flow_arrange(layout_t* layout);
static void grid_arrange(layout_t* layout);
static void border_arrange(layout_t* layout);

/**
 * Create a flow layout
 */
layout_t* layout_create_flow(window_t* parent, int x, int y, int width, int height,
                            flow_direction_t direction, int spacing) {
    if (!parent) {
        LOG(LOG_ERROR, "Cannot create layout without parent window");
        return NULL;
    }
    
    layout_t* layout = hal_memory_alloc(sizeof(layout_t));
    if (!layout) {
        LOG(LOG_ERROR, "Failed to allocate memory for flow layout");
        return NULL;
    }
    
    // Initialize layout structure
    memset(layout, 0, sizeof(layout_t));
    layout->type = LAYOUT_FLOW;
    layout->parent = parent;
    layout->x = x;
    layout->y = y;
    layout->width = width;
    layout->height = height;
    
    // Initialize flow layout data
    layout->data.flow.direction = direction;
    layout->data.flow.spacing = spacing;
    layout->data.flow.h_align = ALIGN_LEFT;
    layout->data.flow.v_align = ALIGN_TOP;
    layout->data.flow.padding_left = 5;
    layout->data.flow.padding_right = 5;
    layout->data.flow.padding_top = 5;
    layout->data.flow.padding_bottom = 5;
    
    // Initialize control array
    layout->control_capacity = INITIAL_CONTROL_CAPACITY;
    layout->controls = hal_memory_alloc(layout->control_capacity * sizeof(control_t*));
    layout->control_constraints = hal_memory_alloc(layout->control_capacity * sizeof(int));
    
    if (!layout->controls || !layout->control_constraints) {
        LOG(LOG_ERROR, "Failed to allocate control arrays for flow layout");
        layout_destroy(layout);
        return NULL;
    }
    
    memset(layout->controls, 0, layout->control_capacity * sizeof(control_t*));
    memset(layout->control_constraints, 0, layout->control_capacity * sizeof(int));
    
    return layout;
}

/**
 * Create a grid layout
 */
layout_t* layout_create_grid(window_t* parent, int x, int y, int width, int height,
                            int rows, int cols, int h_spacing, int v_spacing) {
    if (!parent || rows <= 0 || cols <= 0) {
        LOG(LOG_ERROR, "Invalid parameters for grid layout");
        return NULL;
    }
    
    layout_t* layout = hal_memory_alloc(sizeof(layout_t));
    if (!layout) {
        LOG(LOG_ERROR, "Failed to allocate memory for grid layout");
        return NULL;
    }
    
    // Initialize layout structure
    memset(layout, 0, sizeof(layout_t));
    layout->type = LAYOUT_GRID;
    layout->parent = parent;
    layout->x = x;
    layout->y = y;
    layout->width = width;
    layout->height = height;
    
    // Initialize grid layout data
    layout->data.grid.rows = rows;
    layout->data.grid.cols = cols;
    layout->data.grid.h_spacing = h_spacing;
    layout->data.grid.v_spacing = v_spacing;
    layout->data.grid.padding = 2;
    layout->data.grid.row_heights = NULL;
    layout->data.grid.col_widths = NULL;
    
    // Initialize control array - capacity is rows*cols
    layout->control_capacity = rows * cols;
    layout->controls = hal_memory_alloc(layout->control_capacity * sizeof(control_t*));
    layout->control_constraints = hal_memory_alloc(layout->control_capacity * sizeof(int));
    
    if (!layout->controls || !layout->control_constraints) {
        LOG(LOG_ERROR, "Failed to allocate control arrays for grid layout");
        layout_destroy(layout);
        return NULL;
    }
    
    memset(layout->controls, 0, layout->control_capacity * sizeof(control_t*));
    memset(layout->control_constraints, 0, layout->control_capacity * sizeof(int));
    
    return layout;
}

/**
 * Create a border layout
 */
layout_t* layout_create_border(window_t* parent, int x, int y, int width, int height,
                              int spacing) {
    if (!parent) {
        LOG(LOG_ERROR, "Cannot create layout without parent window");
        return NULL;
    }
    
    layout_t* layout = hal_memory_alloc(sizeof(layout_t));
    if (!layout) {
        LOG(LOG_ERROR, "Failed to allocate memory for border layout");
        return NULL;
    }
    
    // Initialize layout structure
    memset(layout, 0, sizeof(layout_t));
    layout->type = LAYOUT_BORDER;
    layout->parent = parent;
    layout->x = x;
    layout->y = y;
    layout->width = width;
    layout->height = height;
    
    // Initialize border layout data
    layout->data.border.spacing = spacing;
    layout->data.border.north = NULL;
    layout->data.border.south = NULL;
    layout->data.border.east = NULL;
    layout->data.border.west = NULL;
    layout->data.border.center = NULL;
    
    // Initialize control array - 5 regions maximum
    layout->control_capacity = 5;
    layout->controls = hal_memory_alloc(layout->control_capacity * sizeof(control_t*));
    layout->control_constraints = hal_memory_alloc(layout->control_capacity * sizeof(int));
    
    if (!layout->controls || !layout->control_constraints) {
        LOG(LOG_ERROR, "Failed to allocate control arrays for border layout");
        layout_destroy(layout);
        return NULL;
    }
    
    memset(layout->controls, 0, layout->control_capacity * sizeof(control_t*));
    memset(layout->control_constraints, 0, layout->control_capacity * sizeof(int));
    
    return layout;
}

/**
 * Set custom row heights for grid layout
 */
void layout_grid_set_row_heights(layout_t* layout, int* row_heights, int count) {
    if (!layout || layout->type != LAYOUT_GRID || !row_heights || count <= 0) {
        return;
    }
    
    if (count > layout->data.grid.rows) {
        count = layout->data.grid.rows;
    }
    
    // Free any existing row heights
    if (layout->data.grid.row_heights) {
        hal_memory_free(layout->data.grid.row_heights);
    }
    
    // Allocate and copy new row heights
    layout->data.grid.row_heights = hal_memory_alloc(count * sizeof(int));
    if (!layout->data.grid.row_heights) {
        LOG(LOG_ERROR, "Failed to allocate row heights");
        return;
    }
    
    memcpy(layout->data.grid.row_heights, row_heights, count * sizeof(int));
}

/**
 * Set custom column widths for grid layout
 */
void layout_grid_set_column_widths(layout_t* layout, int* col_widths, int count) {
    if (!layout || layout->type != LAYOUT_GRID || !col_widths || count <= 0) {
        return;
    }
    
    if (count > layout->data.grid.cols) {
        count = layout->data.grid.cols;
    }
    
    // Free any existing column widths
    if (layout->data.grid.col_widths) {
        hal_memory_free(layout->data.grid.col_widths);
    }
    
    // Allocate and copy new column widths
    layout->data.grid.col_widths = hal_memory_alloc(count * sizeof(int));
    if (!layout->data.grid.col_widths) {
        LOG(LOG_ERROR, "Failed to allocate column widths");
        return;
    }
    
    memcpy(layout->data.grid.col_widths, col_widths, count * sizeof(int));
}

/**
 * Set padding for flow layout
 */
void layout_flow_set_padding(layout_t* layout, int left, int right, int top, int bottom) {
    if (!layout || layout->type != LAYOUT_FLOW) {
        return;
    }
    
    layout->data.flow.padding_left = left;
    layout->data.flow.padding_right = right;
    layout->data.flow.padding_top = top;
    layout->data.flow.padding_bottom = bottom;
}

/**
 * Set alignment for flow layout
 */
void layout_flow_set_alignment(layout_t* layout, layout_alignment_t h_align, layout_alignment_t v_align) {
    if (!layout || layout->type != LAYOUT_FLOW) {
        return;
    }
    
    layout->data.flow.h_align = h_align;
    layout->data.flow.v_align = v_align;
}

/**
 * Add a control to a flow layout
 */
void layout_flow_add_control(layout_t* layout, control_t* control) {
    if (!layout || !control || layout->type != LAYOUT_FLOW) {
        return;
    }
    
    // Check if array needs to be resized
    if (layout->control_count >= layout->control_capacity) {
        resize_control_array(layout);
    }
    
    // Add control
    layout->controls[layout->control_count] = control;
    layout->control_count++;
    
    // Add to parent window if not already added
    if (control->parent != layout->parent) {
        window_add_control(layout->parent, control);
    }
}

/**
 * Add a control to a grid layout
 */
void layout_grid_add_control(layout_t* layout, control_t* control, int row, int col,
                           int row_span, int col_span) {
    if (!layout || !control || layout->type != LAYOUT_GRID) {
        return;
    }
    
    // Validate grid position
    if (row < 0 || row >= layout->data.grid.rows || 
        col < 0 || col >= layout->data.grid.cols) {
        LOG(LOG_ERROR, "Invalid grid position (%d,%d)", row, col);
        return;
    }
    
    // Validate spans
    if (row_span <= 0) row_span = 1;
    if (col_span <= 0) col_span = 1;
    
    if (row + row_span > layout->data.grid.rows) {
        row_span = layout->data.grid.rows - row;
    }
    
    if (col + col_span > layout->data.grid.cols) {
        col_span = layout->data.grid.cols - col;
    }
    
    // Check if array needs to be resized
    if (layout->control_count >= layout->control_capacity) {
        resize_control_array(layout);
    }
    
    // Add control
    layout->controls[layout->control_count] = control;
    
    // Pack grid position and span into constraint int:
    // bits 0-7: row
    // bits 8-15: column
    // bits 16-23: row span
    // bits 24-31: column span
    layout->control_constraints[layout->control_count] = 
        (row & 0xFF) | 
        ((col & 0xFF) << 8) | 
        ((row_span & 0xFF) << 16) | 
        ((col_span & 0xFF) << 24);
    
    layout->control_count++;
    
    // Add to parent window if not already added
    if (control->parent != layout->parent) {
        window_add_control(layout->parent, control);
    }
}

/**
 * Add a control to a border layout
 */
void layout_border_add_control(layout_t* layout, control_t* control, border_region_t region) {
    if (!layout || !control || layout->type != LAYOUT_BORDER) {
        return;
    }
    
    // Check if region already has a control
    control_t** region_ptr = NULL;
    switch (region) {
        case BORDER_NORTH:
            region_ptr = &layout->data.border.north;
            break;
        case BORDER_SOUTH:
            region_ptr = &layout->data.border.south;
            break;
        case BORDER_EAST:
            region_ptr = &layout->data.border.east;
            break;
        case BORDER_WEST:
            region_ptr = &layout->data.border.west;
            break;
        case BORDER_CENTER:
            region_ptr = &layout->data.border.center;
            break;
    }
    
    if (region_ptr && *region_ptr) {
        // Replace existing control
        for (int i = 0; i < layout->control_count; i++) {
            if (layout->controls[i] == *region_ptr) {
                layout->controls[i] = control;
                layout->control_constraints[i] = region;
                *region_ptr = control;
                
                // Add to parent window if not already added
                if (control->parent != layout->parent) {
                    window_add_control(layout->parent, control);
                }
                
                return;
            }
        }
    }
    
    // Add new control
    if (layout->control_count >= layout->control_capacity) {
        resize_control_array(layout);
    }
    
    layout->controls[layout->control_count] = control;
    layout->control_constraints[layout->control_count] = region;
    layout->control_count++;
    
    // Update region pointer
    if (region_ptr) {
        *region_ptr = control;
    }
    
    // Add to parent window if not already added
    if (control->parent != layout->parent) {
        window_add_control(layout->parent, control);
    }
}

/**
 * Arrange controls according to the layout
 */
void layout_arrange(layout_t* layout) {
    if (!layout) {
        return;
    }
    
    switch (layout->type) {
        case LAYOUT_FLOW:
            flow_arrange(layout);
            break;
        case LAYOUT_GRID:
            grid_arrange(layout);
            break;
        case LAYOUT_BORDER:
            border_arrange(layout);
            break;
        default:
            // No positioning needed for absolute layout
            break;
    }
}

/**
 * Destroy layout and free resources
 */
void layout_destroy(layout_t* layout) {
    if (!layout) {
        return;
    }
    
    // Free control arrays
    if (layout->controls) {
        hal_memory_free(layout->controls);
    }
    
    if (layout->control_constraints) {
        hal_memory_free(layout->control_constraints);
    }
    
    // Free grid layout specific resources
    if (layout->type == LAYOUT_GRID) {
        if (layout->data.grid.row_heights) {
            hal_memory_free(layout->data.grid.row_heights);
        }
        
        if (layout->data.grid.col_widths) {
            hal_memory_free(layout->data.grid.col_widths);
        }
    }
    
    // Free layout structure
    hal_memory_free(layout);
}

/* Flow layout implementation */
static void flow_arrange(layout_t* layout) {
    if (!layout || layout->type != LAYOUT_FLOW || layout->control_count == 0) {
        return;
    }
    
    flow_layout_t* flow = &layout->data.flow;
    int x, y;
    int max_width = 0;
    int max_height = 0;
    int total_width = 0;
    int total_height = 0;
    
    // Calculate starting position and total size
    x = layout->x + flow->padding_left;
    y = layout->y + flow->padding_top;
    
    // First pass: calculate total size
    if (flow->direction == FLOW_HORIZONTAL) {
        // Calculate total width and maximum height
        for (int i = 0; i < layout->control_count; i++) {
            control_t* control = layout->controls[i];
            if (control) {
                total_width += control->width;
                if (i < layout->control_count - 1) {
                    total_width += flow->spacing;
                }
                
                if (control->height > max_height) {
                    max_height = control->height;
                }
            }
        }
    } else {
        // Calculate total height and maximum width
        for (int i = 0; i < layout->control_count; i++) {
            control_t* control = layout->controls[i];
            if (control) {
                total_height += control->height;
                if (i < layout->control_count - 1) {
                    total_height += flow->spacing;
                }
                
                if (control->width > max_width) {
                    max_width = control->width;
                }
            }
        }
    }
    
    // Adjust starting position based on alignment
    int available_width = layout->width - flow->padding_left - flow->padding_right;
    int available_height = layout->height - flow->padding_top - flow->padding_bottom;
    
    if (flow->direction == FLOW_HORIZONTAL) {
        switch (flow->h_align) {
            case ALIGN_LEFT:
                // Already set correctly
                break;
            case ALIGN_CENTER:
                if (total_width < available_width) {
                    x += (available_width - total_width) / 2;
                }
                break;
            case ALIGN_RIGHT:
                if (total_width < available_width) {
                    x += available_width - total_width;
                }
                break;
        }
    } else {
        switch (flow->v_align) {
            case ALIGN_TOP:
                // Already set correctly
                break;
            case ALIGN_MIDDLE:
                if (total_height < available_height) {
                    y += (available_height - total_height) / 2;
                }
                break;
            case ALIGN_BOTTOM:
                if (total_height < available_height) {
                    y += available_height - total_height;
                }
                break;
        }
    }
    
    // Second pass: position controls
    int current_x = x;
    int current_y = y;
    
    for (int i = 0; i < layout->control_count; i++) {
        control_t* control = layout->controls[i];
        if (!control) continue;
        
        if (flow->direction == FLOW_HORIZONTAL) {
            // Position control vertically based on vertical alignment
            int control_y = current_y;
            switch (flow->v_align) {
                case ALIGN_TOP:
                    // Already set correctly
                    break;
                case ALIGN_MIDDLE:
                    control_y += (max_height - control->height) / 2;
                    break;
                case ALIGN_BOTTOM:
                    control_y += max_height - control->height;
                    break;
            }
            
            control->x = current_x;
            control->y = control_y;
            
            // Move to next position
            current_x += control->width + flow->spacing;
            
            // Wrap if needed (not implemented for simplicity)
        } else {
            // Position control horizontally based on horizontal alignment
            int control_x = current_x;
            switch (flow->h_align) {
                case ALIGN_LEFT:
                    // Already set correctly
                    break;
                case ALIGN_CENTER:
                    control_x += (max_width - control->width) / 2;
                    break;
                case ALIGN_RIGHT:
                    control_x += max_width - control->width;
                    break;
            }
            
            control->x = control_x;
            control->y = current_y;
            
            // Move to next position
            current_y += control->height + flow->spacing;
            
            // Wrap if needed (not implemented for simplicity)
        }
    }
}

/* Grid layout implementation */
static void grid_arrange(layout_t* layout) {
    if (!layout || layout->type != LAYOUT_GRID) {
        return;
    }
    
    grid_layout_t* grid = &layout->data.grid;
    int cell_width, cell_height;
    int* col_positions;
    int* row_positions;
    
    // Calculate cell size if not specified
    if (grid->row_heights == NULL) {
        // Uniform row heights
        cell_height = (layout->height - ((grid->rows - 1) * grid->v_spacing)) / grid->rows;
    }
    
    if (grid->col_widths == NULL) {
        // Uniform column widths
        cell_width = (layout->width - ((grid->cols - 1) * grid->h_spacing)) / grid->cols;
    }
    
    // Allocate arrays for row and column positions
    col_positions = hal_memory_alloc(grid->cols * sizeof(int));
    row_positions = hal_memory_alloc(grid->rows * sizeof(int));
    
    if (!col_positions || !row_positions) {
        LOG(LOG_ERROR, "Failed to allocate position arrays for grid layout");
        if (col_positions) hal_memory_free(col_positions);
        if (row_positions) hal_memory_free(row_positions);
        return;
    }
    
    // Calculate row positions
    int pos = layout->y;
    for (int i = 0; i < grid->rows; i++) {
        row_positions[i] = pos;
        
        if (grid->row_heights) {
            pos += grid->row_heights[i] + grid->v_spacing;
        } else {
            pos += cell_height + grid->v_spacing;
        }
    }
    
    // Calculate column positions
    pos = layout->x;
    for (int i = 0; i < grid->cols; i++) {
        col_positions[i] = pos;
        
        if (grid->col_widths) {
            pos += grid->col_widths[i] + grid->h_spacing;
        } else {
            pos += cell_width + grid->h_spacing;
        }
    }
    
    // Position controls
    for (int i = 0; i < layout->control_count; i++) {
        control_t* control = layout->controls[i];
        if (!control) continue;
        
        // Extract grid position from constraints:
        int row = layout->control_constraints[i] & 0xFF;
        int col = (layout->control_constraints[i] >> 8) & 0xFF;
        int row_span = (layout->control_constraints[i] >> 16) & 0xFF;
        int col_span = (layout->control_constraints[i] >> 24) & 0xFF;
        
        // Validate spans and positions
        if (row >= grid->rows || col >= grid->cols) {
            continue;
        }
        
        if (row + row_span > grid->rows) {
            row_span = grid->rows - row;
        }
        
        if (col + col_span > grid->cols) {
            col_span = grid->cols - col;
        }
        
        // Calculate control position
        int x = col_positions[col];
        int y = row_positions[row];
        
        // Calculate control size based on spans
        int width, height;
        
        if (grid->col_widths) {
            width = 0;
            for (int j = 0; j < col_span; j++) {
                if (col + j < grid->cols) {
                    width += grid->col_widths[col + j];
                    if (j < col_span - 1) {
                        width += grid->h_spacing;
                    }
                }
            }
        } else {
            width = col_span * cell_width + (col_span - 1) * grid->h_spacing;
        }
        
        if (grid->row_heights) {
            height = 0;
            for (int j = 0; j < row_span; j++) {
                if (row + j < grid->rows) {
                    height += grid->row_heights[row + j];
                    if (j < row_span - 1) {
                        height += grid->v_spacing;
                    }
                }
            }
        } else {
            height = row_span * cell_height + (row_span - 1) * grid->v_spacing;
        }
        
        // Apply padding within cell
        x += grid->padding;
        y += grid->padding;
        width -= 2 * grid->padding;
        height -= 2 * grid->padding;
        
        // Update control position
        control->x = x;
        control->y = y;
        
        // Optionally resize control to match cell
        // control->width = width;
        // control->height = height;
    }
    
    // Free temporary arrays
    hal_memory_free(col_positions);
    hal_memory_free(row_positions);
}

/* Border layout implementation */
static void border_arrange(layout_t* layout) {
    if (!layout || layout->type != LAYOUT_BORDER) {
        return;
    }
    
    border_layout_t* border = &layout->data.border;
    int spacing = border->spacing;
    
    // Calculate regions
    int north_height = 0;
    int south_height = 0;
    int east_width = 0;
    int west_width = 0;
    
    // Calculate sizes of edge regions
    if (border->north) {
        north_height = border->north->height;
    }
    
    if (border->south) {
        south_height = border->south->height;
    }
    
    if (border->east) {
        east_width = border->east->width;
    }
    
    if (border->west) {
        west_width = border->west->width;
    }
    
    // Position North region
    if (border->north) {
        border->north->x = layout->x;
        border->north->y = layout->y;
        border->north->width = layout->width;
    }
    
    // Position South region
    if (border->south) {
        border->south->x = layout->x;
        border->south->y = layout->y + layout->height - south_height;
        border->south->width = layout->width;
    }
    
    // Calculate center region height
    int center_height = layout->height - north_height - south_height;
    if (north_height > 0) center_height -= spacing;
    if (south_height > 0) center_height -= spacing;
    
    // Position West region
    if (border->west) {
        border->west->x = layout->x;
        border->west->y = layout->y + north_height;
        if (north_height > 0) border->west->y += spacing;
        border->west->height = center_height;
    }
    
    // Position East region
    if (border->east) {
        border->east->x = layout->x + layout->width - east_width;
        border->east->y = layout->y + north_height;
        if (north_height > 0) border->east->y += spacing;
        border->east->height = center_height;
    }
    
    // Position Center region
    if (border->center) {
        border->center->x = layout->x + west_width;
        border->center->y = layout->y + north_height;
        if (west_width > 0) border->center->x += spacing;
        if (north_height > 0) border->center->y += spacing;
        
        border->center->width = layout->width - west_width - east_width;
        if (west_width > 0) border->center->width -= spacing;
        if (east_width > 0) border->center->width -= spacing;
        
        border->center->height = center_height;
    }
}

/* Helper function to resize control array */
static void resize_control_array(layout_t* layout) {
    if (!layout) return;
    
    // Double the capacity
    int new_capacity = layout->control_capacity * 2;
    
    // Allocate new arrays
    control_t** new_controls = hal_memory_alloc(new_capacity * sizeof(control_t*));
    int* new_constraints = hal_memory_alloc(new_capacity * sizeof(int));
    
    if (!new_controls || !new_constraints) {
        LOG(LOG_ERROR, "Failed to resize control arrays");
        if (new_controls) hal_memory_free(new_controls);
        if (new_constraints) hal_memory_free(new_constraints);
        return;
    }
    
    // Copy data to new arrays
    memcpy(new_controls, layout->controls, layout->control_capacity * sizeof(control_t*));
    memcpy(new_constraints, layout->control_constraints, layout->control_capacity * sizeof(int));
    
    // Zero out new portion
    memset(new_controls + layout->control_capacity, 0, layout->control_capacity * sizeof(control_t*));
    memset(new_constraints + layout->control_capacity, 0, layout->control_capacity * sizeof(int));
    
    // Free old arrays
    hal_memory_free(layout->controls);
    hal_memory_free(layout->control_constraints);
    
    // Update layout with new arrays
    layout->controls = new_controls;
    layout->control_constraints = new_constraints;
    layout->control_capacity = new_capacity;
}