#include <tasks/tasks.h>
using namespace extreme::tasks;
alignas(64) unsigned char source[2][192] = {{7, 9, 11}, {13, 17, 19}};
alignas(64) unsigned char destination[2][192];
volatile unsigned factor = 3;

__attribute__((noinline)) unsigned arithmetic(unsigned x) {
    volatile unsigned saved = x + 7;
    for (unsigned i = 0; i < 9; ++i) {
        saved = saved + factor;
    }
    return saved;
}

__attribute__((noinline)) unsigned copy_helper(unsigned task) {
    __builtin_memcpy(destination[task], source[task], 192);
    return arithmetic(task + 10);
}

void worker() {
    unsigned task = id();
    unsigned value = copy_helper(task);
    for (unsigned i = 0; i < 192; ++i) {
        if (destination[task][i] != source[task][i]) {
            abort(0xbad);
        }
    }
    finish(value);
}

void join() {
    unsigned a = 0, b = 0;
    if (read(0, a) != ok || read(1, b) != ok) {
        abort(0xbad1);
    }
    finish(a + b);
}

void empty_task() {}

void aborter() {
    abort(0xdeadbeef);
}

void must_not_run() {
    finish(0xbad2);
}

extern "C" unsigned kernel() {
    unsigned value = 123;
    if (id() != 0xffffffff || state(31) != empty || read(31, value) != not_ready || value != 0) {
        return 1;
    }
    if (issue(31, must_not_run, 1u << 30) != ok || disarm(31) != ok) {
        return 2;
    }
    if (state(31) != cancelled || read(31, value) != ok || value != 0xfffffffd) {
        return 3;
    }
    if (issue(28, empty_task) != ok) {
        return 10;
    }
    if (issue(29, must_not_run, 1u << 30) != ok || issue(30, aborter) != ok) {
        return 4;
    }
    if (issue(2, join, 3) != ok || issue(0, worker) != ok || issue(1, worker) != ok) {
        return 5;
    }
    // The same helper is used concurrently by boot code and both tasks.
    unsigned local = arithmetic(100);
    while (state(2) < complete || state(29) < complete || state(28) < complete) {
    }
    if (state(2) != complete || read(2, value) != ok || value != 89 || local != 134) {
        return 6;
    }
    if (state(30) != failed || read(30, value) != ok || value != 0xdeadbeef) {
        return 7;
    }
    if (state(29) != cancelled || read(29, value) != ok || value != 0xdeadbeef) {
        return 8;
    }
    if (state(28) != complete || read(28, value) != ok || value != 0) {
        return 11;
    }
    if (disarm(0) != ok || state(0) != empty) {
        return 9;
    }
    return 42;
}
