#include "task1.h"

UINTOS_TASK_START(uintos_task1, uintos_init_task1);
int uintos_task1_var;

void uintos_job1(int a, int b) {
    uintos_task1_var = 1;
}

void uintos_job2() {
    uintos_task1_var = 2;
}

void uintos_init_task1() {
    // Removed division by zero that would crash the system
    uintos_job1(1, 2);
    uintos_job2();
}

UINTOS_TASK_END(uintos_task1);
