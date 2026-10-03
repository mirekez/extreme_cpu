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
void __extreme_simd_carry32(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_pair_swap32(uint32_t* destination, const uint32_t* source);
void __extreme_simd_pair_up32(uint32_t* destination, const uint32_t* source);
void __extreme_simd_pair_down32(uint32_t* destination, const uint32_t* source);
// Software sequences over 32-bit lanes; each adjacent [low, high] pair is independent.
void __extreme_simd_add64(uint32_t* destination, const uint32_t* left, const uint32_t* right);
void __extreme_simd_sub64(uint32_t* destination, const uint32_t* left, const uint32_t* right);
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

__attribute__((always_inline)) inline void carry32(uint32_t* destination, const uint32_t* left,
                                                   const uint32_t* right) {
    __extreme_simd_carry32(destination, left, right);
}

__attribute__((always_inline)) inline void borrow32(uint32_t* destination, const uint32_t* left,
                                                    const uint32_t* right) {
    __extreme_simd_ltu32(destination, left, right);
}

__attribute__((always_inline)) inline void pair_swap32(uint32_t* destination,
                                                       const uint32_t* source) {
    __extreme_simd_pair_swap32(destination, source);
}

__attribute__((always_inline)) inline void pair_up32(uint32_t* destination,
                                                     const uint32_t* source) {
    __extreme_simd_pair_up32(destination, source);
}

__attribute__((always_inline)) inline void pair_down32(uint32_t* destination,
                                                       const uint32_t* source) {
    __extreme_simd_pair_down32(destination, source);
}

__attribute__((always_inline)) inline void add64(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_add64(destination, left, right);
}

__attribute__((always_inline)) inline void sub64(uint32_t* destination, const uint32_t* left,
                                                 const uint32_t* right) {
    __extreme_simd_sub64(destination, left, right);
}
} // namespace extreme::simd
#endif
