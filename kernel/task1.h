// #includ "gdt.h"
#include "task.h"

UINTOS_TASK_REGISTER(uintos_task1);

struct uintos_tss uintos_task1_tss;

void uintos_init_task1();
