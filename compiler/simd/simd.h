#pragma once
#ifdef EC_SIMD
#include <stdint.h>
// Each pointer covers one full-width memory word aligned to BUS_WIDTH/8 bytes.
// Intrinsics load both inputs before storing; exact input/output aliasing is legal.
extern "C" {
void __extreme_simd_add32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_sub32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_and32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_or32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_xor32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_shl32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_shr32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_ltu32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_sar32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_mul32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_lts32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_splat32(uint32_t* destination, uint32_t value);
}

namespace extreme::simd {
__attribute__((always_inline)) inline void add32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_add32(destination, left, right);
}

__attribute__((always_inline)) inline void sub32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_sub32(destination, left, right);
}

__attribute__((always_inline)) inline void and32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_and32(destination, left, right);
}

__attribute__((always_inline)) inline void or32(uint32_t* destination, const uint32_t* left,
                                                const uint32_t* right) {
    __extreme_simd_or32(destination, left, right);
}

__attribute__((always_inline)) inline void xor32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_xor32(destination, left, right);
}

__attribute__((always_inline)) inline void shl32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_shl32(destination, left, right);
}

__attribute__((always_inline)) inline void shr32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_shr32(destination, left, right);
}

__attribute__((always_inline)) inline void ltu32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_ltu32(destination, left, right);
}

__attribute__((always_inline)) inline void sar32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_sar32(destination, left, right);
}

__attribute__((always_inline)) inline void mul32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_mul32(destination, left, right);
}

__attribute__((always_inline)) inline void lts32(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_lts32(destination, left, right);
}

__attribute__((always_inline)) inline void splat32(uint32_t* destination, uint32_t value) {
    __extreme_simd_splat32(destination, value);
}
} // namespace extreme::simd
#endif
