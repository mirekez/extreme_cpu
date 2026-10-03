#include <stdint.h>

// The ISA operates on 32-bit scalar lanes. These helpers never perform a C
// 64-bit operation, so lowering cannot recursively depend on the same helper.
struct Words {
    uint32_t lo;
    uint32_t hi;
};

void __extreme_i64_add(struct Words* out, const struct Words* a, const struct Words* b) {
    uint32_t lo = a->lo + b->lo;
    out->hi = a->hi + b->hi + (lo < a->lo);
    out->lo = lo;
}

void __extreme_i64_sub(struct Words* out, const struct Words* a, const struct Words* b) {
    uint32_t lo = a->lo - b->lo;
    out->hi = a->hi - b->hi - (a->lo < b->lo);
    out->lo = lo;
}

void __extreme_i64_and(struct Words* out, const struct Words* a, const struct Words* b) {
    out->lo = a->lo & b->lo;
    out->hi = a->hi & b->hi;
}

void __extreme_i64_or(struct Words* out, const struct Words* a, const struct Words* b) {
    out->lo = a->lo | b->lo;
    out->hi = a->hi | b->hi;
}

void __extreme_i64_xor(struct Words* out, const struct Words* a, const struct Words* b) {
    out->lo = a->lo ^ b->lo;
    out->hi = a->hi ^ b->hi;
}

static uint32_t multiply_high(uint32_t a, uint32_t b) {
    uint32_t low = (a & 65535) * (b & 65535);
    uint32_t middle = (a >> 16) * (b & 65535) + (low >> 16);
    uint32_t carry = middle >> 16;
    middle = (middle & 65535) + (a & 65535) * (b >> 16);
    return (a >> 16) * (b >> 16) + carry + (middle >> 16);
}

void __extreme_i64_mul(struct Words* out, const struct Words* a, const struct Words* b) {
    out->hi = multiply_high(a->lo, b->lo) + a->lo * b->hi + a->hi * b->lo;
    out->lo = a->lo * b->lo;
}

void __extreme_i64_shl(struct Words* out, const struct Words* a, const struct Words* b) {
    uint32_t n = b->lo & 63;
    if (n == 0) {
        out->lo = a->lo;
        out->hi = a->hi;
    } else if (n >= 32) {
        out->hi = a->lo << (n - 32);
        out->lo = 0;
    } else {
        out->hi = (a->hi << n) | (a->lo >> (32 - n));
        out->lo = a->lo << n;
    }
}

void __extreme_i64_lshr(struct Words* out, const struct Words* a, const struct Words* b) {
    uint32_t n = b->lo & 63;
    if (n == 0) {
        out->lo = a->lo;
        out->hi = a->hi;
    } else if (n >= 32) {
        out->lo = a->hi >> (n - 32);
        out->hi = 0;
    } else {
        out->lo = (a->lo >> n) | (a->hi << (32 - n));
        out->hi = a->hi >> n;
    }
}

void __extreme_i64_ashr(struct Words* out, const struct Words* a, const struct Words* b) {
    uint32_t n = b->lo & 63;
    if (n == 0) {
        out->lo = a->lo;
        out->hi = a->hi;
    } else if (n >= 32) {
        out->lo = (uint32_t)((int32_t)a->hi >> (n - 32));
        out->hi = (uint32_t)((int32_t)a->hi >> 31);
    } else {
        out->lo = (a->lo >> n) | (a->hi << (32 - n));
        out->hi = (uint32_t)((int32_t)a->hi >> n);
    }
}

static void divide(struct Words* quotient, struct Words* remainder, const struct Words* a,
                   const struct Words* b) {
    uint32_t qlo = 0, qhi = 0, rlo = 0, rhi = 0;
    if ((b->lo | b->hi) == 0) {
        __builtin_trap();
    }
    for (unsigned bit = 64; bit != 0;) {
        --bit;
        uint32_t carry = rhi >> 31;
        rhi = (rhi << 1) | (rlo >> 31);
        uint32_t input = bit < 32 ? a->lo >> bit : a->hi >> (bit - 32);
        rlo = (rlo << 1) | (input & 1);
        if (carry || rhi > b->hi || (rhi == b->hi && rlo >= b->lo)) {
            uint32_t borrow = rlo < b->lo;
            rlo -= b->lo;
            rhi -= b->hi + borrow;
            if (bit < 32) {
                qlo |= (uint32_t)1 << bit;
            } else {
                qhi |= (uint32_t)1 << (bit - 32);
            }
        }
    }
    quotient->lo = qlo;
    quotient->hi = qhi;
    remainder->lo = rlo;
    remainder->hi = rhi;
}

static void negate(struct Words* value) {
    value->hi = ~value->hi + (value->lo == 0);
    value->lo = 0 - value->lo;
}

void __extreme_i64_udiv(struct Words* out, const struct Words* a, const struct Words* b) {
    struct Words remainder;
    divide(out, &remainder, a, b);
}

void __extreme_i64_urem(struct Words* out, const struct Words* a, const struct Words* b) {
    struct Words quotient;
    divide(&quotient, out, a, b);
}

static void signed_divide(struct Words* quotient, struct Words* remainder, const struct Words* a,
                          const struct Words* b) {
    struct Words left = {a->lo, a->hi}, right = {b->lo, b->hi};
    if ((int32_t)a->hi < 0) {
        negate(&left);
    }
    if ((int32_t)b->hi < 0) {
        negate(&right);
    }
    divide(quotient, remainder, &left, &right);
    if ((int32_t)(a->hi ^ b->hi) < 0) {
        negate(quotient);
    }
    if ((int32_t)a->hi < 0) {
        negate(remainder);
    }
}

void __extreme_i64_sdiv(struct Words* out, const struct Words* a, const struct Words* b) {
    struct Words remainder;
    signed_divide(out, &remainder, a, b);
}

void __extreme_i64_srem(struct Words* out, const struct Words* a, const struct Words* b) {
    struct Words quotient;
    signed_divide(&quotient, out, a, b);
}

void __extreme_i64_bswap(struct Words* out, const struct Words* a, const struct Words* unused) {
    (void)unused;
    out->lo = __builtin_bswap32(a->hi);
    out->hi = __builtin_bswap32(a->lo);
}
