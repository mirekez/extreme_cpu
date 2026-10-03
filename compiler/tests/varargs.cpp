#include <stdarg.h>
#include <stdint.h>

__attribute__((noinline)) unsigned read_args(unsigned tag, ...) {
    va_list first, copy;
    va_start(first, tag);
    va_copy(copy, first);
    unsigned value = va_arg(first, unsigned);
    uint64_t wide = va_arg(first, uint64_t);
    unsigned last = va_arg(first, unsigned);
    if (value != va_arg(copy, unsigned) || wide != va_arg(copy, uint64_t) ||
        last != va_arg(copy, unsigned)) {
        return 1;
    }
    va_end(first);
    va_end(copy);
    return tag == 7 && value == 5 && wide == 0x123456789abcdef0ULL && last == 9 ? 42 : 2;
}

__attribute__((noinline)) unsigned many(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e,
                                        unsigned f, unsigned g, unsigned h, unsigned i,
                                        unsigned j) {
    return a + b + c + d + e + f + g + h + i + j;
}

volatile uint64_t source = 0x123456789abcdef0ULL;

extern "C" unsigned kernel() {
    if (many(1, 2, 3, 4, 5, 6, 7, 8, 9, unsigned(source & 15)) != 45) {
        return 3;
    }
    return read_args(7, 5, source, 9);
}
