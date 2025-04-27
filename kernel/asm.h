#define UINTOS_LOAD_IDT(idt) asm("lidt "#idt)
#define UINTOS_LOAD_GDT(gdt) asm("lgdt "#gdt)
#define UINTOS_INTERRUPT_RETURN()                 \
    asm("add esp, 0x4");                        \
    asm("leave");                               \
    asm("pop eax");                             \
    asm("iret")

#define UINTOS_SET_ES(var)                        \
    asm("mov edx, "#var"\n\t"               \
        "mov es, dx" :: "r"(var))

#define UINTOS_SET_CS(var)                        \
    asm("mov edx, "#var"\n\t"               \
        "mov cs, dx")

#define UINTOS_SET_DS(var)                        \
    asm("mov edx, "#var"\n\t"               \
        "mov ds, dx")

#define UINTOS_SET_SS(var)                        \
    asm("mov edx, "#var"\n\t"               \
        "mov ss, dx")

#define UINTOS_SET_GS(var)                        \
    asm("mov edx, "#var"\n\t"               \
        "mov gs, dx")

#define UINTOS_WRITE_MEM_ES(offset, value)        \
    asm("mov edx, "#value"\n\t"             \
        "mov word [es:"#offset"], edx")

#define UINTOS_LOAD_TASK_REGISTER(gdt_index)      \
    asm("mov edx, "#gdt_index"\n\t"         \
        "ltr dx")
