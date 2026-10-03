#ifdef EC_SIMD
#include <simd/simd.h>
using namespace extreme::simd;
constexpr unsigned lanes = EC_BUS_BYTES / 4;

struct Case {
    uint32_t a[2], b[2], sum[2], difference[2], doubled[2], carry[2];
};

// Independent host 64-bit results cover low carry/borrow, high overflow and pair isolation.
const Case cases[] = {
    {0xffffffffu, 0x00000000u, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000001u, 0xfffffffeu,
     0x00000000u, 0xfffffffeu, 0x00000001u, 0x00000001u, 0x00000000u},
    {0xffffffffu, 0xffffffffu, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, 0xfffffffeu,
     0xffffffffu, 0xfffffffeu, 0xffffffffu, 0x00000001u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u, 0x00000001u, 0x00000000u, 0xffffffffu,
     0xffffffffu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000001u, 0x00000001u, 0x00000000u, 0x00000001u, 0x00000001u, 0xffffffffu,
     0x00000000u, 0x00000000u, 0x00000002u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0xffffffffu, 0x00000000u, 0xffffffffu, 0x00000000u, 0xfffffffeu, 0x00000000u,
     0x00000000u, 0x00000000u, 0xfffffffeu, 0x00000000u, 0x00000001u},
    {0x00000003u, 0x00000002u, 0x00000002u, 0x40000000u, 0x00000005u, 0x40000002u, 0x00000001u,
     0xc0000002u, 0x00000006u, 0x00000004u, 0x00000000u, 0x00000000u},
    {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u, 0x00000000u, 0x00000001u, 0x00000000u,
     0x00000000u, 0x00000000u, 0x00000001u, 0x00000001u, 0x00000001u},
    {0x9abcdef0u, 0x12345678u, 0x10fedcbau, 0x98765432u, 0xabbbbbaau, 0xaaaaaaaau, 0x89be0236u,
     0x79be0246u, 0x3579bde0u, 0x2468acf1u, 0x00000000u, 0x00000000u}};
alignas(EC_BUS_BYTES) uint32_t left[lanes], right[lanes];
alignas(EC_BUS_BYTES) uint32_t sum[lanes], difference[lanes], carry[lanes], borrow[lanes];
alignas(EC_BUS_BYTES) uint32_t swapped[lanes], up[lanes], down[lanes];

__attribute__((noinline)) void compute() {
    add64(sum, left, right);
    sub64(difference, left, right);
    carry32(carry, left, right);
    borrow32(borrow, left, right);
    pair_swap32(swapped, left);
    pair_up32(up, left);
    pair_down32(down, left);
}

extern "C" unsigned kernel() {
    for (unsigned round = 0; round < 8; ++round) {
        for (unsigned lane = 0; lane < lanes; ++lane) {
            const Case& item = cases[(round + lane / 2) % 8];
            left[lane] = item.a[lane % 2];
            right[lane] = item.b[lane % 2];
        }
        compute();
        for (unsigned lane = 0; lane < lanes; ++lane) {
            const Case& item = cases[(round + lane / 2) % 8];
            unsigned half = lane % 2;
            if (sum[lane] != item.sum[half] || difference[lane] != item.difference[half] ||
                carry[lane] != item.carry[half] ||
                borrow[lane] != unsigned(item.a[half] < item.b[half])) {
                return 1;
            }
            if (swapped[lane] != item.a[half ^ 1] || up[lane] != (half ? item.a[0] : 0) ||
                down[lane] != (half ? 0 : item.a[1])) {
                return 2;
            }
        }
        add64(left, left, right);
        sub64(left, left, right);
        sub64(right, left, right);
        for (unsigned lane = 0; lane < lanes; ++lane) {
            const Case& item = cases[(round + lane / 2) % 8];
            if (left[lane] != item.a[lane % 2] || right[lane] != item.difference[lane % 2]) {
                return 3;
            }
        }
        pair_swap32(left, left);
        pair_swap32(left, left);
        add64(left, left, left);
        for (unsigned lane = 0; lane < lanes; ++lane) {
            if (left[lane] != cases[(round + lane / 2) % 8].doubled[lane % 2]) {
                return 4;
            }
        }
    }
    return 42;
}
#endif
