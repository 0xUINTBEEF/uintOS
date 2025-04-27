#define UINTOS_BIT_MASK(start, end) ((~(~0 << (end - start + 1))) << start)
#define UINTOS_BIT_VALUE(v, start, end) ((UINTOS_BIT_MASK(start, end) & v) >> start)
