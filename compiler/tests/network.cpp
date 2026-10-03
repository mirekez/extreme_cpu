#include <stdint.h>
volatile uint32_t input = 0x12345678;
volatile uint16_t short_input = 0x89ab;

__attribute__((noinline)) unsigned checksum(const unsigned char* bytes) {
    unsigned sum = 0;
    for (unsigned i = 0; i < 8; ++i) {
        sum += bytes[i] * (i + 1);
    }
    return sum;
}

extern "C" unsigned kernel() {
    // Clang represents this initializer as a coalesced i64 constant store.
    unsigned char bytes[8] = {1, 3, 5, 7, 9, 11, 13, 15};
    if (__builtin_bswap32(input) != 0x78563412 || __builtin_bswap16(short_input) != 0xab89) {
        return 1;
    }
    if (checksum(bytes) != 372) {
        return 2;
    }
    return 42;
}
