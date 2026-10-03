#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
extern "C" {
#include "../integer64.c"
}

using Operation = void (*)(Words*, const Words*, const Words*);

static void check(Operation operation, uint64_t a, uint64_t b, uint64_t expected) {
    Words left{uint32_t(a), uint32_t(a >> 32)};
    Words right{uint32_t(b), uint32_t(b >> 32)};
    Words result{};
    operation(&result, &left, &right);
    if ((uint64_t(result.hi) << 32 | result.lo) != expected) {
        std::cerr << "integer64 helper mismatch: a=" << a << " b=" << b << '\n';
        std::exit(1);
    }
}

int main() {
    uint64_t state = 0x0123456789abcdefULL;
    auto random = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (unsigned test = 0; test < 4096; ++test) {
        uint64_t a = random(), b = random();
        if (test % 3 == 0) {
            b &= 65535;
        }
        if (b == 0) {
            b = 1;
        }
        unsigned shift = test % 64;
        int64_t sa = std::bit_cast<int64_t>(a), sb = std::bit_cast<int64_t>(b);
        check(__extreme_i64_add, a, b, a + b);
        check(__extreme_i64_sub, a, b, a - b);
        check(__extreme_i64_mul, a, b, a * b);
        check(__extreme_i64_and, a, b, a & b);
        check(__extreme_i64_or, a, b, a | b);
        check(__extreme_i64_xor, a, b, a ^ b);
        check(__extreme_i64_udiv, a, b, a / b);
        check(__extreme_i64_urem, a, b, a % b);
        if (sa != std::numeric_limits<int64_t>::min() || sb != -1) {
            check(__extreme_i64_sdiv, a, b, uint64_t(sa / sb));
            check(__extreme_i64_srem, a, b, uint64_t(sa % sb));
        }
        check(__extreme_i64_shl, a, shift, a << shift);
        check(__extreme_i64_lshr, a, shift, a >> shift);
        check(__extreme_i64_ashr, a, shift, uint64_t(sa >> shift));
        check(__extreme_i64_bswap, a, b, __builtin_bswap64(a));
    }
    std::cout << "integer64 runtime differential tests PASS\n";
}
