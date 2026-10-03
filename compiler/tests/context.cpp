#include <context/context.h>

ExtremeContext parent_context, child_context, nested_context;
alignas(256) unsigned char child_storage[8192];
volatile unsigned stage;
alignas(EC_BUS_BYTES) unsigned char unaligned[sizeof(ExtremeContext) + 8];

__attribute__((noinline)) unsigned heap_style_context() {
    for (unsigned i = 0; i < sizeof(unaligned); ++i) {
        unaligned[i] = 0x5a;
    }
    auto* context = reinterpret_cast<ExtremeContext*>(unaligned + 4);
    int result = __extreme_context_save(context);
    if (!result) {
        __extreme_context_restore(context, 13);
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (unaligned[i] != 0x5a || unaligned[sizeof(unaligned) - 1 - i] != 0x5a) {
            return 1;
        }
    }
    return result == 13 ? 42 : 2;
}

__attribute__((noinline)) void leave_nested(unsigned depth) {
    volatile unsigned local = depth + 10;
    if (depth) {
        leave_nested(depth - 1);
    }
    __extreme_context_restore(&nested_context, local - 10);
}

__attribute__((noinline)) unsigned nested() {
    volatile unsigned preserved = 37;
    int result = __extreme_context_save(&nested_context);
    if (!result) {
        leave_nested(1);
    }
    // A zero longjmp argument must produce 1, and caller locals survive.
    return result == 1 && preserved == 37 ? 42 : 2;
}

__attribute__((noinline)) unsigned child_call() {
    volatile unsigned preserved = 91;
    int result = __extreme_context_save(&child_context);
    if (!result) {
        __extreme_context_restore(&parent_context, 7);
    }
    return preserved + result;
}

void child() {
    unsigned value = child_call();
    stage = value;
    __extreme_context_restore(&parent_context, 9);
}

extern "C" unsigned kernel() {
    if (nested() != 42 || heap_style_context() != 42) {
        return 1;
    }
    int result = __extreme_context_save(&parent_context);
    if (!result) {
        __extreme_context_enter(child, child_storage);
    }
    if (result == 7) {
        __extreme_context_restore(&child_context, 3);
    }
    return result == 9 && stage == 94 ? 42 : 3;
}
