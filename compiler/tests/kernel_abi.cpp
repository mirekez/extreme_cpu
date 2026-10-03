#include <stdint.h>

struct Pair {
    uint32_t first;
    uint32_t second;
};

volatile uint32_t input[] = {0, 1, 0x80000000u, 0xffffffffu, 0x00100100u};
const unsigned leading[] = {32, 31, 0, 0, 11};
const unsigned population[] = {0, 1, 1, 32, 2};

__attribute__((noinline)) Pair swap_pair(Pair value) {
    return {value.second, value.first};
}

// Exception-free library abort handlers may accept and ignore variadic arguments.
__attribute__((noinline)) unsigned ignore_extra(unsigned value, ...) {
    return value;
}

extern "C" unsigned kernel() {
    for (unsigned i = 0; i < 5; ++i) {
        uint32_t value = input[i];
        if (__builtin_clzg(value, 32) != leading[i] || __builtin_popcount(value) != population[i]) {
            return 1;
        }
        Pair pair = swap_pair({value, ~value});
        if (pair.first != ~value || pair.second != value) {
            return 2;
        }
    }
    return ignore_extra(42, input[1], input[2]);
}
