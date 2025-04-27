#include "../filesystem/fat12.h"
#include "greatest.h"
#include <string.h>

TEST test_fat12_read_file() {
    char buffer[512];
    fat12_init();

    int bytes_read = fat12_read_file("TEST    TXT", buffer, sizeof(buffer));
    ASSERT(bytes_read > 0);
    ASSERT(strncmp(buffer, "Hello, FAT12!", 13) == 0);

    PASS();
}

TEST sample_test() {
    ASSERT_EQ(1, 1);
    PASS();
}

SUITE(test_suite) {
    RUN_TEST(sample_test);
}

SUITE(fat12_suite) {
    RUN_TEST(test_fat12_read_file);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(test_suite);
    RUN_SUITE(fat12_suite);
    GREATEST_MAIN_END();
}