#include <stdint.h>

// C-specific syntax verifies that .c inputs are not parsed as C++.
struct Input {
    uint32_t first;
    uint32_t second;
};

__attribute__((noinline)) uint32_t sum(const struct Input* input) {
    return input->first + input->second;
}

uint32_t kernel(void) {
    return sum(&(struct Input){.first = 17, .second = 25});
}
