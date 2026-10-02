#pragma once
#include <iostream>
#include <stdexcept>
#include <vector>
#include <deque>
#include <random>
#include <type_traits>
#include <string>

inline void require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

using Word = cpphdl::logic<EC_BITS>;

inline Word pattern(uint32_t seed) {
    Word w = 0;
    for (unsigned j = 0; j < EC_BITS / 32; ++j) {
        w.bits(j * 32 + 31, j * 32) = uint32_t(seed * 2654435761u + j * 2246822519u);
    }
    return w;
}
#ifdef VERILATOR
#define OUT(name) dut.name
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define STR_(a) #a
#define STR(a) STR_(a)
#include STR(CAT(V, TEST_TOP).h)
using Dut = CAT(V, TEST_TOP);

template <class T> void drive_word(T& target, const Word& w) {
    if constexpr (std::is_integral_v<T>) {
        target = (uint64_t)w;
    } else {
        for (unsigned i = 0; i < EC_BITS / 32; ++i) {
            target[i] = (uint32_t)(w >> (i * 32));
        }
    }
}

template <class T> Word read_word(const T& source) {
    if constexpr (std::is_integral_v<T>) {
        return Word(source);
    } else {
        Word w = 0;
        for (unsigned i = 0; i < EC_BITS / 32; ++i) {
            w.bits(i * 32 + 31, i * 32) = source[i];
        }
        return w;
    }
}
#else
#define OUT(name) dut.name()
using Dut = TEST_TOP;

inline Word read_word(const Word& w) {
    return w;
}
#endif
long _system_clock = 0;
