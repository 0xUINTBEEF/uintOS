#include "gdt.h"

static segment_descriptor uintos_gdt_segments[UINTOS_GDT_SIZE];

uintos_gdt_t uintos_global_descriptor_table = {
    .base = uintos_gdt_segments,
    .size = (UINTOS_DESCRIPTOR_SIZE * UINTOS_GDT_SIZE) - 1,
    .next_id = 0
};

// Add a new descriptor to the GDT. This implementation scans the table to find the next available ID.
uint16_t uintos_create_segment(descriptor_table* table, uint32_t base, uint32_t limit, uint32_t access, uint16_t id, uint8_t auto_assign) {
    segment_descriptor *segment;

    if (auto_assign) {
        if (table->next_id > UINTOS_GDT_SIZE)
            table->next_id = 1;
    } else {
        table->next_id = id;
    }

    do {
        segment = uintos_fetch_segment(table, table->next_id);

        if (auto_assign)
            table->next_id++;
        else
            break;

        if (uintos_is_null_segment(segment)) break;
    } while (table->next_id < UINTOS_GDT_SIZE);

    segment->first = UINTOS_SEG_BASE_0_15(base) | UINTOS_SEG_LIMIT_0_15(limit);
    segment->second = UINTOS_SEG_BASE_24_31(base) | access | UINTOS_SEG_BASE_16_23(base);

    return auto_assign ? (table->next_id - 1) : id;
}

inline int8_t uintos_is_null_segment(segment_descriptor* segment) {
    return (segment->raw == 0);
}

segment_descriptor* uintos_fetch_segment(descriptor_table *table, uint16_t index) {
    if (index >= UINTOS_GDT_SIZE) return 0;

    return &table->base[index];
}

void uintos_initialize_gdt(struct uintos_tss* initial_tss) {
  uintos_create_segment(&uintos_global_descriptor_table, 0x0, 0x0, 0x0, 0, 1);

  uintos_create_segment(&uintos_global_descriptor_table, 0x0000, 0xFFFF,
                 UINTOS_SEG4K | UINTOS_SEG32 | UINTOS_SEG64_0 | UINTOS_SEG_LIMIT_16_19(0xf) |
                 UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_CODE_DATA | UINTOS_SEG_XR, 0, 1);

  uintos_create_segment(&uintos_global_descriptor_table, 0x0000, 0xFFFF,
                 UINTOS_SEG4K | UINTOS_SEG32 | UINTOS_SEG64_0 | UINTOS_SEG_LIMIT_16_19(0xf) |
                 UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_CODE_DATA | UINTOS_SEG_RWE, 0, 1);

  uintos_create_segment(&uintos_global_descriptor_table, (uintptr_t) initial_tss, 0x067,
                 UINTOS_SEG1B | UINTOS_SEG_AVAILABLE_1 | UINTOS_SEG_LIMIT_16_19(0x0) |
                 UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_SYSTEM | UINTOS_SEG_TSS32_AVAILABLE, 0, 1);

  uintos_create_segment(&uintos_global_descriptor_table, 0xB8000, 0x7FFF,
                 UINTOS_SEG4K | UINTOS_SEG32 | UINTOS_SEG64_0 | UINTOS_SEG_LIMIT_16_19(0xf) |
                 UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_CODE_DATA | UINTOS_SEG_RW, 0, 1);

  uintos_create_segment(&uintos_global_descriptor_table, (uintptr_t) &uintos_gdt_segments, UINTOS_GDT_SIZE,
                 UINTOS_SEG4K | UINTOS_SEG32 | UINTOS_SEG64_0 | UINTOS_SEG_LIMIT_16_19(0xf) |
                 UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_CODE_DATA | UINTOS_SEG_RW, 0, 1);

  UINTOS_LOAD_GDT(uintos_global_descriptor_table);
  UINTOS_LOAD_TASK_REGISTER(0x3 << 3);
}
