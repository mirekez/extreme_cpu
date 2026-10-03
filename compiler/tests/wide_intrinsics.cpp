#include <stdint.h>
volatile uint64_t values[] = {0, 1, 0x8000000000000000ULL, 0x123456789abcdef0ULL, ~uint64_t(0)};
volatile unsigned counts[] = {0, 1, 7, 31, 32, 33, 63, 64, 65};

extern "C" unsigned kernel() {
    for (unsigned i = 0; i < 5; ++i) {
        uint64_t x = values[i];
        unsigned leading = 0, trailing = 0, population = 0;
        // Volatile shifts retain an independent loop reference.
        volatile uint64_t scan = x;
        for (unsigned bit = 0; bit < 64; ++bit) {
            population += unsigned((scan >> bit) & 1);
            if (((scan >> bit) & 1) == 0 && trailing == bit) {
                ++trailing;
            }
            if (((scan >> (63 - bit)) & 1) == 0 && leading == bit) {
                ++leading;
            }
        }
        if (__builtin_popcountll(x) != population) {
            return 1;
        }
        if (x && (__builtin_clzll(x) != leading || __builtin_ctzll(x) != trailing)) {
            return 2;
        }
        for (unsigned j = 0; j < 9; ++j) {
            unsigned n = counts[j] & 63;
            uint64_t reference = n ? (x << n) | (x >> (64 - n)) : x;
            if (__builtin_rotateleft64(x, counts[j]) != reference) {
                return 3;
            }
        }
    }
    uint64_t result;
    uint64_t maximum = values[4];
    if (!__builtin_mul_overflow(maximum, uint64_t(2), &result) || result != maximum - 1) {
        return 4;
    }
    if (__builtin_mul_overflow(maximum, uint64_t(0), &result) || result != 0) {
        return 5;
    }
    if (__builtin_mul_overflow(uint64_t(0x100000000), uint64_t(0xffffffff), &result) ||
        result != 0xffffffff00000000ULL) {
        return 6;
    }
    return 42;
}
