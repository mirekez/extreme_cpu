#ifdef EC_SIMD
#include <simd/simd.h>
using namespace extreme::simd;
constexpr unsigned lanes = EC_BUS_BYTES / 4;
alignas(EC_BUS_BYTES) uint32_t left[lanes];
alignas(EC_BUS_BYTES) uint32_t right[lanes];
alignas(EC_BUS_BYTES) uint32_t output[12][lanes];

__attribute__((noinline)) void compute(uint32_t* a, uint32_t* b) {
    add32(output[0], a, b);
    sub32(output[1], a, b);
    and32(output[2], a, b);
    or32(output[3], a, b);
    xor32(output[4], a, b);
    shl32(output[5], a, b);
    shr32(output[6], a, b);
    ltu32(output[7], a, b);
    sar32(output[8], a, b);
    mul32(output[9], a, b);
    lts32(output[10], a, b);
    splat32(output[11], 0x87654321);
}

extern "C" unsigned kernel() {
    for (unsigned i = 0; i < lanes; ++i) {
        left[i] = 0xffffffffu - i * 0x12345678u;
        right[i] = 31 + i;
    }
    compute(left, right);
    for (unsigned i = 0; i < lanes; ++i) {
        uint32_t x = left[i], y = right[i], shift = y & 31;
        uint32_t sar = (x >> shift) | ((x & 0x80000000u) && shift ? ~0u << (32 - shift) : 0);
        if (output[0][i] != x + y || output[1][i] != x - y || output[2][i] != (x & y) ||
            output[3][i] != (x | y) || output[4][i] != (x ^ y) || output[5][i] != (x << shift) ||
            output[6][i] != (x >> shift) || output[7][i] != unsigned(x < y) ||
            output[8][i] != sar || output[9][i] != x * y ||
            output[10][i] != unsigned((x ^ 0x80000000u) < (y ^ 0x80000000u)) ||
            output[11][i] != 0x87654321) {
            return 1;
        }
    }
    // In-place stores and dependent reads must observe completed prior stores.
    add32(left, left, right);
    sub32(left, left, right);
    xor32(right, right, right);
    for (unsigned i = 0; i < lanes; ++i) {
        if (left[i] != 0xffffffffu - i * 0x12345678u || right[i] != 0) {
            return 2;
        }
    }
    splat32(left, 42);
    return left[lanes - 1];
}
#endif
