#pragma once
#include <stdint.h>
#ifndef EC_BUS_BYTES
#error "Use the Extreme compiler to select the context layout"
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Save/restore the compiler ABI: two return registers, SP, activation base,
// aligned continuation PC and the saved call's result address. Not a general
// assembly register dump. Memory holding a live context must survive restoration.
struct ExtremeContext {
#ifdef __cplusplus
    alignas(EC_BUS_BYTES)
#else
    _Alignas(EC_BUS_BYTES)
#endif
        unsigned char storage[3 * EC_BUS_BYTES + 16];
};

__attribute__((returns_twice)) int __extreme_context_save(struct ExtremeContext*);
__attribute__((noreturn)) void __extreme_context_restore(const struct ExtremeContext*, int);
// Abandon the current call chain and enter a void() function with fresh return
// lanes and caller-provided activation storage. The function must not return.
__attribute__((noreturn)) void __extreme_context_enter(void (*entry)(void), void* storage);
#ifdef __cplusplus
}
#endif
