# SIMD32 C++ intrinsics

Configure the project with `-DEC_SIMD=ON`, then compile kernels with that
build's `bin/extreme-cxx`. Include `<simd/simd.h>` and use `extreme::simd`.
All declarations and backend lowering are guarded by `#ifdef EC_SIMD`.

`add32`, `sub32`, `and32`, `or32`, `xor32`, `shl32`, `shr32`, `sar32`, `mul32`,
`ltu32`, and `lts32` accept `(uint32_t* destination, const uint32_t* left,
const uint32_t* right)`. Each performs one operation on BUS_WIDTH/32 lanes.
`splat32(uint32_t* destination, uint32_t value)` broadcasts a scalar.
Comparisons produce 0 or 1 in each lane. Arithmetic wraps modulo 2^32;
shifts use the low five bits of each right-hand lane.

`carry32(destination, left, right)` writes the unsigned carry-out of each
32-bit sum as 0 or 1. `borrow32` writes 0 or 1 for unsigned `left < right`.
The sum/difference is a separate operation; there are no hidden carry flags.

`pair_swap32(destination, source)`, `pair_up32`, and `pair_down32` operate on
each adjacent [low,high] pair of lanes. They produce [high,low], [0,low], and
[high,0], respectively. These are fixed 32-bit moves, not 64-bit ALU operations.

`add64(destination, left, right)` and `sub64` use the same `uint32_t*` pointer
signatures as `add32`. Each processes BUS_WIDTH/64 independent little-endian
pairs modulo 2^64. The compiler emits a carry/borrow mask, moves each low-word
carry into its paired high lane, and uses two 32-bit SIMD arithmetic operations.
Overflow does not propagate between pairs. All of these APIs operate on one
complete bus word, with the alignment and aliasing rules below.

Each pointer must reference one complete bus word (`BUS_WIDTH/8` bytes),
aligned to `BUS_WIDTH/8` bytes. For example:

```cpp
#include <simd/simd.h>
alignas(EC_BUS_BYTES) uint32_t a[EC_BUS_BYTES / 4];
alignas(EC_BUS_BYTES) uint32_t b[EC_BUS_BYTES / 4];
alignas(EC_BUS_BYTES) uint32_t result[EC_BUS_BYTES / 4];

extern "C" unsigned kernel() {
    extreme::simd::splat32(a, 7);
    extreme::simd::splat32(b, 6);
    extreme::simd::mul32(result, a, b);
    return result[0];
}
```

The compiler emits input loads, the SIMD instruction sequence, and an output store. It drains
prior stores before input reads and drains the result store before continuing.
Exact destination/input aliasing is allowed: both inputs are loaded before the
store. Source reads and destination writes must be valid in the memory map.
The intrinsics are not atomic across cores; callers still need task dependencies
or other ownership protocols for shared memory. They preserve the compiler's
return-register and task-frame conventions and work inside ordinary functions
or dispatched tasks.

Automatic vectorization remains disabled. These are explicit one-word intrinsics,
not LLVM vector values, and do not promise streaming COPY throughput. SIMD is
an integer-only extension; no floating point, saturation, reductions, or alternate
lane widths are provided yet.

Ordinary C/C++ `uint64_t` and `int64_t` expressions also use paired lanes for
addition, subtraction, bitwise operations, and constant logical shifts by 32
when SIMD is enabled. Other 64-bit arithmetic uses software helpers built from
32-bit operations. Ordinary scalar expressions use one pair; use `add64` or
`sub64` explicitly to process every pair in a register.
