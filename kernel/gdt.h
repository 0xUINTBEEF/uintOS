#include <inttypes.h>
#include "bitmanip.h"
#include "asm.h"

#pragma once

#define UINTOS_DESCRIPTOR_SIZE 8

#define UINTOS_SEG_LIMIT_0_15(limit) UINTOS_BIT_VALUE(limit, 0, 15)
#define UINTOS_SEG_BASE_0_15(base) (UINTOS_BIT_VALUE(base, 0, 15) << 16)
#define UINTOS_SEG_BASE_16_23(base) UINTOS_BIT_VALUE(base, 16, 23)
#define UINTOS_SEG_BASE_24_31(base) (UINTOS_BIT_VALUE(base, 24, 31) << 24)

/* Segment access permission, only used for code and data descriptors */
#define UINTOS_SEG_R (0x0 << 8)
#define UINTOS_SEG_RA (0x1 << 8)
#define UINTOS_SEG_RW (0x2 << 8)
#define UINTOS_SEG_RWA (0x3 << 8)
#define UINTOS_SEG_RE (0x4 << 8)
#define UINTOS_SEG_REA (0x5 << 8)
#define UINTOS_SEG_RWE (0x6 << 8)
#define UINTOS_SEG_RWEA (0x7 << 8)
#define UINTOS_SEG_X (0x8 << 8)
#define UINTOS_SEG_XA (0x9 << 8)
#define UINTOS_SEG_XR (0xA << 8)
#define UINTOS_SEG_XRA (0xB << 8)
#define UINTOS_SEG_XC (0xC << 8)
#define UINTOS_SEG_XCA (0xD << 8)
#define UINTOS_SEG_XRC (0xE << 8)
#define UINTOS_SEG_XRCA (0xF << 8)

/* Segment types, only used for system descriptors */
#define UINTOS_SEG_TSS16_AVAILABLE (0x1 << 8)
#define UINTOS_SEG_LDT (0x2 << 8)
#define UINTOS_SEG_TSS16_BUSY (0x3 << 8)
#define UINTOS_SEG_CALLGATE16 (0x4 << 8)
#define UINTOS_SEG_TASKGATE (0x5 << 8)
#define UINTOS_SEG_INTGATE16 (0x6 << 8)
#define UINTOS_SEG_TRAPGATE16 (0x7 << 8)
#define UINTOS_SEG_TSS32_AVAILABLE (0x9 << 8)
#define UINTOS_SEG_TSS32_BUSY (0xB << 8)
#define UINTOS_SEG_CALLGATE32 (0xC << 8)
#define UINTOS_SEG_INTGATE32 (0xE << 8)
#define UINTOS_SEG_TRAPGATE32 (0xF << 8)

/* Segment is code/data or reserved for system */
#define UINTOS_SEG_SYSTEM 0
#define UINTOS_SEG_CODE_DATA (0x1 << 12)

/* Segment DPL */
#define UINTOS_SEG_RING0 (0x0 << 13)
#define UINTOS_SEG_RING1 (0x1 << 13)
#define UINTOS_SEG_RING2 (0x2 << 13)
#define UINTOS_SEG_RING3 (0x3 << 13)

/* Segment present or not */
#define UINTOS_SEG_NOT_PRESENT 0
#define UINTOS_SEG_PRESENT (1 << 15)

/* Segment limit 16-19 */
#define UINTOS_SEG_LIMIT_16_19(lim) (lim << 16)

/* Segment available */
#define UINTOS_SEG_AVAILABLE_0 0
#define UINTOS_SEG_AVAILABLE_1 (1 << 20)

/* Segment 64-bit, default operation size must set to SEG16, which is 0 */
#define UINTOS_SEG64_0 0
#define UINTOS_SEG64_1 (1 << 21)

/* Segment default operation sizes, D-flag for code segment and B-flag for data/stack segments */
#define UINTOS_SEG16 0
#define UINTOS_SEG32 (1 << 22)

/* Granularity */
#define UINTOS_SEG1B 0
#define UINTOS_SEG4K (1 << 23)

#define UINTOS_SEG_AVAILABLE 0

#define UINTOS_GDT_ENTRY_SIZE 8
#define UINTOS_GDT_SIZE 8192

#define UINTOS_CODE_SELECTOR (0x1 << 3)
#define UINTOS_DATA_SELECTOR ((0x2 << 3))
#define UINTOS_VIDEO_SELECTOR ((0x4 << 3))

#define UINTOS_GDT_OFFSET(index) (index * 8)

typedef union _segment_descriptor {
    uint64_t raw;

    struct {
        uint32_t first;
        uint32_t second;
    };

    struct {
        uint16_t limit_0_15;
        uint16_t base_0_15;
        uint8_t base_16_23;
        uint8_t type;
        uint8_t granularity;
        uint8_t base_24_31;
    };
} segment_descriptor;

typedef union _segment_selector {
    uint16_t raw;

    struct {
        uint16_t rpl : 2;
        uint16_t ti : 1;
        uint16_t index : 13;
    };
} segment_selector;

// 6 bytes struct is required by lgdt
// we must always remember compiler's automatic alignment when writing low level code
typedef struct __attribute__((__packed__)) gdt {
    /*
     * the size of the table in bytes
     * BEWARE: the table can carry 8192 entries, which means at most 0xFFFF bytes, not 0x10000 bytes = 8192 * 8. The reason is that the table starts at index 0, and byte 0 is included. Thus, the maximimum boundary is 0 + 0xFFFFh. */
    uint16_t size;
    segment_descriptor *base;
    uint16_t free_id;
} descriptor_table;

typedef descriptor_table gdt_t;
typedef descriptor_table ldt_t;

extern gdt_t uintos_gdt;

#define uintos_gdt_get_segment(index) get_segment(uintos_gdt, index)
#define uintos_remove_segment(index) (uintos_gdt.base[index].raw = 0)
#define uintos_gdt_add_segment(base, limit, access) add_segment(&uintos_gdt, base, limit, access, 0x0, 1)

uint16_t add_segment(descriptor_table *t, uint32_t base, uint32_t limit, uint32_t access, uint16_t id, uint8_t auto_id);
segment_descriptor* get_segment(descriptor_table *table, uint16_t index);
int8_t null_segment_p(segment_descriptor* s);
void setup_gdt();
