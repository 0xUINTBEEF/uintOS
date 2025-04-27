#include "gdt.h"
#include "asm.h"
#include <inttypes.h>

#pragma once

/* Task State Structure records the machine state of a process */
struct uintos_tss {
    uint16_t link_r;
    uint16_t link_h;
    uint32_t esp0;
    uint16_t ss0_r;
    uint16_t ss0_h;
    uint32_t esp1;
    uint16_t ss1_r;
    uint16_t ss1_h;
    uint32_t esp2;
    uint16_t ss2_r;
    uint16_t ss2_h;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint16_t es_r;
    uint16_t es_h;
    uint16_t cs_r;
    uint16_t cs_h;
    uint16_t ss_r;
    uint16_t ss_h;
    uint16_t ds_r;
    uint16_t ds_h;
    uint16_t fs_r;
    uint16_t fs_h;
    uint16_t gs_r;
    uint16_t gs_h;
    uint16_t ldt_r;
    uint16_t ldt_h;
    uint16_t trap_r;
    uint16_t io_base;
};

extern struct uintos_tss uintos_init_tss;

/* Add a descriptor entry to the Local Descriptor Table of a process */
#define uintos_ldt_add_segment(name, base, limit, access)  \
    add_segment(name, base, limit, access, 0x0, 1)

/* Statically allocate a task/process image right inside the kernel */
#define UINTOS_TASK_REGISTER(name)                     \
    void name ## _start();                      \
    void name ## _init();                       \
    extern uint16_t uintos_task_id_##name       \

/* Shift 3 bits to put the task selector value in the variable into the proper location in the Task Register */
#define UINTOS_TASK_SELECTOR(name) (uintos_task_id_##name << 3)

/* 0x35000 is the currently chosen starting address for process storage. */
#define UINTOS_TASK_DATA_ADDRESS(id) 0x35000 + id * 0x1000

/* Generate init data and functions for every registered process */
#define UINTOS_TASK_START(name, init_func)                                     \
    void __UINTOS_TASK_END_##name();                                           \
    void __UINTOS_TASK_START_##name(){}                                        \
                                                                        \
    segment_descriptor name##_ldt_segments[3];                          \
    ldt_t ldt_##name;                                                   \
    uint16_t uintos_task_id_##name;                                            \
    struct uintos_tss name##_tss = {                                           \
        .link_r = 0x3 << 3,                                             \
            .ss0_r = UINTOS_DATA_SELECTOR,                                     \
            .eip = (uintptr_t) init_func,                               \
            .esp = 0x3000,                                              \
            .eflags = 0x87,                                             \
            .cr3 = 0x2000,                                              \
            .es_r = UINTOS_VIDEO_SELECTOR,                                     \
            .cs_r = UINTOS_CODE_SELECTOR,                                      \
            .ds_r = UINTOS_DATA_SELECTOR,                                      \
            .ss_r = UINTOS_DATA_SELECTOR,                                      \
            .fs_r = UINTOS_DATA_SELECTOR,                                      \
            .gs_r = UINTOS_DATA_SELECTOR,                                      \
            };                                                          \
                                                                        \
    void name##_init() {                                                \
        uint16_t ldt_id;                                                \
        ldt_##name.base = name##_ldt_segments;                          \
        ldt_##name.size = UINTOS_DESCRIPTOR_SIZE * 3;                          \
        ldt_##name.free_id = 0;                                         \
                                                                        \
        uintos_task_id_##name = uintos_gdt_add_segment((uintptr_t) &name##_tss, 0x067, \ \
                                         UINTOS_SEG1B | UINTOS_SEG_AVAILABLE_1 | UINTOS_SEG_LIMIT_16_19(0x0) | \
                                         UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_SYSTEM | UINTOS_SEG_TSS32_AVAILABLE); \
        ldt_id = uintos_gdt_add_segment((uintptr_t) &name##_ldt_segments, ldt_##name.size, \
                                 UINTOS_SEG1B | UINTOS_SEG32 | UINTOS_SEG64_0  | UINTOS_SEG_LIMIT_16_19(0x0) | \
                                 UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_SYSTEM | UINTOS_SEG_LDT); \
                                                                        \
        name##_tss.esp0 = UINTOS_TASK_DATA_ADDRESS(uintos_task_id_##name);            \
        name##_tss.esp = UINTOS_TASK_DATA_ADDRESS(uintos_task_id_##name);             \
        name##_tss.ldt_r = ldt_id << 3;                                 \
                                                                        \
        uintos_ldt_add_segment(&ldt_##name, 0, 0, 0);                          \
        uintos_ldt_add_segment(&ldt_##name, (uintptr_t) __UINTOS_TASK_START_##name,   \
                        __UINTOS_TASK_END_##name - __UINTOS_TASK_START_##name,        \
                        UINTOS_SEG4K | UINTOS_SEG_AVAILABLE_1 | UINTOS_SEG32 | UINTOS_SEG64_0 |     \
                        UINTOS_SEG_LIMIT_16_19(0xf) | UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | \
                        UINTOS_SEG_CODE_DATA | UINTOS_SEG_XR);                        \
                                                                        \
        uintos_ldt_add_segment(&ldt_##name, UINTOS_TASK_DATA_ADDRESS(uintos_task_id_##name), \
                        0xFFFF,                                         \
                        UINTOS_SEG4K | UINTOS_SEG_AVAILABLE_1 | UINTOS_SEG32 | UINTOS_SEG64_0 | UINTOS_SEG_LIMIT_16_19(0xf) | \
                        UINTOS_SEG_PRESENT | UINTOS_SEG_RING0 | UINTOS_SEG_CODE_DATA | UINTOS_SEG_RWE); \
    }                                                                   \
    void name##_start() {                                               \
        asm("pushw 0xa0");                                              \
        asm("pushd 0x0"); \
        asm("jmp FAR PTR  [esp]");                                            \
    }

// Task information structure for status reporting
typedef struct {
    int id;                  // Task ID
    unsigned int state;      // Current state (UNUSED, READY, RUNNING)
    unsigned int stack_size; // Size of the task's stack
    const char* name;        // Task name (or "Unknown")
    int is_current;          // Whether this is the currently running task
} task_info_t;

// Enhanced task management APIs
void set_task_name(int task_id, const char *name);
const char *get_task_name(int task_id);
int get_task_count(void);
int get_current_task_id(void);
int get_task_info(int task_id, task_info_t *info);
int create_named_task(void (*entry_point)(), const char *name);

// Basic task management functions for the simple scheduler
void create_task(void (*entry_point)());
void switch_task();
void initialize_multitasking();
void set_task_switching(unsigned int enabled);

// Task state definitions
#define TASK_STATE_UNUSED 0
#define TASK_STATE_READY 1
#define TASK_STATE_RUNNING 2

#define UINTOS_INIT_TASK(name) name ## _init()
#define UINTOS_RUN_TASK(name) name ## _start()
#define UINTOS_TASK_END(name) void __UINTOS_TASK_END_##name() {}
#define UINTOS_TASK_START_ADDRESS(name) (uintptr_t) &name##_start
