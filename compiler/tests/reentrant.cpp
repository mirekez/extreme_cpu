#include <stdint.h>
#include <string.h>

using Operation = uint32_t (*)(uint32_t);

__attribute__((noinline)) uint32_t recurse(uint32_t depth) {
    volatile uint32_t saved = depth + 7;
    if (depth == 0) {
        return saved;
    }
    uint32_t child = recurse(depth - 1);
    return child + saved;
}

__attribute__((noinline)) uint32_t square(uint32_t value) {
    return value * value;
}

Operation volatile operations[2] = {recurse, square};

__attribute__((noinline, optnone)) bool is_aligned(uintptr_t address) {
    return (address & 255) == 0;
}

__attribute__((noinline)) uint32_t invoke(Operation operation, uint32_t value) {
    alignas(256) unsigned char source[EC_BUS_BYTES];
    alignas(256) unsigned char destination[EC_BUS_BYTES];
    if (!is_aligned(reinterpret_cast<uintptr_t>(source)) ||
        !is_aligned(reinterpret_cast<uintptr_t>(destination))) {
        return 999;
    }
    for (unsigned i = 0; i < EC_BUS_BYTES; ++i) {
        source[i] = value + i;
    }
    // A called function's COPY must preserve both return registers and r7.
    memcpy(destination, source, sizeof(source));
    uint32_t result = operation(value);
    for (unsigned i = 0; i < EC_BUS_BYTES; ++i) {
        if (destination[i] != value + i) {
            return 1000;
        }
    }
    return result;
}

extern "C" uint32_t kernel() {
    if (invoke(operations[0], 2) != 24) {
        return 1;
    }
    if (invoke(operations[1], 6) != 36) {
        return 2;
    }
    if (recurse(3) != 34) {
        return 3;
    }
    return 42;
}
