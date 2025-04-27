#include "lapic.h"

void uintos_enable_lapic_timer(uint8_t timer_mode, uint32_t initial_count, uint32_t divider_config, uint32_t vector_number) {
    uintos_defpointer(uint32_t, timer_register, UINTOS_TIMER_REG);
    uintos_defpointer(uint32_t, lapic_base_address, 0x1b);

    asm("mov ecx, 0x1b");
    asm("rdmsr");
    uint32_t lapic_version = uintos_lapic_get_version();
    uintos_lapic_set_timer_initial_count(initial_count);
    uintos_lapic_set_divide_config(divider_config);
    *timer_register = UINTOS_TIMER_MODE(UINTOS_TIMER_PERIODIC) | UINTOS_VECTOR(vector_number);
    uint32_t current_timer_count = uintos_lapic_get_current_timer_count();
    uint32_t error_status = uintos_lapic_get_error_status();
    uint32_t timer_settings = uintos_lapic_get_timer_setting();
}

uint32_t uintos_lapic_get_version() {
    uintos_defpointer(uint32_t, version_register, UINTOS_LAPIC_VERSION_REG);
    return *version_register;
}

uint32_t uintos_lapic_get_timer_setting() {
    uintos_defpointer(uint32_t, timer_register, UINTOS_TIMER_REG);
    return *timer_register;
}

uint32_t uintos_lapic_get_current_timer_count() {
    uintos_defpointer(uint32_t, current_count_register, UINTOS_TIMER_CURRENT_COUNT_REG);
    return *current_count_register;
}

uint32_t uintos_lapic_get_error_status() {
    uintos_defpointer(uint32_t, error_status_register, UINTOS_LAPIC_ERROR_REG);
    return *error_status_register;
}

void uintos_lapic_set_timer_initial_count(uint32_t count) {
    uintos_defpointer(uint32_t, timer_initial_count_register, UINTOS_TIMER_INIT_COUNT_REG);
    *timer_initial_count_register = count;
}

void uintos_lapic_set_divide_config(uint32_t divider) {
    uintos_defpointer(uint32_t, divider_register, UINTOS_TIMER_DIVIDE_CONFIG_REG);
    *divider_register = divider;
}
