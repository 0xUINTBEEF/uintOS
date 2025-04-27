#include <inttypes.h>

#define UINTOS_LAPIC_BASE 0xfee00000
#define UINTOS_LAPIC_VERSION_REG UINTOS_LAPIC_BASE + 0x0030
#define UINTOS_CMICI_REG UINTOS_LAPIC_BASE + 0x02F0
#define UINTOS_THERMAL_MONITOR_REG UINTOS_LAPIC_BASE + 0x0330

#define UINTOS_TIMER_REG UINTOS_LAPIC_BASE + 0x0320
#define UINTOS_TIMER_INIT_COUNT_REG UINTOS_LAPIC_BASE + 0x0380
#define UINTOS_TIMER_CURRENT_COUNT_REG UINTOS_LAPIC_BASE + 0x0390
#define UINTOS_TIMER_DIVIDE_CONFIG_REG UINTOS_LAPIC_BASE + 0x03E0

#define UINTOS_LAPIC_ISR_BASE UINTOS_LAPIC_BASE + 0x0100

#define UINTOS_LAPIC_EIO_REG UINTOS_LAPIC_BASE + 0x00b0

#define UINTOS_LAPIC_ERROR_REG UINTOS_LAPIC_BASE + 0x0280

#define UINTOS_TIMER_ONE_SHOT 0x0
#define UINTOS_TIMER_PERIODIC 0x1
#define UINTOS_TIMER_TSC_DEADLINE 0x2

#define UINTOS_TIMER_DIV_2 0x0
#define UINTOS_TIMER_DIV_4 0x1
#define UINTOS_TIMER_DIV_8 0x2
#define UINTOS_TIMER_DIV_16 0x3
#define UINTOS_TIMER_DIV_32 0x8
#define UINTOS_TIMER_DIV_64 0x9
#define UINTOS_TIMER_DIV_128 0xA
#define UINTOS_TIMER_DIV_1 0xB

#define UINTOS_TIMER_MODE(mode) (mode << 17)
#define UINTOS_MASK(m) (m << 16)
#define UINTOS_DELIVERY_STATUS(s) (s << 12)
#define UINTOS_DELIVERY_MODE(s) (s << 8)
#define UINTOS_VECTOR(v) v

#define UINTOS_CONCATENATE_DETAILS(x,y) x ## y
#define UINTOS_CONCATENATE(x,y) UINTOS_CONCATENATE_DETAILS(x, y)
#define UINTOS_MAKE_UNIQUE(x) UINTOS_CONCATENATE(x,__COUNTER__)

#define uintos_defpointer(type, name, address)           \
    type* name =  (type *)((uintptr_t) address);

#define uintos_lapic_isr_complete(num)                 \
    ;                                           \
    uintos_defpointer(uint32_t, eoi, UINTOS_LAPIC_EIO_REG);   \
    *eoi = 1;                              \

void uintos_lapic_enable_timer(uint8_t mode, uint32_t init_count, uint32_t div_config, uint32_t vec_num);
uint32_t uintos_lapic_get_version();
uint32_t uintos_lapic_get_current_timer_count();
uint32_t uintos_lapic_get_error_status();
void uintos_lapic_set_timer_initial_count(uint32_t count);
void uintos_lapic_set_divide_config(uint32_t d);
uint32_t uintos_lapic_get_timer_setting();
