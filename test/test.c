#include "greatest.h"

TEST sample_test() {
    ASSERT_EQ(1, 1);
    PASS();
}

SUITE(test_suite) {
    RUN_TEST(sample_test);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(test_suite);
    GREATEST_MAIN_END();
}