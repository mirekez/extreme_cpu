#include <stdint.h>

struct Case {
    uint64_t a, b, sum, difference, product, quotient, remainder;
    uint64_t signed_quotient, signed_remainder, swapped, bit_and, bit_or, bit_xor;
    unsigned comparison;
};

// Expected values are independent host-integer calculations, not target helpers.
volatile Case cases[] = {
    {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000001ULL, 0xffffffffffffffffULL,
     0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
     0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000001ULL,
     0x0000000000000001ULL, 206},
    {0xffffffffffffffffULL, 0x0000000100000001ULL, 0x0000000100000000ULL, 0xfffffffefffffffeULL,
     0xfffffffeffffffffULL, 0x00000000ffffffffULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
     0xffffffffffffffffULL, 0xffffffffffffffffULL, 0x0000000100000001ULL, 0xffffffffffffffffULL,
     0xfffffffefffffffeULL, 242},
    {0x123456789abcdef0ULL, 0x0000000987654321ULL, 0x1234568222222211ULL, 0x1234566f13579bcfULL,
     0x3333328fe5618cf0ULL, 0x0000000001e9131aULL, 0x000000071c6f9a96ULL, 0x0000000001e9131aULL,
     0x000000071c6f9a96ULL, 0xf0debc9a78563412ULL, 0x0000000882244220ULL, 0x123456799ffddff1ULL,
     0x123456711dd99dd1ULL, 818},
    {0x8000000000000000ULL, 0x0000000000000003ULL, 0x8000000000000003ULL, 0x7ffffffffffffffdULL,
     0x8000000000000000ULL, 0x2aaaaaaaaaaaaaaaULL, 0x0000000000000002ULL, 0xd555555555555556ULL,
     0xfffffffffffffffeULL, 0x0000000000000080ULL, 0x0000000000000000ULL, 0x8000000000000003ULL,
     0x8000000000000003ULL, 242},
    {0x0000000100000001ULL, 0xffffffffffffffffULL, 0x0000000100000000ULL, 0x0000000100000002ULL,
     0xfffffffeffffffffULL, 0x0000000000000000ULL, 0x0000000100000001ULL, 0xfffffffeffffffffULL,
     0x0000000000000000ULL, 0x0100000001000000ULL, 0x0000000100000001ULL, 0xffffffffffffffffULL,
     0xfffffffefffffffeULL, 782},
    {0xffffffff00000001ULL, 0x00000000ffffffffULL, 0x0000000000000000ULL, 0xfffffffe00000002ULL,
     0x00000001ffffffffULL, 0x0000000100000000ULL, 0x0000000000000001ULL, 0xffffffffffffffffULL,
     0x0000000000000000ULL, 0x01000000ffffffffULL, 0x0000000000000001ULL, 0xffffffffffffffffULL,
     0xfffffffffffffffeULL, 242}};

__attribute__((noinline)) uint64_t calculate(uint64_t a, unsigned tag, uint64_t b) {
    return (a ^ b) + tag;
}

using Calculation = uint64_t (*)(uint64_t, unsigned, uint64_t);
#ifdef EXTREME_STATIC_ABI
#define call calculate
#else
Calculation volatile call = calculate;
#endif

struct __attribute__((packed)) Packed {
    unsigned char prefix;
    uint64_t value;
};

volatile Packed packed = {7, 0x123456789abcdef0ULL};
volatile uint64_t shift_input = 0x89abcdef01234567ULL;

volatile unsigned shift_counts[] = {0, 1, 31, 32, 33, 63};
const uint64_t left_shift[] = {0x89abcdef01234567ULL, 0x13579bde02468aceULL, 0x8091a2b380000000ULL,
                               0x0123456700000000ULL, 0x02468ace00000000ULL, 0x8000000000000000ULL};
const uint64_t right_shift[] = {0x89abcdef01234567ULL, 0x44d5e6f78091a2b3ULL,
                                0x0000000113579bdeULL, 0x0000000089abcdefULL,
                                0x0000000044d5e6f7ULL, 0x0000000000000001ULL};
const uint64_t signed_shift[] = {0x89abcdef01234567ULL, 0xc4d5e6f78091a2b3ULL,
                                 0xffffffff13579bdeULL, 0xffffffff89abcdefULL,
                                 0xffffffffc4d5e6f7ULL, 0xffffffffffffffffULL};
volatile int32_t negative_source = -1234567;

__attribute__((noinline, optnone)) uint64_t shift_up(uint64_t value) {
    return value << 32;
}

__attribute__((noinline, optnone)) uint64_t shift_down(uint64_t value) {
    return value >> 32;
}

__attribute__((noinline, optnone)) unsigned classify(uint64_t value) {
    switch (value) {
    case 0:
        return 1;
    case 0x100000000ULL:
        return 2;
    case 0x123456789abcdef0ULL:
        return 3;
    default:
        return 4;
    }
}

extern "C" unsigned kernel() {
    uint64_t accumulator = 0;
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint64_t a = cases[index].a, b = cases[index].b;
        if (a + b != cases[index].sum || a - b != cases[index].difference ||
            a * b != cases[index].product) {
            return 1;
        }
        if (a / b != cases[index].quotient || a % b != cases[index].remainder) {
            return 2;
        }
        int64_t sa = (int64_t)a, sb = (int64_t)b;
        if (sa / sb != (int64_t)cases[index].signed_quotient ||
            sa % sb != (int64_t)cases[index].signed_remainder) {
            return 3;
        }
        if (__builtin_bswap64(a) != cases[index].swapped || (a & b) != cases[index].bit_and ||
            (a | b) != cases[index].bit_or || (a ^ b) != cases[index].bit_xor) {
            return 4;
        }
        unsigned comparisons = (a == b) | ((a != b) << 1) | ((a < b) << 2) | ((a <= b) << 3) |
                               ((a > b) << 4) | ((a >= b) << 5) | ((sa < sb) << 6) |
                               ((sa <= sb) << 7) | ((sa > sb) << 8) | ((sa >= sb) << 9);
        if (comparisons != cases[index].comparison) {
            return 5;
        }
        if (call(a, 3, b) != cases[index].bit_xor + 3) {
            return 6;
        }
        packed.value = a;
        if (packed.value != a || packed.prefix != 7) {
            return 7;
        }
        // Keep i64 PHI/select values live across arithmetic helper calls.
        accumulator ^= index & 1 ? a : b;
    }
    for (unsigned index = 0; index < 6; ++index) {
        unsigned n = shift_counts[index];
        if ((shift_input << n) != left_shift[index] || (shift_input >> n) != right_shift[index] ||
            ((int64_t)shift_input >> n) != (int64_t)signed_shift[index]) {
            return 10 + index;
        }
    }
    if (shift_up(shift_input) != 0x0123456700000000ULL ||
        shift_down(shift_input) != 0x0000000089abcdefULL) {
        return 79;
    }
    if (accumulator != 0x7ffffff687654321ULL) {
        return 80;
    }
    int32_t negative = (int32_t)(uint32_t)shift_input;
    if ((int64_t)negative != 0x01234567LL) {
        return 81;
    }
    if ((int64_t)negative_source != -1234567LL) {
        return 82;
    }
    if (classify(cases[0].a) != 1 || classify(cases[1].sum) != 2 || classify(cases[2].a) != 3 ||
        classify(cases[3].a) != 4) {
        return 83;
    }
    return 42;
}
